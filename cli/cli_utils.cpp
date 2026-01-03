#include "cli_utils.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "query_parser.h"
#include "render/duckbox_renderer.h"
#include "ui/color.h"

#ifdef XSQL_USE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#endif
#ifdef XSQL_USE_CURL
#include <curl/curl.h>
#endif

namespace xsql::cli {

namespace {

/// Escapes JSON string content so output remains valid JSON.
/// MUST preserve Unicode bytes and MUST escape control characters.
/// Inputs are raw strings; outputs are escaped strings with no side effects.
std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 4);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

#ifndef XSQL_USE_NLOHMANN_JSON
/// Serializes attributes without nlohmann/json to keep a minimal dependency set.
/// MUST escape keys/values and MUST emit a stable object shape.
/// Inputs are QueryResultRow attributes; outputs are JSON object text.
std::string attributes_to_json(const xsql::QueryResultRow& row) {
  std::string out = "{";
  size_t count = 0;
  for (const auto& kv : row.attributes) {
    if (count++ > 0) out += ",";
    out += "\"";
    out += json_escape(kv.first);
    out += "\":\"";
    out += json_escape(kv.second);
    out += "\"";
  }
  out += "}";
  return out;
}
#endif

/// Prints a single field as JSON, handling NULLs and attribute projections.
/// MUST keep output valid JSON and MUST follow QueryResult column semantics.
/// Inputs are the stream/field/row; side effects are stream writes.
void print_field(std::ostream& os, const std::string& field, const xsql::QueryResultRow& row) {
  if (field == "node_id") {
    os << row.node_id;
  } else if (field == "count") {
    os << row.node_id;
  } else if (field == "tag") {
    os << "\"" << json_escape(row.tag) << "\"";
  } else if (field == "text") {
    os << "\"" << json_escape(row.text) << "\"";
  } else if (field == "inner_html") {
    os << "\"" << json_escape(row.inner_html) << "\"";
  } else if (field == "parent_id") {
    if (row.parent_id.has_value()) {
      os << *row.parent_id;
    } else {
      os << "null";
    }
  } else if (field == "source_uri") {
    os << "\"" << json_escape(row.source_uri) << "\"";
  } else if (field == "attributes") {
#ifndef XSQL_USE_NLOHMANN_JSON
    os << attributes_to_json(row);
#else
    os << "null";
#endif
  } else {
    auto it = row.attributes.find(field);
    if (it != row.attributes.end()) {
      os << "\"" << json_escape(it->second) << "\"";
    } else {
      os << "null";
    }
  }
}

#ifdef XSQL_USE_CURL
/// Appends curl response chunks into the caller-owned buffer.
/// MUST return the full byte count or curl will treat it as an error.
/// Inputs are raw buffer pointers; side effects include buffer writes.
size_t write_to_string(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total = size * nmemb;
  auto* out = static_cast<std::string*>(userp);
  out->append(static_cast<const char*>(contents), total);
  return total;
}
#endif

}  // namespace

std::string read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + path);
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string read_stdin() {
  std::ostringstream buffer;
  buffer << std::cin.rdbuf();
  return buffer.str();
}

std::string trim_semicolon(std::string value) {
  while (!value.empty() && (value.back() == ';' || std::isspace(static_cast<unsigned char>(value.back())))) {
    value.pop_back();
  }
  return value;
}

std::string colorize_json(const std::string& input, bool enable) {
  if (!enable) return input;
  std::string out;
  out.reserve(input.size() * 2);
  bool in_string = false;
  bool escape = false;
  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    if (in_string) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        in_string = false;
        out += '"';
        out += kColor.reset;
        continue;
      }
      out += c;
      continue;
    }

    if (c == '"') {
      in_string = true;
      out += kColor.green;
      out += '"';
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
      out += kColor.cyan;
      while (i < input.size() &&
             (std::isdigit(static_cast<unsigned char>(input[i])) || input[i] == '.' || input[i] == '-' ||
              input[i] == 'e' || input[i] == 'E' || input[i] == '+')) {
        out += input[i++];
      }
      --i;
      out += kColor.reset;
      continue;
    }

    if (input.compare(i, 4, "true") == 0 || input.compare(i, 5, "false") == 0) {
      size_t len = input.compare(i, 4, "true") == 0 ? 4 : 5;
      out += kColor.yellow;
      out.append(input, i, len);
      out += kColor.reset;
      i += len - 1;
      continue;
    }

    if (input.compare(i, 4, "null") == 0) {
      out += kColor.magenta;
      out.append(input, i, 4);
      out += kColor.reset;
      i += 3;
      continue;
    }

    if (c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',') {
      out += kColor.dim;
      out += c;
      out += kColor.reset;
      continue;
    }

    out += c;
  }
  return out;
}

TruncateResult truncate_output(const std::string& text, size_t head_lines, size_t tail_lines) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }

  if (lines.size() <= head_lines + tail_lines) {
    // WHY: avoid truncation when the output already fits within the window.
    return {text, false};
  }

  std::ostringstream oss;
  for (size_t i = 0; i < head_lines; ++i) {
    oss << lines[i] << "\n";
  }
  oss << "... (abbreviated; use .display_mode more to show all or redirect to a file) ...\n";
  for (size_t i = lines.size() - tail_lines; i < lines.size(); ++i) {
    oss << lines[i];
    if (i + 1 < lines.size()) {
      oss << "\n";
    }
  }
  return {oss.str(), true};
}

bool is_url(const std::string& value) {
  return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

std::string load_html_input(const std::string& input, int timeout_ms) {
  if (is_url(input)) {
#ifdef XSQL_USE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
      throw std::runtime_error("Failed to initialize curl");
    }
    std::string buffer;
    curl_easy_setopt(curl, CURLOPT_URL, input.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "xsql/0.1");
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
      throw std::runtime_error(std::string("Failed to fetch URL: ") + curl_easy_strerror(res));
    }
    return buffer;
#else
    (void)timeout_ms;
    throw std::runtime_error("URL fetching is disabled (libcurl not available)");
#endif
  }
  return read_file(input);
}

std::string rewrite_from_path_if_needed(const std::string& query) {
  std::string lower = query;
  for (char& c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  size_t pos = 0;
  while (true) {
    pos = lower.find("from", pos);
    if (pos == std::string::npos) return query;
    bool left_ok = (pos == 0) || std::isspace(static_cast<unsigned char>(lower[pos - 1]));
    bool right_ok = (pos + 4 >= lower.size()) || std::isspace(static_cast<unsigned char>(lower[pos + 4]));
    if (!left_ok || !right_ok) {
      pos += 4;
      continue;
    }
    size_t i = pos + 4;
    while (i < query.size() && std::isspace(static_cast<unsigned char>(query[i]))) {
      ++i;
    }
    if (i >= query.size()) return query;
    char first = query[i];
    if (first == '\'' || first == '"') return query;
    size_t start = i;
    while (i < query.size() && !std::isspace(static_cast<unsigned char>(query[i])) && query[i] != ';') {
      ++i;
    }
    if (start == i) return query;
    std::string token = query.substr(start, i - start);
    bool looks_like_path = token.find('/') != std::string::npos ||
                           token.find('.') != std::string::npos ||
                           (!token.empty() && (token[0] == '.' || token[0] == '~'));
    if (!looks_like_path) return query;
    std::string rewritten = query.substr(0, start);
    rewritten.push_back('\'');
    rewritten += token;
    rewritten.push_back('\'');
    rewritten += query.substr(i);
    return rewritten;
  }
}

std::string sanitize_pasted_line(std::string line) {
  const std::string prompt = "xsql> ";
  size_t pos = line.rfind(prompt);
  if (pos != std::string::npos) {
    line = line.substr(pos + prompt.size());
  }
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
    line.erase(line.begin());
  }
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
    line.pop_back();
  }
  return line;
}

std::optional<QuerySource> parse_query_source(const std::string& query) {
  std::string cleaned = trim_semicolon(query);
  auto parsed = xsql::parse_query(cleaned);
  if (!parsed.query.has_value()) return std::nullopt;
  QuerySource source;
  source.kind = parsed.query->source.kind;
  source.value = parsed.query->source.value;
  return source;
}

std::string build_json(const xsql::QueryResult& result) {
#ifdef XSQL_USE_NLOHMANN_JSON
  using nlohmann::json;
  std::vector<std::string> columns = result.columns;
  if (columns.empty()) {
    columns = {"node_id", "tag", "attributes", "parent_id", "source_uri"};
  }
  json out = json::array();
  for (const auto& row : result.rows) {
    json obj = json::object();
    for (const auto& field : columns) {
      if (field == "node_id") {
        obj[field] = row.node_id;
      } else if (field == "count") {
        obj[field] = row.node_id;
      } else if (field == "tag") {
        obj[field] = row.tag;
      } else if (field == "text") {
        obj[field] = row.text;
      } else if (field == "inner_html") {
        obj[field] = row.inner_html;
      } else if (field == "parent_id") {
        obj[field] = row.parent_id.has_value() ? json(*row.parent_id) : json(nullptr);
      } else if (field == "source_uri") {
        obj[field] = row.source_uri;
      } else if (field == "attributes") {
        json attrs = json::object();
        for (const auto& kv : row.attributes) {
          attrs[kv.first] = kv.second;
        }
        obj[field] = attrs;
      } else {
        auto it = row.attributes.find(field);
        obj[field] = (it != row.attributes.end()) ? json(it->second) : json(nullptr);
      }
    }
    out.push_back(obj);
  }
  return out.dump(2);
#else
  std::vector<std::string> columns = result.columns;
  if (columns.empty()) {
    columns = {"node_id", "tag", "attributes", "parent_id", "source_uri"};
  }
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < result.rows.size(); ++i) {
    const auto& row = result.rows[i];
    if (i > 0) oss << ",";
    oss << "{";
    for (size_t c = 0; c < columns.size(); ++c) {
      if (c > 0) oss << ",";
      const auto& field = columns[c];
      oss << "\"" << json_escape(field) << "\":";
      print_field(oss, field, row);
    }
    oss << "}";
  }
  oss << "]";
  return oss.str();
#endif
}

std::string build_json_list(const xsql::QueryResult& result) {
#ifdef XSQL_USE_NLOHMANN_JSON
  using nlohmann::json;
  std::vector<std::string> columns = result.columns;
  if (columns.size() != 1) {
    throw std::runtime_error("TO LIST() requires a single projected column");
  }
  const std::string& field = columns[0];
  json out = json::array();
  for (const auto& row : result.rows) {
    if (field == "node_id") {
      out.push_back(row.node_id);
    } else if (field == "count") {
      out.push_back(row.node_id);
    } else if (field == "tag") {
      out.push_back(row.tag);
    } else if (field == "text") {
      out.push_back(row.text);
    } else if (field == "inner_html") {
      out.push_back(row.inner_html);
    } else if (field == "parent_id") {
      out.push_back(row.parent_id.has_value() ? json(*row.parent_id) : json(nullptr));
    } else if (field == "source_uri") {
      out.push_back(row.source_uri);
    } else if (field == "attributes") {
      json attrs = json::object();
      for (const auto& kv : row.attributes) {
        attrs[kv.first] = kv.second;
      }
      out.push_back(attrs);
    } else {
      auto it = row.attributes.find(field);
      out.push_back((it != row.attributes.end()) ? json(it->second) : json(nullptr));
    }
  }
  return out.dump(2);
#else
  std::vector<std::string> columns = result.columns;
  if (columns.size() != 1) {
    throw std::runtime_error("TO LIST() requires a single projected column");
  }
  const std::string& field = columns[0];
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < result.rows.size(); ++i) {
    const auto& row = result.rows[i];
    if (i > 0) oss << ",";
    print_field(oss, field, row);
  }
  oss << "]";
  return oss.str();
#endif
}

std::string build_table_json(const xsql::QueryResult& result) {
#ifdef XSQL_USE_NLOHMANN_JSON
  using nlohmann::json;
  if (result.tables.size() == 1) {
    json rows = json::array();
    for (const auto& row : result.tables[0].rows) {
      rows.push_back(row);
    }
    return rows.dump(2);
  }
  json out = json::array();
  for (const auto& table : result.tables) {
    json rows = json::array();
    for (const auto& row : table.rows) {
      rows.push_back(row);
    }
    out.push_back({{"node_id", table.node_id}, {"rows", rows}});
  }
  return out.dump(2);
#else
  std::ostringstream oss;
  if (result.tables.size() == 1) {
    oss << "[";
    const auto& rows = result.tables[0].rows;
    for (size_t i = 0; i < rows.size(); ++i) {
      if (i > 0) oss << ",";
      oss << "[";
      for (size_t j = 0; j < rows[i].size(); ++j) {
        if (j > 0) oss << ",";
        oss << "\"" << json_escape(rows[i][j]) << "\"";
      }
      oss << "]";
    }
    oss << "]";
    return oss.str();
  }
  oss << "[";
  for (size_t i = 0; i < result.tables.size(); ++i) {
    if (i > 0) oss << ",";
    oss << "{\"node_id\":" << result.tables[i].node_id << ",\"rows\":[";
    const auto& rows = result.tables[i].rows;
    for (size_t r = 0; r < rows.size(); ++r) {
      if (r > 0) oss << ",";
      oss << "[";
      for (size_t c = 0; c < rows[r].size(); ++c) {
        if (c > 0) oss << ",";
        oss << "\"" << json_escape(rows[r][c]) << "\"";
      }
      oss << "]";
    }
    oss << "]}";
  }
  oss << "]";
  return oss.str();
#endif
}

std::string build_summary_json(const std::vector<std::pair<std::string, size_t>>& summary) {
#ifdef XSQL_USE_NLOHMANN_JSON
  using nlohmann::json;
  json out = json::array();
  for (const auto& item : summary) {
    out.push_back({{"tag", item.first}, {"count", item.second}});
  }
  return out.dump(2);
#else
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < summary.size(); ++i) {
    if (i > 0) oss << ",";
    oss << "{\"tag\":\"" << json_escape(summary[i].first) << "\",\"count\":" << summary[i].second << "}";
  }
  oss << "]";
  return oss.str();
#endif
}

std::string render_table_duckbox(const xsql::QueryResult::TableResult& table,
                                 bool highlight,
                                 bool is_tty,
                                 size_t max_rows) {
  size_t max_cols = 0;
  for (const auto& row : table.rows) {
    if (row.size() > max_cols) {
      max_cols = row.size();
    }
  }
  if (max_cols == 0) {
    return "(empty table)";
  }
  xsql::QueryResult view;
  size_t data_start = 0;
  std::vector<std::string> column_keys;
  column_keys.reserve(max_cols);
  if (!table.rows.empty()) {
    std::vector<std::string> headers = table.rows.front();
    data_start = 1;
    if (headers.size() < max_cols) {
      headers.resize(max_cols);
    }
    std::unordered_map<std::string, int> seen;
    for (size_t i = 0; i < max_cols; ++i) {
      std::string name = headers[i];
      if (name.empty()) {
        name = "col" + std::to_string(i + 1);
      }
      auto& count = seen[name];
      std::string key = name;
      if (count > 0) {
        key = name + "_" + std::to_string(count + 1);
      }
      ++count;
      column_keys.push_back(key);
    }
  } else {
    for (size_t i = 0; i < max_cols; ++i) {
      column_keys.push_back("col" + std::to_string(i + 1));
    }
  }
  view.columns = column_keys;
  for (size_t r = data_start; r < table.rows.size(); ++r) {
    const auto& row_values = table.rows[r];
    xsql::QueryResultRow row;
    size_t limit = std::min(row_values.size(), column_keys.size());
    for (size_t i = 0; i < limit; ++i) {
      row.attributes[column_keys[i]] = row_values[i];
    }
    view.rows.push_back(std::move(row));
  }
  xsql::render::DuckboxOptions options;
  options.max_width = 0;
  options.max_rows = max_rows;
  options.highlight = highlight;
  options.is_tty = is_tty;
  return xsql::render::render_duckbox(view, options);
}

std::string export_kind_label(xsql::QueryResult::ExportSink::Kind kind) {
  if (kind == xsql::QueryResult::ExportSink::Kind::Csv) return "CSV";
  if (kind == xsql::QueryResult::ExportSink::Kind::Parquet) return "Parquet";
  return "Export";
}

}  // namespace xsql::cli
