#include "xsql/xsql.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

#include "../executor.h"
#include "../html_parser.h"
#include "../query_parser.h"
#include "../util/string_util.h"
#include "xsql_internal.h"

namespace xsql {

namespace {

/// Executes a parsed query over provided HTML and assembles QueryResult.
/// MUST apply validation before execution and MUST propagate errors as exceptions.
/// Inputs are HTML/source/query; outputs are QueryResult with no side effects.
QueryResult execute_query_from_html(const std::string& html,
                                    const std::string& source_uri,
                                    const std::string& query) {
  auto parsed = parse_query(query);
  if (!parsed.query.has_value()) {
    throw std::runtime_error("Query parse error: " + parsed.error->message);
  }
  xsql_internal::validate_projection(*parsed.query);
  xsql_internal::validate_order_by(*parsed.query);
  xsql_internal::validate_to_table(*parsed.query);
  xsql_internal::validate_export_sink(*parsed.query);
  xsql_internal::validate_qualifiers(*parsed.query);
  xsql_internal::validate_predicates(*parsed.query);
  auto inner_html_depth = xsql_internal::find_inner_html_depth(*parsed.query);
  const Query::SelectItem* trim_item = xsql_internal::find_trim_item(*parsed.query);
  bool use_text_function = false;
  bool use_inner_html_function = false;
  for (const auto& item : parsed.query->select_items) {
    if (item.field.has_value() && *item.field == "text" && item.text_function) {
      use_text_function = true;
    }
    if (item.field.has_value() && *item.field == "inner_html" && item.inner_html_function) {
      use_inner_html_function = true;
    }
  }
  std::optional<size_t> effective_inner_html_depth = inner_html_depth;
  if (!effective_inner_html_depth.has_value() && use_inner_html_function) {
    effective_inner_html_depth = 1;
  }

  HtmlDocument doc = parse_html(html);
  ExecuteResult exec = execute_query(*parsed.query, doc, source_uri);
  QueryResult out;
  out.columns = xsql_internal::build_columns(*parsed.query);
  out.to_list = parsed.query->to_list;
  out.to_table = parsed.query->to_table;
  if (parsed.query->export_sink.has_value()) {
    const auto& sink = *parsed.query->export_sink;
    if (sink.kind == Query::ExportSink::Kind::Csv) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Csv;
    } else if (sink.kind == Query::ExportSink::Kind::Parquet) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Parquet;
    }
    out.export_sink.path = sink.path;
  }
  if (!parsed.query->select_items.empty() &&
      parsed.query->select_items[0].aggregate == Query::SelectItem::Aggregate::Summarize) {
    std::unordered_map<std::string, size_t> counts;
    for (const auto& node : exec.nodes) {
      ++counts[node.tag];
    }
    std::vector<std::pair<std::string, size_t>> summary;
    summary.reserve(counts.size());
    for (const auto& kv : counts) {
      summary.emplace_back(kv.first, kv.second);
    }
    if (!parsed.query->order_by.empty()) {
      std::sort(summary.begin(), summary.end(),
                [&](const auto& a, const auto& b) {
                  for (const auto& order_by : parsed.query->order_by) {
                    int cmp = 0;
                    if (order_by.field == "count") {
                      if (a.second < b.second) cmp = -1;
                      else if (a.second > b.second) cmp = 1;
                    } else {
                      if (a.first < b.first) cmp = -1;
                      else if (a.first > b.first) cmp = 1;
                    }
                    if (cmp == 0) continue;
                    if (order_by.descending) {
                      return cmp > 0;
                    }
                    return cmp < 0;
                  }
                  return false;
                });
    } else {
      std::sort(summary.begin(), summary.end(),
                [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second > b.second;
                  return a.first < b.first;
                });
    }
    if (parsed.query->limit.has_value() && summary.size() > *parsed.query->limit) {
      summary.resize(*parsed.query->limit);
    }
    for (const auto& item : summary) {
      QueryResultRow row;
      row.tag = item.first;
      row.node_id = static_cast<int64_t>(item.second);
      row.source_uri = source_uri;
      out.rows.push_back(std::move(row));
    }
    return out;
  }
  // WHY: table extraction bypasses row projections to preserve table layout.
  if (parsed.query->to_table ||
      (parsed.query->export_sink.has_value() && xsql_internal::is_table_select(*parsed.query))) {
    auto children = xsql_internal::build_children(doc);
    for (const auto& node : exec.nodes) {
      QueryResult::TableResult table;
      table.node_id = node.id;
      xsql_internal::collect_rows(doc, children, node.id, table.rows);
      out.tables.push_back(std::move(table));
    }
    return out;
  }
  for (const auto& item : parsed.query->select_items) {
    if (item.aggregate == Query::SelectItem::Aggregate::Count) {
      QueryResultRow row;
      row.node_id = static_cast<int64_t>(exec.nodes.size());
      row.source_uri = source_uri;
      out.rows.push_back(row);
      return out;
    }
  }
  for (const auto& node : exec.nodes) {
    QueryResultRow row;
    row.node_id = node.id;
    row.tag = node.tag;
    row.text = use_text_function ? xsql_internal::extract_direct_text(node.inner_html) : node.text;
    row.inner_html = effective_inner_html_depth.has_value()
                         ? xsql_internal::limit_inner_html(node.inner_html, *effective_inner_html_depth)
                         : node.inner_html;
    row.attributes = node.attributes;
    row.source_uri = source_uri;
    if (trim_item != nullptr && trim_item->field.has_value()) {
      const std::string& field = *trim_item->field;
      if (field == "text") {
        row.text = util::trim_ws(row.text);
      } else if (field == "inner_html") {
        row.inner_html = util::trim_ws(row.inner_html);
      } else if (field == "tag") {
        row.tag = util::trim_ws(row.tag);
      } else if (field == "source_uri") {
        row.source_uri = util::trim_ws(row.source_uri);
      } else {
        auto it = row.attributes.find(field);
        if (it != row.attributes.end()) {
          it->second = util::trim_ws(it->second);
        }
      }
    }
    row.parent_id = node.parent_id;
    out.rows.push_back(row);
  }
  return out;
}

}  // namespace

/// Executes a query over in-memory HTML with document as the source label.
/// MUST not perform IO and MUST propagate parse/validation failures.
/// Inputs are HTML/query; outputs are QueryResult with no side effects.
QueryResult execute_query_from_document(const std::string& html, const std::string& query) {
  return execute_query_from_html(html, "document", query);
}

/// Executes a query over a file and uses the path as source label.
/// MUST read from disk and MUST propagate IO failures as exceptions.
/// Inputs are path/query; outputs are QueryResult with file IO side effects.
QueryResult execute_query_from_file(const std::string& path, const std::string& query) {
  std::string html = xsql_internal::read_file(path);
  return execute_query_from_html(html, path, query);
}

/// Executes a query over a URL and uses the URL as source label.
/// MUST honor timeout_ms and MUST propagate network failures as exceptions.
/// Inputs are url/query/timeout; outputs are QueryResult with network side effects.
QueryResult execute_query_from_url(const std::string& url, const std::string& query, int timeout_ms) {
  std::string html = xsql_internal::fetch_url(url, timeout_ms);
  return execute_query_from_html(html, url, query);
}

}  // namespace xsql
