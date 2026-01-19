#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace xsql {

/// Represents a single materialized row so callers can format or export results consistently.
/// MUST keep fields aligned with the executor/output contract to avoid schema drift.
/// Inputs/outputs are the row fields; side effects are none but consumers may rely on defaults.
struct QueryResultRow {
  int64_t node_id = 0;
  std::string tag;
  std::string text;
  std::string inner_html;
  std::unordered_map<std::string, double> term_scores;
  std::unordered_map<std::string, std::string> attributes;
  std::unordered_map<std::string, std::string> computed_fields;
  std::optional<int64_t> parent_id;
  int64_t sibling_pos = 0;
  int64_t max_depth = 0;
  int64_t doc_order = 0;
  std::string source_uri;
};

/// Carries the full query output, including row sets, tables, and export intent.
/// MUST keep columns consistent with rows/tables and MUST NOT mix incompatible modes.
/// Inputs/outputs are populated by execution; side effects include downstream export behavior.
struct QueryResult {
  struct ExportSink {
    /// Describes an export target so the CLI can write files without re-parsing the query.
    /// MUST use Kind::None to indicate no export and MUST carry a valid path otherwise.
    /// Inputs are kind/path; side effects occur when the CLI performs the write.
    enum class Kind { None, Csv, Parquet } kind = Kind::None;
    // WHY: defaulting to None prevents accidental file writes when a sink is not specified.
    std::string path;
  };
  std::vector<std::string> columns;
  std::vector<QueryResultRow> rows;
  /// True when output columns come from implicit defaults (e.g., SELECT * or tag-only).
  bool columns_implicit = false;
  /// True when EXCLUDE explicitly removed source_uri in the query.
  bool source_uri_excluded = false;
  bool to_list = false;
  struct TableResult {
    /// Holds extracted HTML table rows when TO TABLE() is requested.
    /// MUST preserve row order from the source table and MUST NOT mutate cell contents.
    /// Inputs are node_id/rows; side effects are none but output formatting depends on them.
    int64_t node_id = 0;
    std::vector<std::vector<std::string>> rows;
  };
  std::vector<TableResult> tables;
  bool to_table = false;
  bool table_has_header = true;
  ExportSink export_sink;
};

/// Executes a query over an in-memory HTML document for zero-IO operation.
/// MUST receive valid HTML and MUST treat the input as immutable.
/// Inputs are HTML/query; failures throw exceptions and side effects are none.
QueryResult execute_query_from_document(const std::string& html, const std::string& query);
/// Executes a query over a file path and loads the file contents internally.
/// MUST read from disk and MUST report errors via exceptions on IO failures.
/// Inputs are path/query; side effects include file reads and thrown errors.
QueryResult execute_query_from_file(const std::string& path, const std::string& query);
/// Executes a query over a URL using the configured network backend.
/// MUST honor timeout_ms and MUST fail if network support is unavailable.
/// Inputs are url/query/timeout; side effects include network IO and thrown errors.
QueryResult execute_query_from_url(const std::string& url, const std::string& query, int timeout_ms);

}  // namespace xsql
