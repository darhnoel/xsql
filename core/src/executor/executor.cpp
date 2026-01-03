#include "../executor.h"

#include <algorithm>
#include <vector>

#include "executor_internal.h"
#include "../util/string_util.h"

namespace xsql {

/// Executes a query against a parsed HTML document.
/// MUST respect WHERE, ORDER BY, and LIMIT semantics deterministically.
/// Inputs are query/doc/source_uri; outputs are ExecuteResult with no side effects.
ExecuteResult execute_query(const Query& query, const HtmlDocument& doc, const std::string& source_uri) {
  ExecuteResult result;
  std::vector<std::string> select_tags;
  select_tags.reserve(query.select_items.size());
  bool select_all = false;
  for (const auto& item : query.select_items) {
    if (item.tag == "*") {
      select_all = true;
      break;
    }
    select_tags.push_back(util::to_lower(item.tag));
  }

  std::vector<std::vector<int64_t>> children(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (node.parent_id.has_value()) {
      children.at(static_cast<size_t>(*node.parent_id)).push_back(node.id);
    }
  }

  for (const auto& node : doc.nodes) {
    // WHY: skip non-selected tags early to reduce downstream filtering cost.
    if (!select_all && !executor_internal::string_in_list(node.tag, select_tags)) continue;
    if (query.where.has_value()) {
      if (!executor_internal::eval_expr(*query.where, doc, children, node)) continue;
    }
    HtmlNode out = node;
    out.tag = node.tag;
    result.nodes.push_back(out);
  }

  if (!query.order_by.empty()) {
    std::stable_sort(result.nodes.begin(), result.nodes.end(),
                     [&](const HtmlNode& left, const HtmlNode& right) {
                       for (const auto& order_by : query.order_by) {
                         int cmp = executor_internal::compare_nodes(left, right, order_by.field);
                         if (cmp == 0) continue;
                         if (order_by.descending) {
                           return cmp > 0;
                         }
                         return cmp < 0;
                       }
                       return false;
                     });
  }

  if (query.limit.has_value() && result.nodes.size() > *query.limit) {
    result.nodes.resize(*query.limit);
  }

  return result;
}

}  // namespace xsql
