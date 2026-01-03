#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ast.h"
#include "xsql/xsql.h"

namespace xsql::cli {

/// Holds truncated output and whether truncation occurred for user messaging.
/// MUST keep output intact when truncated is false and MUST be consistent with truncation logic.
/// Inputs/outputs are the stored fields; side effects are none.
struct TruncateResult {
  std::string output;
  bool truncated = false;
};

/// Reads a file into memory for CLI queries that need filesystem input.
/// MUST throw on missing/unreadable files and MUST not perform network IO.
/// Inputs are a path; outputs are contents; side effects are file reads/errors.
std::string read_file(const std::string& path);
/// Reads all stdin content for non-interactive usage.
/// MUST block until EOF and MUST not interpret the stream contents.
/// Inputs are stdin; outputs are captured content; side effects are stream reads.
std::string read_stdin();
/// Removes trailing semicolons and whitespace for uniform query handling.
/// MUST preserve internal content and MUST only trim at the end.
/// Inputs are the raw string; outputs are trimmed string with no side effects.
std::string trim_semicolon(std::string value);
/// Applies ANSI coloring to JSON for readability when enabled.
/// MUST NOT alter JSON semantics and MUST be disabled for non-TTY output.
/// Inputs are JSON text and a flag; outputs are colored text with no side effects.
std::string colorize_json(const std::string& input, bool enable);
/// Truncates long output by keeping head/tail windows for interactive UX.
/// MUST preserve line boundaries and MUST indicate truncation explicitly.
/// Inputs are text and window sizes; outputs are truncated text with a flag.
TruncateResult truncate_output(const std::string& text, size_t head_lines, size_t tail_lines);
/// Checks whether a string should be treated as a URL.
/// MUST only match supported schemes and MUST avoid false positives.
/// Inputs are a string; outputs are a boolean with no side effects.
bool is_url(const std::string& value);
/// Loads HTML from a path or URL using CLI-side IO behavior.
/// MUST honor timeouts and MUST fail when network support is disabled.
/// Inputs are path/url and timeout; outputs are HTML text with IO side effects.
std::string load_html_input(const std::string& input, int timeout_ms);
/// Rewrites bare FROM paths into quoted string literals for parsing.
/// MUST only rewrite likely paths and MUST preserve user intent otherwise.
/// Inputs are the query string; outputs are rewritten query with no side effects.
std::string rewrite_from_path_if_needed(const std::string& query);
/// Cleans up pasted lines by removing prompts and extra whitespace.
/// MUST not remove meaningful content and MUST keep multiline intent.
/// Inputs are raw line text; outputs are sanitized text with no side effects.
std::string sanitize_pasted_line(std::string line);

/// Extracts a source descriptor from parsed queries for dispatch decisions.
/// MUST mirror parser semantics and MUST treat document as the default source.
/// Inputs are parsed values; outputs are the kind/value pair with no side effects.
struct QuerySource {
  xsql::Source::Kind kind = xsql::Source::Kind::Document;
  std::string value;
};

/// Parses the query to identify its source kind for execution routing.
/// MUST return nullopt on parse errors and MUST not throw for invalid queries.
/// Inputs are query text; outputs are optional source with no side effects.
std::optional<QuerySource> parse_query_source(const std::string& query);

/// Serializes full query rows into JSON objects for CLI output modes.
/// MUST preserve column ordering semantics and MUST escape content correctly.
/// Inputs are QueryResult rows; outputs are JSON text with no side effects.
std::string build_json(const xsql::QueryResult& result);
/// Serializes a single-column result into a JSON list.
/// MUST enforce single-column output and MUST represent NULLs explicitly.
/// Inputs are QueryResult rows; outputs are JSON list text with no side effects.
std::string build_json_list(const xsql::QueryResult& result);
/// Serializes table extraction output into JSON arrays.
/// MUST preserve row ordering and MUST keep cells as string values.
/// Inputs are table results; outputs are JSON text with no side effects.
std::string build_table_json(const xsql::QueryResult& result);
/// Serializes tag-count summaries into JSON for non-duckbox output modes.
/// MUST preserve deterministic ordering and MUST escape tag names.
/// Inputs are summary pairs; outputs are JSON text with no side effects.
std::string build_summary_json(const std::vector<std::pair<std::string, size_t>>& summary);

/// Renders an extracted HTML table into duckbox format for terminal display.
/// MUST honor max_rows and MUST avoid color when not in a TTY.
/// Inputs are table data and render options; outputs are formatted text.
std::string render_table_duckbox(const xsql::QueryResult::TableResult& table,
                                 bool highlight,
                                 bool is_tty,
                                 size_t max_rows);

/// Maps export sink kinds to user-facing labels for CLI messages.
/// MUST remain stable for scripts that parse output and MUST handle None.
/// Inputs are export kind values; outputs are labels with no side effects.
std::string export_kind_label(xsql::QueryResult::ExportSink::Kind kind);

}  // namespace xsql::cli
