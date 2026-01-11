#include "export/export_sinks.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifdef XSQL_USE_ARROW
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/writer.h>
#endif

namespace xsql::cli {

namespace {

std::string attributes_to_string(const std::unordered_map<std::string, std::string>& attrs) {
  if (attrs.empty()) return "{}";
  std::vector<std::string> keys;
  keys.reserve(attrs.size());
  for (const auto& kv : attrs) {
    keys.push_back(kv.first);
  }
  std::sort(keys.begin(), keys.end());
  std::ostringstream oss;
  oss << "{";
  for (size_t i = 0; i < keys.size(); ++i) {
    if (i > 0) oss << ",";
    const auto& key = keys[i];
    oss << key << "=" << attrs.at(key);
  }
  oss << "}";
  return oss.str();
}

struct CellValue {
  std::string value;
  bool is_null = false;
};

CellValue field_value(const xsql::QueryResultRow& row, const std::string& field) {
  if (field == "node_id") return {std::to_string(row.node_id), false};
  if (field == "count") return {std::to_string(row.node_id), false};
  if (field == "tag") return {row.tag, false};
  if (field == "text") return {row.text, false};
  if (field == "inner_html") return {row.inner_html, false};
  if (field == "parent_id") {
    if (!row.parent_id.has_value()) return {"", true};
    return {std::to_string(*row.parent_id), false};
  }
  if (field == "source_uri") return {row.source_uri, false};
  if (field == "attributes") return {attributes_to_string(row.attributes), false};
  auto it = row.attributes.find(field);
  if (it == row.attributes.end()) return {"", true};
  return {it->second, false};
}

std::string csv_escape(const std::string& value) {
  bool needs_quotes = false;
  for (char c : value) {
    if (c == ',' || c == '"' || c == '\n' || c == '\r') {
      needs_quotes = true;
      break;
    }
  }
  if (!needs_quotes) return value;
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('"');
  for (char c : value) {
    if (c == '"') {
      out.push_back('"');
      out.push_back('"');
    } else {
      out.push_back(c);
    }
  }
  out.push_back('"');
  return out;
}

bool validate_rectangular(const xsql::QueryResult& result, std::string& error) {
  if (result.to_table || !result.tables.empty()) {
    error = "TO CSV/PARQUET does not support TO TABLE() results";
    return false;
  }
  if (result.columns.empty()) {
    error = "Export requires a rectangular result with columns";
    return false;
  }
  return true;
}

}  // namespace

static std::vector<std::string> table_columns(const xsql::QueryResult::TableResult& table);

bool write_csv(const xsql::QueryResult& result, const std::string& path, std::string& error) {
  if (!validate_rectangular(result, error)) return false;
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = "Failed to open file for writing: " + path;
    return false;
  }
  for (size_t i = 0; i < result.columns.size(); ++i) {
    if (i > 0) out << ",";
    out << csv_escape(result.columns[i]);
  }
  out << "\n";
  for (const auto& row : result.rows) {
    for (size_t i = 0; i < result.columns.size(); ++i) {
      if (i > 0) out << ",";
      CellValue cell = field_value(row, result.columns[i]);
      const std::string& value = cell.is_null ? "" : cell.value;
      out << csv_escape(value);
    }
    out << "\n";
  }
  return true;
}

bool write_table_csv(const xsql::QueryResult::TableResult& table,
                     const std::string& path,
                     std::string& error,
                     bool table_has_header) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = "Failed to open file for writing: " + path;
    return false;
  }
  if (!table_has_header) {
    std::vector<std::string> cols = table_columns(table);
    if (!cols.empty()) {
      for (size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) out << ",";
        out << csv_escape(cols[i]);
      }
      out << "\n";
    }
  }
  for (const auto& row : table.rows) {
    for (size_t i = 0; i < row.size(); ++i) {
      if (i > 0) out << ",";
      out << csv_escape(row[i]);
    }
    out << "\n";
  }
  return true;
}

static std::vector<std::string> table_columns(const xsql::QueryResult::TableResult& table) {
  size_t max_cols = 0;
  for (const auto& row : table.rows) {
    if (row.size() > max_cols) {
      max_cols = row.size();
    }
  }
  std::vector<std::string> cols;
  cols.reserve(max_cols);
  for (size_t i = 0; i < max_cols; ++i) {
    cols.push_back("col" + std::to_string(i + 1));
  }
  return cols;
}

bool write_parquet(const xsql::QueryResult& result, const std::string& path, std::string& error) {
  if (!validate_rectangular(result, error)) return false;
#ifdef XSQL_USE_ARROW
  std::vector<std::shared_ptr<arrow::ArrayBuilder>> builders;
  builders.reserve(result.columns.size());
  for (size_t i = 0; i < result.columns.size(); ++i) {
    builders.push_back(std::make_shared<arrow::StringBuilder>());
  }
  for (const auto& row : result.rows) {
    for (size_t i = 0; i < result.columns.size(); ++i) {
      CellValue cell = field_value(row, result.columns[i]);
      auto builder = std::static_pointer_cast<arrow::StringBuilder>(builders[i]);
      if (cell.is_null) {
        auto st = builder->AppendNull();
        if (!st.ok()) {
          error = st.ToString();
          return false;
        }
      } else {
        auto st = builder->Append(cell.value);
        if (!st.ok()) {
          error = st.ToString();
          return false;
        }
      }
    }
  }
  std::vector<std::shared_ptr<arrow::Field>> fields;
  std::vector<std::shared_ptr<arrow::Array>> arrays;
  fields.reserve(result.columns.size());
  arrays.reserve(result.columns.size());
  for (size_t i = 0; i < result.columns.size(); ++i) {
    fields.push_back(arrow::field(result.columns[i], arrow::utf8(), true));
    auto builder = std::static_pointer_cast<arrow::StringBuilder>(builders[i]);
    std::shared_ptr<arrow::Array> array;
    auto st = builder->Finish(&array);
    if (!st.ok()) {
      error = st.ToString();
      return false;
    }
    arrays.push_back(array);
  }
  auto schema = arrow::schema(fields);
  auto table = arrow::Table::Make(schema, arrays);
  auto output_res = arrow::io::FileOutputStream::Open(path);
  if (!output_res.ok()) {
    error = output_res.status().ToString();
    return false;
  }
  auto output = *output_res;
  auto st = parquet::arrow::WriteTable(*table, arrow::default_memory_pool(), output, 1024);
  if (!st.ok()) {
    error = st.ToString();
    return false;
  }
  return true;
#else
  (void)result;
  (void)path;
  error = "TO PARQUET requires Apache Arrow feature";
  return false;
#endif
}

bool write_table_parquet(const xsql::QueryResult::TableResult& table,
                         const std::string& path,
                         std::string& error) {
#ifdef XSQL_USE_ARROW
  std::vector<std::string> cols = table_columns(table);
  if (cols.empty()) {
    error = "Table export has no rows";
    return false;
  }
  std::vector<std::shared_ptr<arrow::ArrayBuilder>> builders;
  builders.reserve(cols.size());
  for (size_t i = 0; i < cols.size(); ++i) {
    builders.push_back(std::make_shared<arrow::StringBuilder>());
  }
  for (const auto& row : table.rows) {
    for (size_t i = 0; i < cols.size(); ++i) {
      auto builder = std::static_pointer_cast<arrow::StringBuilder>(builders[i]);
      if (i < row.size()) {
        auto st = builder->Append(row[i]);
        if (!st.ok()) {
          error = st.ToString();
          return false;
        }
      } else {
        auto st = builder->AppendNull();
        if (!st.ok()) {
          error = st.ToString();
          return false;
        }
      }
    }
  }
  std::vector<std::shared_ptr<arrow::Field>> fields;
  std::vector<std::shared_ptr<arrow::Array>> arrays;
  fields.reserve(cols.size());
  arrays.reserve(cols.size());
  for (size_t i = 0; i < cols.size(); ++i) {
    fields.push_back(arrow::field(cols[i], arrow::utf8(), true));
    auto builder = std::static_pointer_cast<arrow::StringBuilder>(builders[i]);
    std::shared_ptr<arrow::Array> array;
    auto st = builder->Finish(&array);
    if (!st.ok()) {
      error = st.ToString();
      return false;
    }
    arrays.push_back(array);
  }
  auto schema = arrow::schema(fields);
  auto table_out = arrow::Table::Make(schema, arrays);
  auto output_res = arrow::io::FileOutputStream::Open(path);
  if (!output_res.ok()) {
    error = output_res.status().ToString();
    return false;
  }
  auto output = *output_res;
  auto st = parquet::arrow::WriteTable(*table_out, arrow::default_memory_pool(), output, 1024);
  if (!st.ok()) {
    error = st.ToString();
    return false;
  }
  return true;
#else
  (void)table;
  (void)path;
  error = "TO PARQUET requires Apache Arrow feature";
  return false;
#endif
}

bool export_result(const xsql::QueryResult& result, std::string& error) {
  if (result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::None) {
    return false;
  }
  if (!result.tables.empty()) {
    if (result.tables.size() != 1) {
      error = "Export requires a single table result; add a filter to select one table";
      return false;
    }
    if (result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::Csv) {
      return write_table_csv(result.tables[0], result.export_sink.path, error,
                             result.table_has_header);
    }
    if (result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::Parquet) {
      return write_table_parquet(result.tables[0], result.export_sink.path, error);
    }
  }
  if (result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::Csv) {
    return write_csv(result, result.export_sink.path, error);
  }
  if (result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::Parquet) {
    return write_parquet(result, result.export_sink.path, error);
  }
  error = "Unknown export sink";
  return false;
}

}  // namespace xsql::cli
