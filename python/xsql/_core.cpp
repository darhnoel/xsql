#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "xsql/xsql.h"

namespace py = pybind11;

namespace {

py::dict attributes_to_dict(const std::unordered_map<std::string, std::string>& attrs) {
  py::dict out;
  for (const auto& kv : attrs) {
    out[py::str(kv.first)] = py::str(kv.second);
  }
  return out;
}

py::object field_value(const xsql::QueryResultRow& row, const std::string& field) {
  if (field == "node_id") return py::int_(row.node_id);
  if (field == "count") return py::int_(row.node_id);
  if (field == "tag") return py::str(row.tag);
  if (field == "text") return py::str(row.text);
  if (field == "inner_html") return py::str(row.inner_html);
  if (field == "parent_id") {
    if (row.parent_id.has_value()) return py::int_(*row.parent_id);
    return py::none();
  }
  if (field == "sibling_pos") return py::int_(row.sibling_pos);
  if (field == "source_uri") return py::str(row.source_uri);
  if (field == "attributes") return attributes_to_dict(row.attributes);
  auto it = row.attributes.find(field);
  if (it == row.attributes.end()) return py::none();
  return py::str(it->second);
}

py::dict row_to_dict(const xsql::QueryResultRow& row, const std::vector<std::string>& columns) {
  py::dict out;
  for (const auto& col : columns) {
    out[py::str(col)] = field_value(row, col);
  }
  return out;
}

}  // namespace

PYBIND11_MODULE(_core, m) {
  m.doc() = "Native bindings for XSQL query execution.";

  m.def("execute_from_document",
        [](const std::string& html, const std::string& query) {
          xsql::QueryResult result = xsql::execute_query_from_document(html, query);
          py::dict out;
          out["columns"] = result.columns;
          py::list rows;
          for (const auto& row : result.rows) {
            rows.append(row_to_dict(row, result.columns));
          }
          out["rows"] = rows;
          py::list tables;
          for (const auto& table : result.tables) {
            py::dict table_obj;
            table_obj["node_id"] = table.node_id;
            table_obj["rows"] = table.rows;
            tables.append(table_obj);
          }
          out["tables"] = tables;
          out["to_list"] = result.to_list;
          out["to_table"] = result.to_table;
          py::dict export_sink;
          switch (result.export_sink.kind) {
            case xsql::QueryResult::ExportSink::Kind::Csv:
              export_sink["kind"] = "csv";
              break;
            case xsql::QueryResult::ExportSink::Kind::Parquet:
              export_sink["kind"] = "parquet";
              break;
            default:
              export_sink["kind"] = "none";
              break;
          }
          export_sink["path"] = result.export_sink.path;
          out["export_sink"] = export_sink;
          return out;
        },
        py::arg("html"),
        py::arg("query"));
}
