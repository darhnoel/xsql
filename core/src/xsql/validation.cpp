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
    if (cmp.op == CompareExpr::Op::HasDirectText) return true;
    return !(cmp.lhs.axis == Operand::Axis::Self && cmp.lhs.field_kind == Operand::FieldKind::Tag);
  }
  if (std::holds_alternative<std::shared_ptr<ExistsExpr>>(expr)) {
    const auto& exists = *std::get<std::shared_ptr<ExistsExpr>>(expr);
    if (exists.axis != Operand::Axis::Self) return true;
    if (!exists.where.has_value()) return false;
    return has_non_tag_self_predicate(*exists.where);
  }
  const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
  return has_non_tag_self_predicate(bin.left) || has_non_tag_self_predicate(bin.right);
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

bool scan_descendant_filter(const Expr& expr,
                            size_t& count,
                            bool& has_or,
                            bool& invalid) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    const auto& cmp = std::get<CompareExpr>(expr);
    if (cmp.lhs.axis == Operand::Axis::Descendant) {
      ++count;
      if (cmp.lhs.field_kind == Operand::FieldKind::Tag) {
        if (cmp.op != CompareExpr::Op::Eq && cmp.op != CompareExpr::Op::In) {
          invalid = true;
        }
      } else if (cmp.lhs.field_kind == Operand::FieldKind::Attribute) {
        if (cmp.op != CompareExpr::Op::Eq &&
            cmp.op != CompareExpr::Op::In &&
            cmp.op != CompareExpr::Op::Contains &&
            cmp.op != CompareExpr::Op::ContainsAll &&
            cmp.op != CompareExpr::Op::ContainsAny) {
          invalid = true;
        }
      } else {
        invalid = true;
      }
      if (cmp.rhs.values.empty()) {
        invalid = true;
      }
      return true;
    }
    return false;
  }
  if (std::holds_alternative<std::shared_ptr<ExistsExpr>>(expr)) {
    return false;
  }
  const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
  bool left = scan_descendant_filter(bin.left, count, has_or, invalid);
  bool right = scan_descendant_filter(bin.right, count, has_or, invalid);
  if (bin.op == BinaryExpr::Op::Or && (left || right)) {
    has_or = true;
  }
  return left || right;
}

}  // namespace

bool is_projection_query(const Query& query) {
  for (const auto& item : query.select_items) {
    if (item.flatten_text || item.field.has_value() ||
        item.aggregate != Query::SelectItem::Aggregate::None) {
      return true;
    }
  }
  return false;
}

/// Validates projection, aggregate, and list/table rules.
/// MUST throw on incompatible combinations to keep output schemas stable.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_projection(const Query& query) {
  bool has_aggregate = false;
  bool has_summarize = false;
  bool has_trim = false;
  bool has_flatten = false;
  const Query::SelectItem* flatten_item = nullptr;
  for (const auto& item : query.select_items) {
    if (item.flatten_text) {
      if (flatten_item != nullptr) {
        throw std::runtime_error("FLATTEN_TEXT() supports a single instance per query");
      }
      flatten_item = &item;
      has_flatten = true;
    }
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
  if (has_flatten && has_aggregate) {
    throw std::runtime_error("FLATTEN_TEXT() cannot be combined with aggregates");
  }
  if (has_flatten && query.where.has_value()) {
    size_t descendant_count = 0;
    bool has_or = false;
    bool invalid = false;
    scan_descendant_filter(*query.where, descendant_count, has_or, invalid);
    if (invalid) {
      throw std::runtime_error("descendant filters support tag (=, IN) and attributes (=, IN, CONTAINS, CONTAINS ALL/ANY)");
    }
    if (has_or) {
      throw std::runtime_error("descendant filters cannot be combined with OR when using FLATTEN_TEXT()");
    }
  }

  if (!is_projection_query(query)) {
    if (!query.exclude_fields.empty() && !is_wildcard_only(query)) {
      throw std::runtime_error("EXCLUDE requires SELECT *");
    }
    if (!query.exclude_fields.empty()) {
      const std::vector<std::string> allowed = {"node_id", "tag", "attributes", "parent_id",
                                                "max_depth", "doc_order", "source_uri"};
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
    if (query.select_items[0].aggregate == Query::SelectItem::Aggregate::Tfidf) {
      if (query.select_items[0].tfidf_top_terms == 0) {
        throw std::runtime_error("TFIDF requires TOP_TERMS > 0");
      }
      if (query.select_items[0].tfidf_max_df > 0 &&
          query.select_items[0].tfidf_max_df < query.select_items[0].tfidf_min_df) {
        throw std::runtime_error("TFIDF MAX_DF must be >= MIN_DF");
      }
      return;
    }
    if (query.select_items[0].aggregate != Query::SelectItem::Aggregate::Count) {
      throw std::runtime_error("Unsupported aggregate");
    }
    return;
  }
  if (has_flatten && flatten_item != nullptr) {
    if (flatten_item->flatten_aliases.empty()) {
      throw std::runtime_error("FLATTEN_TEXT() requires AS (col1, ...) column aliases");
    }
    if (query.to_list) {
      if (flatten_item->flatten_aliases.size() != 1 || query.select_items.size() != 1) {
        throw std::runtime_error("TO LIST() requires a single projected column");
      }
    }
  } else if (query.to_list && query.select_items.size() != 1) {
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
    throw std::runtime_error("TEXT()/INNER_HTML()/RAW_INNER_HTML() requires a WHERE clause");
  }
  if (has_text_function || has_inner_html_function) {
    if (!query.where.has_value() || !has_non_tag_self_predicate(*query.where)) {
      throw std::runtime_error(
          "TEXT()/INNER_HTML()/RAW_INNER_HTML() requires a non-tag filter (e.g., attributes or parent)");
    }
  }
  if (has_trim && query.select_items.size() != 1) {
    throw std::runtime_error("TRIM() requires a single projected column");
  }
  std::string tag;
  std::optional<size_t> inner_html_depth;
  std::optional<bool> inner_html_raw;
  for (const auto& item : query.select_items) {
    if (!item.field.has_value() && !item.flatten_text) {
      throw std::runtime_error("Cannot mix tag-only and projected fields in SELECT");
    }
    if (tag.empty()) {
      tag = item.tag;
    } else if (tag != item.tag) {
      throw std::runtime_error("Projected fields must use a single tag");
    }
    if (item.flatten_text) {
      continue;
    }
    const std::string& field = *item.field;
    if (field == "text" && !item.text_function) {
      throw std::runtime_error("TEXT() must be used to project text");
    }
    if (field == "inner_html" && !item.inner_html_function) {
      throw std::runtime_error("INNER_HTML() must be used to project inner_html");
    }
    if (item.trim && (field == "attributes" || field == "sibling_pos")) {
      throw std::runtime_error("TRIM() does not support attributes or sibling_pos");
    }
    if (field == "inner_html") {
      if (inner_html_raw.has_value() && *inner_html_raw != item.raw_inner_html_function) {
        throw std::runtime_error("INNER_HTML() and RAW_INNER_HTML() cannot be mixed in one SELECT");
      }
      inner_html_raw = item.raw_inner_html_function;
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
        field != "inner_html" && field != "parent_id" && field != "source_uri" &&
        field != "attributes" && field != "sibling_pos" &&
        field != "max_depth" && field != "doc_order") {
      // Treat other fields as attribute projections (e.g., link.href).
    }
  }
}

/// Validates that all qualifiers refer to the active source alias.
/// MUST throw on unknown qualifiers to prevent silent misrouting.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_qualifiers(const Query& query) {
  std::optional<std::string> sole_tag;
  bool tag_consistent = true;
  for (const auto& item : query.select_items) {
    if (item.aggregate == Query::SelectItem::Aggregate::Tfidf) {
      tag_consistent = false;
      break;
    }
    std::string tag = util::to_lower(item.tag);
    if (tag.empty() || tag == "*") {
      tag_consistent = false;
      break;
    }
    if (!sole_tag.has_value()) {
      sole_tag = tag;
    } else if (*sole_tag != tag) {
      tag_consistent = false;
      break;
    }
  }
  if (!tag_consistent) {
    sole_tag.reset();
  }

  auto is_allowed = [&](const std::optional<std::string>& qualifier) -> bool {
    if (!qualifier.has_value()) return true;
    if (query.source.alias.has_value() && *qualifier == *query.source.alias) return true;
    if (query.source.kind == Source::Kind::Document && *qualifier == "document") return true;
    if (sole_tag.has_value() && util::to_lower(*qualifier) == *sole_tag) return true;
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
    if (std::holds_alternative<std::shared_ptr<ExistsExpr>>(expr)) {
      const auto& exists = *std::get<std::shared_ptr<ExistsExpr>>(expr);
      if (exists.where.has_value()) {
        visit(*exists.where);
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
      if (cmp.op == CompareExpr::Op::Contains ||
          cmp.op == CompareExpr::Op::ContainsAll ||
          cmp.op == CompareExpr::Op::ContainsAny) {
        if (cmp.lhs.field_kind != Operand::FieldKind::Attribute) {
          throw std::runtime_error("CONTAINS supports only attributes");
        }
        if (cmp.op == CompareExpr::Op::Contains && cmp.rhs.values.size() != 1) {
          throw std::runtime_error("CONTAINS expects a single string literal");
        }
        if ((cmp.op == CompareExpr::Op::ContainsAll || cmp.op == CompareExpr::Op::ContainsAny) &&
            cmp.rhs.values.empty()) {
          throw std::runtime_error("CONTAINS expects at least one value");
        }
      }
      if (cmp.op == CompareExpr::Op::HasDirectText) {
        if (cmp.lhs.axis != Operand::Axis::Self ||
            cmp.lhs.field_kind != Operand::FieldKind::Tag ||
            cmp.lhs.attribute.empty()) {
          throw std::runtime_error("HAS_DIRECT_TEXT expects a tag identifier");
        }
        if (cmp.rhs.values.size() != 1) {
          throw std::runtime_error("HAS_DIRECT_TEXT expects a single string literal");
        }
      }
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
    if (std::holds_alternative<std::shared_ptr<ExistsExpr>>(expr)) {
      const auto& exists = *std::get<std::shared_ptr<ExistsExpr>>(expr);
      if (exists.where.has_value()) {
        visit(*exists.where);
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
    if (field != "node_id" && field != "tag" && field != "text" &&
        field != "parent_id" && field != "sibling_pos" &&
        field != "max_depth" && field != "doc_order") {
      throw std::runtime_error("ORDER BY supports node_id, tag, text, parent_id, sibling_pos, max_depth, or doc_order");
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
