#include "executor_internal.h"

#include <optional>

namespace xsql::executor_internal {

namespace {

/// Compares optional integers with NULLS LAST semantics.
/// MUST return 0 for equal/null-equal and MUST be stable for sorting.
/// Inputs are optional ints; outputs are comparison integers.
int compare_nullable_int(std::optional<int64_t> left, std::optional<int64_t> right) {
  if (!left.has_value() && !right.has_value()) return 0;
  // WHY: keep NULLs last to match user expectations in ORDER BY.
  if (!left.has_value()) return 1;
  if (!right.has_value()) return -1;
  if (*left < *right) return -1;
  if (*left > *right) return 1;
  return 0;
}

/// Compares strings with default lexicographic ordering.
/// MUST use exact byte comparison for deterministic results.
/// Inputs are strings; outputs are comparison integers.
int compare_string(const std::string& left, const std::string& right) {
  if (left < right) return -1;
  if (left > right) return 1;
  return 0;
}

}  // namespace

/// Compares nodes by a supported field for ORDER BY evaluation.
/// MUST return 0 for unknown fields to preserve stable ordering.
/// Inputs are nodes/field; outputs are comparison integers.
int compare_nodes(const HtmlNode& left, const HtmlNode& right, const std::string& field) {
  if (field == "node_id") {
    if (left.id < right.id) return -1;
    if (left.id > right.id) return 1;
    return 0;
  }
  if (field == "tag") return compare_string(left.tag, right.tag);
  if (field == "text") return compare_string(left.text, right.text);
  if (field == "parent_id") return compare_nullable_int(left.parent_id, right.parent_id);
  if (field == "max_depth") {
    if (left.max_depth < right.max_depth) return -1;
    if (left.max_depth > right.max_depth) return 1;
    return 0;
  }
  if (field == "doc_order") {
    if (left.doc_order < right.doc_order) return -1;
    if (left.doc_order > right.doc_order) return 1;
    return 0;
  }
  return 0;
}

}  // namespace xsql::executor_internal
