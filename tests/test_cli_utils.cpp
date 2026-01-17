#include "test_harness.h"

#include "cli_utils.h"

namespace {

void test_count_table_rows_header() {
  xsql::QueryResult::TableResult table;
  table.rows = {
      {"H1", "H2"},
      {"a", "b"},
      {"c", "d"},
  };
  size_t count = xsql::cli::count_table_rows(table, true);
  expect_eq(count, 2, "count_table_rows excludes header");
}

void test_count_table_rows_no_header() {
  xsql::QueryResult::TableResult table;
  table.rows = {
      {"a", "b"},
      {"c", "d"},
  };
  size_t count = xsql::cli::count_table_rows(table, false);
  expect_eq(count, 2, "count_table_rows includes all rows");
}

void test_count_result_rows() {
  xsql::QueryResult result;
  result.rows.resize(3);
  size_t count = xsql::cli::count_result_rows(result);
  expect_eq(count, 3, "count_result_rows returns row count");
}

}  // namespace

void register_cli_utils_tests(std::vector<TestCase>& tests) {
  tests.push_back({"count_table_rows_header", test_count_table_rows_header});
  tests.push_back({"count_table_rows_no_header", test_count_table_rows_no_header});
  tests.push_back({"count_result_rows", test_count_result_rows});
}
