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

struct FragmentSource {
  std::vector<std::string> fragments;
};

bool looks_like_html_fragment(const std::string& value) {
  return value.find('<') != std::string::npos && value.find('>') != std::string::npos;
}

std::optional<std::string> field_value_string(const QueryResultRow& row, const std::string& field) {
  if (field == "node_id") return std::to_string(row.node_id);
  if (field == "count") return std::to_string(row.node_id);
  if (field == "tag") return row.tag;
  if (field == "text") return row.text;
  if (field == "inner_html") return row.inner_html;
  if (field == "parent_id") {
    if (!row.parent_id.has_value()) return std::nullopt;
    return std::to_string(*row.parent_id);
  }
  if (field == "sibling_pos") return std::to_string(row.sibling_pos);
  if (field == "source_uri") return row.source_uri;
  if (field == "attributes") return std::nullopt;
  auto it = row.attributes.find(field);
  if (it == row.attributes.end()) return std::nullopt;
  return it->second;
}

QueryResult build_meta_result(const std::vector<std::string>& columns,
                              const std::vector<std::vector<std::string>>& rows) {
  QueryResult out;
  out.columns = columns;
  for (const auto& values : rows) {
    QueryResultRow row;
    for (size_t i = 0; i < columns.size() && i < values.size(); ++i) {
      const auto& col = columns[i];
      const auto& value = values[i];
      if (col == "source_uri") {
        row.source_uri = value;
      } else {
        row.attributes[col] = value;
      }
    }
    out.rows.push_back(std::move(row));
  }
  return out;
}

QueryResult execute_meta_query(const Query& query, const std::string& source_uri) {
  switch (query.kind) {
    case Query::Kind::ShowInput: {
      return build_meta_result({"key", "value"},
                               {{"source_uri", source_uri}});
    }
    case Query::Kind::ShowInputs: {
      return build_meta_result({"source_uri"},
                               {{source_uri}});
    }
    case Query::Kind::ShowFunctions: {
      return build_meta_result(
          {"function", "returns", "description"},
          {
              {"text(tag)", "string", "Text content of a tag"},
              {"inner_html(tag[, depth])", "string", "HTML inside a tag"},
              {"trim(inner_html(...))", "string", "Trim whitespace in inner_html"},
              {"count(tag|*)", "int64", "Aggregate node count"},
              {"summarize(*)", "table<tag,count>", "Tag counts summary"},
              {"tfidf(tag|*)", "map<string,double>", "TF-IDF term scores"},
          });
    }
    case Query::Kind::ShowAxes: {
      return build_meta_result(
          {"axis", "description"},
          {
              {"parent", "Parent node"},
              {"child", "Direct child nodes"},
              {"ancestor", "Any ancestor node"},
              {"descendant", "Any descendant node"},
          });
    }
    case Query::Kind::ShowOperators: {
      return build_meta_result(
          {"operator", "description"},
          {
              {"=", "Equality"},
              {"<>", "Not equal"},
              {"IN (...)", "Membership"},
              {"CONTAINS", "Substring or list contains"},
              {"CONTAINS ALL", "Contains all values"},
              {"CONTAINS ANY", "Contains any value"},
              {"IS NULL", "Null check"},
              {"IS NOT NULL", "Not-null check"},
              {"HAS_DIRECT_TEXT", "Direct text predicate"},
              {"~", "Regex match"},
              {"AND", "Logical AND"},
              {"OR", "Logical OR"},
          });
    }
    case Query::Kind::DescribeDoc: {
      return build_meta_result(
          {"column_name", "type", "nullable", "notes"},
          {
              {"node_id", "int64", "false", "Stable node identifier"},
              {"tag", "string", "false", "Lowercase tag name"},
              {"attributes", "map<string,string>", "false", "HTML attributes"},
              {"parent_id", "int64", "true", "Null for root"},
              {"sibling_pos", "int64", "false", "1-based among siblings"},
              {"source_uri", "string", "true", "Empty for RAW/STDIN"},
          });
    }
    case Query::Kind::DescribeLanguage: {
      return build_meta_result(
          {"category", "name", "syntax", "notes"},
          {
              {"clause", "SELECT", "SELECT <tag|*>[, ...]", "Tag list or *"},
              {"clause", "FROM", "FROM <source>", "Defaults to document in REPL"},
              {"clause", "WHERE", "WHERE <expr>", "Predicate expression"},
              {"clause", "ORDER BY", "ORDER BY <field> [ASC|DESC]",
               "node_id, tag, text, parent_id, sibling_pos; SUMMARIZE uses tag/count"},
              {"clause", "LIMIT", "LIMIT <n>", "n >= 0, max enforced"},
              {"clause", "EXCLUDE", "EXCLUDE <field>[, ...]", "Only with SELECT *"},
              {"output", "TO LIST", "TO LIST()", "Requires one projected column"},
              {"output", "TO TABLE", "TO TABLE([HEADER|NOHEADER][, EXPORT='file.csv'])",
               "Select table tags only"},
              {"output", "TO CSV", "TO CSV('file.csv')", "Export result"},
              {"output", "TO PARQUET", "TO PARQUET('file.parquet')", "Export result"},
              {"source", "document", "FROM document", "Active input in REPL"},
              {"source", "alias", "FROM doc", "Alias for document"},
              {"source", "path", "FROM 'file.html'", "Local file"},
              {"source", "url", "FROM 'https://example.com'", "Requires libcurl"},
              {"source", "raw", "FROM RAW('<html>')", "Inline HTML"},
              {"source", "fragments", "FROM FRAGMENTS(<raw|subquery>)",
               "Concatenate HTML fragments"},
              {"source", "fragments_raw", "FRAGMENTS(RAW('<ul>...</ul>'))", "Raw fragment input"},
              {"source", "fragments_query",
               "FRAGMENTS(SELECT inner_html(...) FROM doc)", "Subquery returns HTML strings"},
              {"field", "node_id", "node_id", "int64"},
              {"field", "tag", "tag", "lowercase"},
              {"field", "attributes", "attributes", "map<string,string>"},
              {"field", "parent_id", "parent_id", "int64 or null"},
              {"field", "sibling_pos", "sibling_pos", "1-based among siblings"},
              {"field", "source_uri", "source_uri", "Hidden unless multi-source"},
              {"function", "text", "text(tag)", "Direct text content; requires WHERE"},
              {"function", "inner_html", "inner_html(tag[, depth])",
               "Inner HTML; requires WHERE"},
              {"function", "trim", "trim(text(...)) | trim(inner_html(...))",
               "Trim whitespace"},
              {"aggregate", "count", "count(tag|*)", "int64"},
              {"aggregate", "summarize", "summarize(*)", "tag counts table"},
              {"aggregate", "tfidf", "tfidf(tag|*)", "map<string,double>"},
              {"axis", "parent", "parent.<field>", "Direct parent"},
              {"axis", "child", "child.<field>", "Direct child"},
              {"axis", "ancestor", "ancestor.<field>", "Any ancestor"},
              {"axis", "descendant", "descendant.<field>", "Any descendant"},
              {"operator", "=", "lhs = rhs", "Equality"},
              {"operator", "<>", "lhs <> rhs", "Not equal"},
              {"operator", "IN", "lhs IN ('a','b')", "Membership"},
              {"operator", "CONTAINS", "lhs CONTAINS 'x'", "Substring or list contains"},
              {"operator", "CONTAINS ALL", "lhs CONTAINS ALL ('a','b')", "All values"},
              {"operator", "CONTAINS ANY", "lhs CONTAINS ANY ('a','b')", "Any value"},
              {"operator", "IS NULL", "lhs IS NULL", "Null check"},
              {"operator", "IS NOT NULL", "lhs IS NOT NULL", "Not-null check"},
              {"operator", "HAS_DIRECT_TEXT", "HAS_DIRECT_TEXT", "Predicate on direct text"},
              {"operator", "~", "lhs ~ 're'", "Regex match"},
              {"operator", "AND", "expr AND expr", "Logical AND"},
              {"operator", "OR", "expr OR expr", "Logical OR"},
              {"meta", "SHOW INPUT", "SHOW INPUT", "Active source"},
              {"meta", "SHOW INPUTS", "SHOW INPUTS", "Last result sources or active"},
              {"meta", "SHOW FUNCTIONS", "SHOW FUNCTIONS", "Function list"},
              {"meta", "SHOW AXES", "SHOW AXES", "Axis list"},
              {"meta", "SHOW OPERATORS", "SHOW OPERATORS", "Operator list"},
              {"meta", "DESCRIBE doc", "DESCRIBE doc", "Document schema"},
              {"meta", "DESCRIBE language", "DESCRIBE language", "Language spec"},
          });
    }
    case Query::Kind::Select:
    default:
      return QueryResult{};
  }
}

void append_document(HtmlDocument& target, const HtmlDocument& source) {
  const int64_t offset = static_cast<int64_t>(target.nodes.size());
  target.nodes.reserve(target.nodes.size() + source.nodes.size());
  for (const auto& node : source.nodes) {
    HtmlNode copy = node;
    copy.id = node.id + offset;
    if (node.parent_id.has_value()) {
      copy.parent_id = *node.parent_id + offset;
    }
    target.nodes.push_back(std::move(copy));
  }
}

HtmlDocument build_fragments_document(const FragmentSource& fragments) {
  HtmlDocument merged;
  for (const auto& fragment : fragments.fragments) {
    HtmlDocument doc = parse_html(fragment);
    append_document(merged, doc);
  }
  return merged;
}

FragmentSource collect_fragments(const QueryResult& result) {
  if (result.to_table || !result.tables.empty()) {
    throw std::runtime_error("FRAGMENTS does not accept TO TABLE() results");
  }
  if (result.columns.size() != 1) {
    throw std::runtime_error("FRAGMENTS expects a single HTML string column");
  }
  const std::string& field = result.columns[0];
  FragmentSource out;
  size_t total_bytes = 0;
  for (const auto& row : result.rows) {
    std::optional<std::string> value = field_value_string(row, field);
    if (!value.has_value()) {
      throw std::runtime_error("FRAGMENTS expects HTML strings (use inner_html(...) or RAW('<...>'))");
    }
    std::string trimmed = util::trim_ws(*value);
    if (trimmed.empty()) {
      continue;
    }
    if (!looks_like_html_fragment(trimmed)) {
      throw std::runtime_error("FRAGMENTS expects HTML strings (use inner_html(...) or RAW('<...>'))");
    }
    if (trimmed.size() > xsql_internal::kMaxFragmentHtmlBytes) {
      throw std::runtime_error("FRAGMENTS HTML fragment exceeds maximum size");
    }
    total_bytes += trimmed.size();
    if (out.fragments.size() >= xsql_internal::kMaxFragmentCount) {
      throw std::runtime_error("FRAGMENTS exceeds maximum fragment count");
    }
    if (total_bytes > xsql_internal::kMaxFragmentBytes) {
      throw std::runtime_error("FRAGMENTS exceeds maximum total HTML size");
    }
    out.fragments.push_back(std::move(trimmed));
  }
  if (out.fragments.empty()) {
    throw std::runtime_error("FRAGMENTS produced no HTML fragments");
  }
  return out;
}

QueryResult execute_query_ast(const Query& query, const HtmlDocument& doc, const std::string& source_uri) {
  ExecuteResult exec = execute_query(query, doc, source_uri);
  QueryResult out;
  out.columns = xsql_internal::build_columns(query);
  out.columns_implicit = !xsql_internal::is_projection_query(query);
  out.source_uri_excluded =
      std::find(query.exclude_fields.begin(), query.exclude_fields.end(), "source_uri") !=
      query.exclude_fields.end();
  out.to_list = query.to_list;
  out.to_table = query.to_table;
  out.table_has_header = query.table_has_header;
  if (query.export_sink.has_value()) {
    const auto& sink = *query.export_sink;
    if (sink.kind == Query::ExportSink::Kind::Csv) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Csv;
    } else if (sink.kind == Query::ExportSink::Kind::Parquet) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Parquet;
    }
    out.export_sink.path = sink.path;
  }
  if (query.export_sink.has_value() &&
      (query.to_table || xsql_internal::is_table_select(query)) &&
      exec.nodes.size() != 1) {
    throw std::runtime_error(
        "Export requires a single table result; add a filter to select one table");
  }
  if (!query.select_items.empty() &&
      query.select_items[0].aggregate == Query::SelectItem::Aggregate::Tfidf) {
    out.rows = xsql_internal::build_tfidf_rows(query, exec.nodes);
    return out;
  }
  if (!query.select_items.empty() &&
      query.select_items[0].aggregate == Query::SelectItem::Aggregate::Summarize) {
    std::unordered_map<std::string, size_t> counts;
    for (const auto& node : exec.nodes) {
      ++counts[node.tag];
    }
    std::vector<std::pair<std::string, size_t>> summary;
    summary.reserve(counts.size());
    for (const auto& kv : counts) {
      summary.emplace_back(kv.first, kv.second);
    }
    if (!query.order_by.empty()) {
      std::sort(summary.begin(), summary.end(),
                [&](const auto& a, const auto& b) {
                  for (const auto& order_by : query.order_by) {
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
    if (query.limit.has_value() && summary.size() > *query.limit) {
      summary.resize(*query.limit);
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
  if (query.to_table ||
      (query.export_sink.has_value() && xsql_internal::is_table_select(query))) {
    auto children = xsql_internal::build_children(doc);
    for (const auto& node : exec.nodes) {
      QueryResult::TableResult table;
      table.node_id = node.id;
      xsql_internal::collect_rows(doc, children, node.id, table.rows);
      out.tables.push_back(std::move(table));
    }
    return out;
  }
  for (const auto& item : query.select_items) {
    if (item.aggregate == Query::SelectItem::Aggregate::Count) {
      QueryResultRow row;
      row.node_id = static_cast<int64_t>(exec.nodes.size());
      row.source_uri = source_uri;
      out.rows.push_back(row);
      return out;
    }
  }
  auto inner_html_depth = xsql_internal::find_inner_html_depth(query);
  const Query::SelectItem* trim_item = xsql_internal::find_trim_item(query);
  bool use_text_function = false;
  bool use_inner_html_function = false;
  for (const auto& item : query.select_items) {
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
  auto children = xsql_internal::build_children(doc);
  std::vector<int64_t> sibling_positions(doc.nodes.size(), 1);
  for (size_t parent = 0; parent < children.size(); ++parent) {
    const auto& kids = children[parent];
    for (size_t idx = 0; idx < kids.size(); ++idx) {
      sibling_positions.at(static_cast<size_t>(kids[idx])) = static_cast<int64_t>(idx + 1);
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
    row.sibling_pos = sibling_positions.at(static_cast<size_t>(node.id));
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

void validate_query(const Query& query) {
  if (query.kind != Query::Kind::Select) {
    return;
  }
  xsql_internal::validate_projection(query);
  xsql_internal::validate_order_by(query);
  xsql_internal::validate_to_table(query);
  xsql_internal::validate_export_sink(query);
  xsql_internal::validate_qualifiers(query);
  xsql_internal::validate_predicates(query);
  xsql_internal::validate_limits(query);
}

QueryResult execute_query_with_source(const Query& query,
                                      const std::string& default_html,
                                      const std::string& default_source_uri) {
  std::string effective_source_uri = default_source_uri;
  if (query.source.kind == Source::Kind::RawHtml) {
    if (query.source.value.size() > xsql_internal::kMaxRawHtmlBytes) {
      throw std::runtime_error("RAW() HTML exceeds maximum size");
    }
    HtmlDocument doc = parse_html(query.source.value);
    effective_source_uri = "raw";
    return execute_query_ast(query, doc, effective_source_uri);
  }
  if (query.source.kind == Source::Kind::Fragments) {
    FragmentSource fragments;
    if (query.source.fragments_raw.has_value()) {
      if (query.source.fragments_raw->size() > xsql_internal::kMaxRawHtmlBytes) {
        throw std::runtime_error("FRAGMENTS RAW() input exceeds maximum size");
      }
      fragments.fragments.push_back(*query.source.fragments_raw);
    } else if (query.source.fragments_query != nullptr) {
      const Query& subquery = *query.source.fragments_query;
      validate_query(subquery);
      if (subquery.source.kind == Source::Kind::Path || subquery.source.kind == Source::Kind::Url) {
        throw std::runtime_error("FRAGMENTS subquery cannot use URL or file sources");
      }
      QueryResult sub_result = execute_query_with_source(subquery, default_html, default_source_uri);
      fragments = collect_fragments(sub_result);
    } else {
      throw std::runtime_error("FRAGMENTS requires a subquery or RAW('<...>') input");
    }
    HtmlDocument doc = build_fragments_document(fragments);
    effective_source_uri = "fragment";
    return execute_query_ast(query, doc, effective_source_uri);
  }
  HtmlDocument doc = parse_html(default_html);
  return execute_query_ast(query, doc, effective_source_uri);
}

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
  validate_query(*parsed.query);
  if (parsed.query->kind != Query::Kind::Select) {
    return execute_meta_query(*parsed.query, source_uri);
  }
  return execute_query_with_source(*parsed.query, html, source_uri);
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
