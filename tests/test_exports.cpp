#include "test_harness.h"
#include "test_utils.h"

#include <filesystem>

#include "export/export_sinks.h"

namespace {

void test_csv_escaping() {
  xsql::QueryResult result;
  result.columns = {"col1", "col2"};
  xsql::QueryResultRow row1;
  row1.attributes["col1"] = "a,b";
  row1.attributes["col2"] = "He said \"hi\"";
  result.rows.push_back(row1);
  xsql::QueryResultRow row2;
  row2.attributes["col1"] = "line1\nline2";
  row2.attributes["col2"] = "plain";
  result.rows.push_back(row2);

  auto path = std::filesystem::temp_directory_path() / "xsql_csv_escape_test.csv";
  std::string error;
  bool ok = xsql::cli::write_csv(result, path.string(), error);
  expect_true(ok, "csv escaping write ok");
  expect_true(error.empty(), "csv escaping no error");
  std::string content = read_file_to_string(path);
  std::filesystem::remove(path);
  std::string expected =
      "col1,col2\n"
      "\"a,b\",\"He said \"\"hi\"\"\"\n"
      "\"line1\nline2\",plain\n";
  expect_true(content == expected, "csv escaping content");
}

void test_csv_export_integration() {
  std::string html = "<a href='x'>Hi</a><a>Skip</a>";
  auto path = std::filesystem::temp_directory_path() / "xsql_csv_integration_test.csv";
  std::string query = "SELECT a.href, TEXT(a) FROM document WHERE attributes.href = 'x' TO CSV(\"" +
                      path.string() + "\")";
  auto result = run_query(html, query);
  std::string error;
  bool ok = xsql::cli::export_result(result, error);
  expect_true(ok, "csv export integration ok");
  expect_true(error.empty(), "csv export integration no error");
  std::string content = read_file_to_string(path);
  std::filesystem::remove(path);
  std::string expected =
      "href,text\n"
      "x,Hi\n";
  expect_true(content == expected, "csv export integration content");
}

void test_table_csv_export_integration() {
  std::string html =
      "<table>"
      "<tr><th>H1</th><th>H2</th></tr>"
      "<tr><td>A</td><td>B</td></tr>"
      "</table>";
  auto path = std::filesystem::temp_directory_path() / "xsql_table_csv_test.csv";
  std::string query = "SELECT table FROM document TO TABLE(EXPORT=\"" + path.string() + "\")";
  auto result = run_query(html, query);
  std::string error;
  bool ok = xsql::cli::export_result(result, error);
  expect_true(ok, "table csv export integration ok");
  expect_true(error.empty(), "table csv export integration no error");
  std::string content = read_file_to_string(path);
  std::filesystem::remove(path);
  std::string expected =
      "H1,H2\n"
      "A,B\n";
  expect_true(content == expected, "table csv export integration content");
}

void test_table_csv_export_header_off() {
  std::string html =
      "<table>"
      "<tr><td>A</td><td>B</td></tr>"
      "</table>";
  auto path = std::filesystem::temp_directory_path() / "xsql_table_csv_header_off.csv";
  std::string query =
      "SELECT table FROM document TO TABLE(EXPORT=\"" + path.string() + "\", HEADER=OFF)";
  auto result = run_query(html, query);
  std::string error;
  bool ok = xsql::cli::export_result(result, error);
  expect_true(ok, "table csv header off export ok");
  expect_true(error.empty(), "table csv header off export no error");
  std::string content = read_file_to_string(path);
  std::filesystem::remove(path);
  std::string expected =
      "col1,col2\n"
      "A,B\n";
  expect_true(content == expected, "table csv header off export content");
}

void test_table_export_requires_single_table() {
  bool threw = false;
  try {
    std::string html =
        "<table><tr><td>A</td></tr></table>"
        "<table><tr><td>B</td></tr></table>";
    auto path = std::filesystem::temp_directory_path() / "xsql_table_csv_multi.csv";
    std::string query = "SELECT table FROM document TO TABLE(EXPORT=\"" + path.string() + "\")";
    run_query(html, query);
  } catch (const std::exception& ex) {
    threw = true;
    std::string message = ex.what();
    expect_true(message.find("single table result") != std::string::npos,
                "table export requires single table message");
  }
  expect_true(threw, "table export requires single table");
}

#ifdef XSQL_USE_ARROW
void test_parquet_export_smoke() {
  std::string html = "<div id='x'>Hi</div>";
  auto path = std::filesystem::temp_directory_path() / "xsql_parquet_smoke.parquet";
  std::string query = "SELECT TEXT(div) FROM document WHERE attributes.id = 'x' TO PARQUET(\"" +
                      path.string() + "\")";
  auto result = run_query(html, query);
  std::string error;
  bool ok = xsql::cli::export_result(result, error);
  expect_true(ok, "parquet export smoke ok");
  expect_true(error.empty(), "parquet export smoke no error");
  expect_true(std::filesystem::exists(path), "parquet file created");
  if (std::filesystem::exists(path)) {
    expect_true(std::filesystem::file_size(path) > 0, "parquet file non-empty");
    std::filesystem::remove(path);
  }
}
#endif

}  // namespace

void register_export_tests(std::vector<TestCase>& tests) {
  tests.push_back({"csv_escaping", test_csv_escaping});
  tests.push_back({"csv_export_integration", test_csv_export_integration});
  tests.push_back({"table_csv_export_integration", test_table_csv_export_integration});
  tests.push_back({"table_csv_export_header_off", test_table_csv_export_header_off});
  tests.push_back({"table_export_requires_single_table", test_table_export_requires_single_table});
#ifdef XSQL_USE_ARROW
  tests.push_back({"parquet_export_smoke", test_parquet_export_smoke});
#endif
}
