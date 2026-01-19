#include "test_utils.h"

#include <fstream>
#include <sstream>

xsql::QueryResult run_query(const std::string& html, const std::string& query) {
  return xsql::execute_query_from_document(html, query);
}

xsql::QueryResult make_result(const std::vector<std::string>& columns,
                              const std::vector<std::vector<std::string>>& values) {
  xsql::QueryResult result;
  result.columns = columns;
  for (const auto& row_values : values) {
    xsql::QueryResultRow row;
    for (size_t i = 0; i < columns.size() && i < row_values.size(); ++i) {
      const auto& col = columns[i];
      const auto& value = row_values[i];
      if (col == "node_id") {
        row.node_id = std::stoll(value);
      } else if (col == "tag") {
        row.tag = value;
      } else if (col == "text") {
        row.text = value;
      } else if (col == "inner_html") {
        row.inner_html = value;
      } else if (col == "parent_id") {
        if (value == "NULL") {
          row.parent_id.reset();
        } else {
          row.parent_id = std::stoll(value);
        }
      } else if (col == "max_depth") {
        row.max_depth = std::stoll(value);
      } else if (col == "doc_order") {
        row.doc_order = std::stoll(value);
      } else if (col == "source_uri") {
        row.source_uri = value;
      } else if (col == "attributes") {
        row.attributes["value"] = value;
      } else {
        row.attributes[col] = value;
      }
    }
    result.rows.push_back(row);
  }
  return result;
}

std::string read_file_to_string(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}
