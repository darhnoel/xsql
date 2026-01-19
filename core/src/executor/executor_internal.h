#pragma once

#include <string>
#include <vector>

#include "../executor.h"

namespace xsql::executor_internal {

/// Compares two nodes by a supported field for ORDER BY sorting.
/// MUST implement stable comparisons and MUST return 0 for equality.
/// Inputs are nodes/field; outputs are comparison integers with no side effects.
int compare_nodes(const HtmlNode& left, const HtmlNode& right, const std::string& field);
/// Evaluates a predicate expression against a node and document context.
/// MUST be deterministic and MUST respect axis semantics.
/// Inputs are expr/doc/children/node; outputs are boolean with no side effects.
bool eval_expr(const Expr& expr,
               const HtmlDocument& doc,
               const std::vector<std::vector<int64_t>>& children,
               const HtmlNode& node);
bool eval_expr_flatten_base(const Expr& expr,
                            const HtmlDocument& doc,
                            const std::vector<std::vector<int64_t>>& children,
                            const HtmlNode& node);
/// Checks membership of a string in a list for filtering decisions.
/// MUST use exact matching and MUST be case-sensitive.
/// Inputs are value/list; outputs are boolean with no side effects.
bool string_in_list(const std::string& value, const std::vector<std::string>& list);

}  // namespace xsql::executor_internal
