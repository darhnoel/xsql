#include "xsql_internal.h"

#include <algorithm>
#include <functional>
#include <stdexcept>

#include "../util/string_util.h"

namespace xsql::xsql_internal {

namespace {

/// Detects predicates that filter beyond tag-only self comparisons.
/// MUST return true when any non-tag self predicate exists.
/// Inputs are Expr trees; outputs are boolean with no side effects.
bool has_non_tag_self_predicate(const Expr& expr) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    const auto& cmp = std::get<CompareExpr>(expr);
    return !(cmp.lhs.axis == Operand::Axis::Self && cmp.lhs.field_kind == Operand::FieldKind::Tag);
  }
  const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
  return has_non_tag_self_predicate(bin.left) || has_non_tag_self_predicate(bin.right);
}

/// Checks whether the SELECT list projects fields or aggregates.
/// MUST return false for tag-only selections.
/// Inputs are Query objects; outputs are boolean with no side effects.
bool is_projection_query(const Query& query) {
  for (const auto& item : query.select_items) {
    if (item.field.has_value() || item.aggregate != Query::SelectItem::Aggregate::None) return true;
  }
  return false;
}

/// Checks whether the query is a SUMMARIZE aggregate.
/// MUST require a single select item with Summarize.
/// Inputs are Query objects; outputs are boolean with no side effects.
bool is_summarize_query(const Query& query) {
  if (query.select_items.size() != 1) return false;
  return query.select_items[0].aggregate == Query::SelectItem::Aggregate::Summarize;
}

/// Checks whether SELECT includes the wildcard tag.
/// MUST detect '*' anywhere in the select list.
/// Inputs are Query objects; outputs are boolean with no side effects.
bool has_wildcard_tag(const Query& query) {
  for (const auto& item : query.select_items) {
    if (item.tag == "*") return true;
  }
  return false;
}

/// Checks whether the query is exactly SELECT *.
/// MUST return false when additional select items exist.
/// Inputs are Query objects; outputs are boolean with no side effects.
bool is_wildcard_only(const Query& query) {
  return query.select_items.size() == 1 && query.select_items[0].tag == "*";
}

}  // namespace

/// Validates projection, aggregate, and list/table rules.
/// MUST throw on incompatible combinations to keep output schemas stable.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_projection(const Query& query) {
  bool has_aggregate = false;
  bool has_summarize = false;
  bool has_trim = false;
  for (const auto& item : query.select_items) {
    if (item.aggregate != Query::SelectItem::Aggregate::None) {
      has_aggregate = true;
      if (item.aggregate == Query::SelectItem::Aggregate::Summarize) {
        has_summarize = true;
      }
      break;
    }
  }

  // WHY: ordering aggregate results without grouping is undefined in this engine.
  if (has_aggregate && !query.order_by.empty() && !has_summarize) {
    throw std::runtime_error("ORDER BY is not supported with aggregate queries");
  }

  if (!is_projection_query(query)) {
    if (!query.exclude_fields.empty() && !is_wildcard_only(query)) {
      throw std::runtime_error("EXCLUDE requires SELECT *");
    }
    if (!query.exclude_fields.empty()) {
      const std::vector<std::string> allowed = {"node_id", "tag", "attributes", "parent_id", "source_uri"};
      for (const auto& field : query.exclude_fields) {
        if (std::find(allowed.begin(), allowed.end(), field) == allowed.end()) {
          throw std::runtime_error("Unknown EXCLUDE field: " + field);
        }
      }
    }
    if (query.to_list) {
      throw std::runtime_error("TO LIST() requires a projected column");
    }
    if (has_wildcard_tag(query) && query.select_items.size() > 1) {
      throw std::runtime_error("SELECT * cannot be combined with other tags");
    }
    return;
  }
  if (query.to_table) {
    throw std::runtime_error("TO TABLE() cannot be used with projections");
  }
  if (has_aggregate) {
    if (query.select_items.size() != 1) {
      throw std::runtime_error("Aggregate queries require a single select item");
    }
    if (query.to_list) {
      throw std::runtime_error("Aggregate queries do not support TO LIST()");
    }
    if (query.select_items[0].aggregate == Query::SelectItem::Aggregate::Summarize) {
      return;
    }
    if (query.select_items[0].aggregate != Query::SelectItem::Aggregate::Count) {
      throw std::runtime_error("Unsupported aggregate");
    }
    return;
  }
  if (query.to_list && query.select_items.size() != 1) {
    throw std::runtime_error("TO LIST() requires a single projected column");
  }
  for (const auto& item : query.select_items) {
    if (item.trim) {
      has_trim = true;
      break;
    }
  }
  bool has_text_function = false;
  bool has_inner_html_function = false;
  for (const auto& item : query.select_items) {
    if (item.text_function) has_text_function = true;
    if (item.inner_html_function) has_inner_html_function = true;
  }
  if ((has_text_function || has_inner_html_function) && !query.where.has_value()) {
    throw std::runtime_error("TEXT()/INNER_HTML() requires a WHERE clause");
  }
  if (has_text_function || has_inner_html_function) {
    if (!query.where.has_value() || !has_non_tag_self_predicate(*query.where)) {
      throw std::runtime_error("TEXT()/INNER_HTML() requires a non-tag filter (e.g., attributes or parent)");
    }
  }
  if (has_trim && query.select_items.size() != 1) {
    throw std::runtime_error("TRIM() requires a single projected column");
  }
  std::string tag;
  std::optional<size_t> inner_html_depth;
  for (const auto& item : query.select_items) {
    if (!item.field.has_value()) {
      throw std::runtime_error("Cannot mix tag-only and projected fields in SELECT");
    }
    if (tag.empty()) {
      tag = item.tag;
    } else if (tag != item.tag) {
      throw std::runtime_error("Projected fields must use a single tag");
    }
    const std::string& field = *item.field;
    if (field == "text" && !item.text_function) {
      throw std::runtime_error("TEXT() must be used to project text");
    }
    if (field == "inner_html" && !item.inner_html_function) {
      throw std::runtime_error("INNER_HTML() must be used to project inner_html");
    }
    if (item.trim && field == "attributes") {
      throw std::runtime_error("TRIM() does not support attributes");
    }
    if (field == "inner_html") {
      if (item.inner_html_depth.has_value()) {
        if (inner_html_depth.has_value() && *inner_html_depth != *item.inner_html_depth) {
          throw std::runtime_error("inner_html() depth must be consistent");
        }
        inner_html_depth = item.inner_html_depth;
      } else if (inner_html_depth.has_value()) {
        throw std::runtime_error("inner_html() depth must be consistent");
      }
    }
    if (field != "node_id" && field != "tag" && field != "text" &&
        field != "inner_html" && field != "parent_id" && field != "source_uri" && field != "attributes") {
      // Treat other fields as attribute projections (e.g., link.href).
    }
  }
}

/// Validates that all qualifiers refer to the active source alias.
/// MUST throw on unknown qualifiers to prevent silent misrouting.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_qualifiers(const Query& query) {
  auto is_allowed = [&](const std::optional<std::string>& qualifier) -> bool {
    if (!qualifier.has_value()) return true;
    if (query.source.alias.has_value() && *qualifier == *query.source.alias) return true;
    if (query.source.kind == Source::Kind::Document && *qualifier == "document") return true;
    return false;
  };

  std::function<void(const Expr&)> visit = [&](const Expr& expr) {
    if (std::holds_alternative<CompareExpr>(expr)) {
      const auto& cmp = std::get<CompareExpr>(expr);
      if (!is_allowed(cmp.lhs.qualifier)) {
        throw std::runtime_error("Unknown qualifier: " + *cmp.lhs.qualifier);
      }
      return;
    }
    const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
    visit(bin.left);
    visit(bin.right);
  };

  if (query.where.has_value()) {
    visit(*query.where);
  }
}

/// Validates predicate operators against supported field kinds.
/// MUST throw on unsupported comparisons for map-typed fields.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_predicates(const Query& query) {
  std::function<void(const Expr&)> visit = [&](const Expr& expr) {
    if (std::holds_alternative<CompareExpr>(expr)) {
      const auto& cmp = std::get<CompareExpr>(expr);
      if (cmp.lhs.field_kind == Operand::FieldKind::AttributesMap) {
        if (cmp.op != CompareExpr::Op::IsNull && cmp.op != CompareExpr::Op::IsNotNull) {
          throw std::runtime_error("attributes supports only IS NULL or IS NOT NULL");
        }
      }
      if (cmp.op == CompareExpr::Op::Regex) {
        for (const auto& value : cmp.rhs.values) {
          if (value.size() > kMaxRegexLength) {
            throw std::runtime_error("Regex pattern exceeds maximum length");
          }
        }
      }
      return;
    }
    const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
    visit(bin.left);
    visit(bin.right);
  };

  if (query.where.has_value()) {
    visit(*query.where);
  }
}

/// Validates LIMIT values to prevent excessive output sizes.
/// MUST enforce configured caps for predictable execution time.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_limits(const Query& query) {
  if (query.limit.has_value() && *query.limit > kMaxLimit) {
    throw std::runtime_error("LIMIT exceeds maximum allowed rows");
  }
}

/// Validates ORDER BY fields and summarize ordering constraints.
/// MUST throw on unsupported fields to avoid incorrect sorting.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_order_by(const Query& query) {
  if (query.order_by.empty()) return;
  // Assumption: ORDER BY is limited to core node fields for now (no attributes/expressions).
  for (const auto& order_by : query.order_by) {
    const std::string& field = order_by.field;
    if (is_summarize_query(query)) {
      if (field != "tag" && field != "count") {
        throw std::runtime_error("ORDER BY supports tag or count for SUMMARIZE()");
      }
      continue;
    }
    if (field != "node_id" && field != "tag" && field != "text" && field != "parent_id") {
      throw std::runtime_error("ORDER BY supports node_id, tag, text, or parent_id");
    }
  }
}

/// Validates TO TABLE usage for HTML table extraction.
/// MUST enforce table-only selection rules.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_to_table(const Query& query) {
  if (!query.to_table) return;
  if (query.to_list) {
    throw std::runtime_error("TO TABLE() cannot be combined with TO LIST()");
  }
  if (!query.select_items.empty()) {
    if (query.select_items.size() != 1) {
      throw std::runtime_error("TO TABLE() requires a single select item");
    }
    const auto& item = query.select_items[0];
    if (item.aggregate != Query::SelectItem::Aggregate::None || item.field.has_value()) {
      throw std::runtime_error("TO TABLE() requires a tag-only SELECT");
    }
    if (util::to_lower(item.tag) != "table") {
      throw std::runtime_error("TO TABLE() only supports SELECT table");
    }
  }
}

/// Checks if a query selects only table tags without projection.
/// MUST return false for aggregates or field projections.
/// Inputs are Query objects; outputs are boolean with no side effects.
bool is_table_select(const Query& query) {
  if (query.select_items.size() != 1) return false;
  const auto& item = query.select_items[0];
  if (item.aggregate != Query::SelectItem::Aggregate::None) return false;
  if (item.field.has_value()) return false;
  return util::to_lower(item.tag) == "table";
}

/// Validates export sink configuration and incompatible flags.
/// MUST throw on empty paths or invalid combinations.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_export_sink(const Query& query) {
  if (!query.export_sink.has_value()) return;
  if (query.to_list) {
    throw std::runtime_error("TO LIST() cannot be combined with export sinks");
  }
  if (query.export_sink->path.empty()) {
    throw std::runtime_error("Export path cannot be empty");
  }
}

}  // namespace xsql::xsql_internal
