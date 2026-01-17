#include "test_harness.h"
#include "test_utils.h"

#include "cli_utils.h"
#include "query_parser.h"

namespace {

void test_parse_show_describe() {
  auto show_inputs = xsql::parse_query("SHOW INPUTS");
  expect_true(show_inputs.query.has_value(), "SHOW INPUTS parses");
  if (show_inputs.query.has_value()) {
    expect_true(show_inputs.query->kind == xsql::Query::Kind::ShowInputs,
                "SHOW INPUTS kind");
  }

  auto describe = xsql::parse_query("DESCRIBE doc");
  expect_true(describe.query.has_value(), "DESCRIBE doc parses");
  if (describe.query.has_value()) {
    expect_true(describe.query->kind == xsql::Query::Kind::DescribeDoc,
                "DESCRIBE doc kind");
  }

  auto describe_language = xsql::parse_query("DESCRIBE language");
  expect_true(describe_language.query.has_value(), "DESCRIBE language parses");
  if (describe_language.query.has_value()) {
    expect_true(describe_language.query->kind == xsql::Query::Kind::DescribeLanguage,
                "DESCRIBE language kind");
  }
}

void test_execute_describe_doc() {
  auto result = run_query("", "DESCRIBE doc");
  expect_eq(result.columns.size(), 4, "DESCRIBE doc column count");
  if (result.columns.size() == 4) {
    expect_true(result.columns[0] == "column_name", "DESCRIBE column_name");
    expect_true(result.columns[1] == "type", "DESCRIBE type");
    expect_true(result.columns[2] == "nullable", "DESCRIBE nullable");
    expect_true(result.columns[3] == "notes", "DESCRIBE notes");
  }
  expect_true(result.rows.size() >= 5, "DESCRIBE rows include core fields");
}

void test_show_functions_output() {
  auto result = run_query("", "SHOW FUNCTIONS");
  expect_true(!result.rows.empty(), "SHOW FUNCTIONS returns rows");
  bool saw_text = false;
  for (const auto& row : result.rows) {
    auto it = row.attributes.find("function");
    if (it != row.attributes.end() && it->second == "text(tag)") {
      saw_text = true;
      break;
    }
  }
  expect_true(saw_text, "SHOW FUNCTIONS lists text(tag)");
}

void test_describe_language_output() {
  auto result = run_query("", "DESCRIBE language");
  expect_eq(result.columns.size(), 4, "DESCRIBE language column count");
  bool saw_select = false;
  for (const auto& row : result.rows) {
    auto cat = row.attributes.find("category");
    auto name = row.attributes.find("name");
    if (cat != row.attributes.end() && name != row.attributes.end() &&
        cat->second == "clause" && name->second == "SELECT") {
      saw_select = true;
      break;
    }
  }
  expect_true(saw_select, "DESCRIBE language lists SELECT clause");
}

void test_show_input_requires_source() {
  xsql::QueryResult result;
  std::string error;
  bool ok = xsql::cli::build_show_input_result("", result, error);
  expect_true(!ok, "SHOW INPUT requires an active source");
  expect_true(!error.empty(), "SHOW INPUT provides an error message");
}

void test_show_inputs_requires_source() {
  xsql::QueryResult result;
  std::string error;
  bool ok = xsql::cli::build_show_inputs_result({}, "", result, error);
  expect_true(!ok, "SHOW INPUTS requires sources");
  expect_true(!error.empty(), "SHOW INPUTS provides an error message");
}

void test_show_inputs_fallback_source() {
  xsql::QueryResult result;
  std::string error;
  bool ok = xsql::cli::build_show_inputs_result({}, "doc", result, error);
  expect_true(ok, "SHOW INPUTS falls back to the active source");
  expect_eq(result.rows.size(), 1, "SHOW INPUTS fallback row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].source_uri == "doc", "SHOW INPUTS fallback value");
  }
}

void test_auto_include_source_uri() {
  xsql::QueryResult result;
  result.columns = {"node_id", "tag", "attributes", "parent_id"};
  result.columns_implicit = true;
  result.source_uri_excluded = false;
  xsql::QueryResultRow row_a;
  row_a.source_uri = "a.html";
  result.rows.push_back(row_a);
  xsql::QueryResultRow row_b;
  row_b.source_uri = "b.html";
  result.rows.push_back(row_b);

  auto sources = xsql::cli::collect_source_uris(result);
  xsql::cli::apply_source_uri_policy(result, sources);

  expect_true(!result.columns.empty(), "auto include columns not empty");
  if (!result.columns.empty()) {
    expect_true(result.columns[0] == "source_uri", "auto include source_uri column");
  }

  xsql::QueryResult explicit_result;
  explicit_result.columns = {"node_id"};
  explicit_result.columns_implicit = false;
  explicit_result.rows = result.rows;
  auto explicit_sources = xsql::cli::collect_source_uris(explicit_result);
  xsql::cli::apply_source_uri_policy(explicit_result, explicit_sources);
  expect_true(explicit_result.columns.size() == 1, "explicit columns stay unchanged");
}

}  // namespace

void register_meta_command_tests(std::vector<TestCase>& tests) {
  tests.push_back({"parse_show_describe", test_parse_show_describe});
  tests.push_back({"execute_describe_doc", test_execute_describe_doc});
  tests.push_back({"show_functions_output", test_show_functions_output});
  tests.push_back({"describe_language_output", test_describe_language_output});
  tests.push_back({"show_input_requires_source", test_show_input_requires_source});
  tests.push_back({"show_inputs_requires_source", test_show_inputs_requires_source});
  tests.push_back({"show_inputs_fallback_source", test_show_inputs_fallback_source});
  tests.push_back({"auto_include_source_uri", test_auto_include_source_uri});
}
