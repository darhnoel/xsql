#include "xsql_internal.h"

#include <algorithm>
#include <stdexcept>

namespace xsql::xsql_internal {

/// Builds column names for the result set based on query semantics.
/// MUST enforce EXCLUDE rules and MUST preserve deterministic ordering.
/// Inputs are Query objects; outputs are column name vectors.
std::vector<std::string> build_columns(const Query& query) {
  for (const auto& item : query.select_items) {
    if (item.aggregate == Query::SelectItem::Aggregate::Count) {
      return {"count"};
    }
    if (item.aggregate == Query::SelectItem::Aggregate::Summarize) {
      return {"tag", "count"};
    }
    if (item.aggregate == Query::SelectItem::Aggregate::Tfidf) {
      return {"node_id", "parent_id", "tag", "terms_score"};
    }
  }
  if (!is_projection_query(query)) {
    std::vector<std::string> cols = {"node_id", "tag", "attributes", "parent_id", "max_depth", "doc_order"};
    if (!query.exclude_fields.empty()) {
      std::vector<std::string> out;
      out.reserve(cols.size());
      for (const auto& col : cols) {
        if (std::find(query.exclude_fields.begin(), query.exclude_fields.end(), col) ==
            query.exclude_fields.end()) {
          out.push_back(col);
        }
      }
      // WHY: empty columns would create unusable result schemas.
      if (out.empty()) {
        throw std::runtime_error("EXCLUDE removes all columns");
      }
      return out;
    }
    return cols;
  }
  std::vector<std::string> cols;
  size_t extra = 0;
  for (const auto& item : query.select_items) {
    if (item.flatten_text) {
      extra += item.flatten_aliases.size();
    }
  }
  cols.reserve(query.select_items.size() + extra);
  for (const auto& item : query.select_items) {
    if (item.flatten_text) {
      cols.insert(cols.end(), item.flatten_aliases.begin(), item.flatten_aliases.end());
      continue;
    }
    cols.push_back(*item.field);
  }
  return cols;
}

/// Extracts a shared inner_html depth override from the select items.
/// MUST return nullopt when inner_html is not projected.
/// Inputs are Query objects; outputs are optional depth values.
std::optional<size_t> find_inner_html_depth(const Query& query) {
  for (const auto& item : query.select_items) {
    if (!item.field.has_value() || *item.field != "inner_html") continue;
    if (item.inner_html_depth.has_value()) return item.inner_html_depth;
  }
  return std::nullopt;
}

/// Finds the single TRIM() select item if present.
/// MUST return nullptr when trimming is not enabled.
/// Inputs are Query objects; outputs are optional item pointers.
const Query::SelectItem* find_trim_item(const Query& query) {
  if (query.select_items.size() != 1) return nullptr;
  const auto& item = query.select_items[0];
  if (!item.trim) return nullptr;
  return &item;
}

}  // namespace xsql::xsql_internal
