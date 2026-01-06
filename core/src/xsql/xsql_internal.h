#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../ast.h"
#include "../html_parser.h"

namespace xsql::xsql_internal {

/// Reads a file into memory for query execution helpers.
/// MUST throw on IO failures and MUST not perform network access.
/// Inputs are paths; outputs are contents with file IO side effects.
std::string read_file(const std::string& path);
/// Fetches a URL into memory using the configured HTTP backend.
/// MUST honor timeout_ms and MUST fail if networking is disabled.
/// Inputs are URL/timeout; outputs are contents with network side effects.
std::string fetch_url(const std::string& url, int timeout_ms);

/// Validates SELECT projection rules and aggregate constraints.
/// MUST throw on incompatible projections and MUST remain deterministic.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_projection(const Query& query);
/// Validates source qualifiers against the current FROM alias.
/// MUST throw on unknown qualifiers and MUST not mutate the query.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_qualifiers(const Query& query);
/// Validates predicate operators for supported fields.
/// MUST reject unsupported comparisons and MUST not mutate the query.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_predicates(const Query& query);
/// Validates ORDER BY fields against supported columns.
/// MUST throw on unsupported fields and MUST not mutate the query.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_order_by(const Query& query);
/// Validates TO TABLE usage for HTML table extraction.
/// MUST reject invalid combinations and MUST not mutate the query.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_to_table(const Query& query);
/// Validates export sink settings and required parameters.
/// MUST reject invalid paths and MUST not mutate the query.
/// Inputs are Query objects; outputs are exceptions on failure.
void validate_export_sink(const Query& query);

/// Checks whether a query selects only the table tag.
/// MUST return false for projected fields or aggregates.
/// Inputs are Query objects; outputs are boolean with no side effects.
bool is_table_select(const Query& query);

/// Builds output column names from query projections and aggregates.
/// MUST preserve deterministic ordering and MUST throw on invalid excludes.
/// Inputs are Query objects; outputs are column vectors.
std::vector<std::string> build_columns(const Query& query);
/// Finds an inner_html depth override if specified in the query.
/// MUST return nullopt when no depth is specified.
/// Inputs are Query objects; outputs are optional depth values.
std::optional<size_t> find_inner_html_depth(const Query& query);
/// Returns the TRIM() select item if present and valid.
/// MUST return nullptr when trimming is not enabled.
/// Inputs are Query objects; outputs are optional item pointers.
const Query::SelectItem* find_trim_item(const Query& query);

/// Builds a child adjacency list for efficient tree traversal.
/// MUST preserve node order and MUST size the vector to doc.nodes.
/// Inputs are HtmlDocument; outputs are children vectors.
std::vector<std::vector<int64_t>> build_children(const HtmlDocument& doc);
/// Collects table cell text for TO TABLE export and rendering.
/// MUST preserve row order and MUST ignore empty rows.
/// Inputs are doc/children/table_id; outputs are row vectors.
void collect_rows(const HtmlDocument& doc,
                  const std::vector<std::vector<int64_t>>& children,
                  int64_t table_id,
                  std::vector<std::vector<std::string>>& out_rows);
/// Limits inner_html content to a maximum nesting depth.
/// MUST preserve tag balance up to max_depth and MUST be deterministic.
/// Inputs are HTML and depth; outputs are truncated HTML strings.
std::string limit_inner_html(const std::string& html, size_t max_depth);
/// Extracts direct text nodes from inner_html without nested descendants.
/// MUST ignore text inside child tags and MUST preserve order.
/// Inputs are HTML strings; outputs are text-only strings.
std::string extract_direct_text(const std::string& html);

}  // namespace xsql::xsql_internal
