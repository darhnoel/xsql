#include "test_harness.h"
#include "test_utils.h"

namespace {

void test_text_requires_non_tag_filter() {
  bool threw = false;
  try {
    std::string html = "<div></div>";
    run_query(html, "SELECT TEXT(div) FROM document WHERE tag = 'div'");
  } catch (const std::exception&) {
    threw = true;
  }
  expect_true(threw, "text requires non-tag filter");
}

void test_inner_html_function() {
  std::string html = "<div id='root'><span>Hi</span><em>There</em></div>";
  auto result = run_query(html, "SELECT inner_html(div) FROM document WHERE attributes.id = 'root'");
  expect_eq(result.columns.size(), 1, "inner_html projection has one column");
  if (!result.columns.empty()) {
    expect_true(result.columns[0] == "inner_html", "inner_html column name");
  }
  expect_eq(result.rows.size(), 1, "inner_html row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].inner_html == "<span>Hi</span><em>There</em>",
                "inner_html content");
  }
}

void test_inner_html_depth() {
  std::string html = "<div id='root'><span><b>Hi</b></span><em>There</em></div>";
  auto result = run_query(html, "SELECT inner_html(div, 1) FROM document WHERE attributes.id = 'root'");
  expect_eq(result.rows.size(), 1, "inner_html depth row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].inner_html == "<span>Hi</span><em>There</em>",
                "inner_html depth content");
  }
}

void test_trim_inner_html() {
  std::string html = "<li id='item'>\n  <a href=\"/x\">Link</a>\n</li>";
  auto result = run_query(html, "SELECT trim(inner_html(li)) FROM document WHERE attributes.id = 'item'");
  expect_eq(result.rows.size(), 1, "trim inner_html row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].inner_html == "<a href=\"/x\">Link</a>",
                "trim inner_html content");
  }
}

void test_count_aggregate() {
  std::string html = "<div></div><div></div>";
  auto result = run_query(html, "SELECT COUNT(div) FROM document");
  expect_eq(result.rows.size(), 1, "count aggregate row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].node_id == 2, "count aggregate value");
  }
}

void test_count_star() {
  std::string html = "<div></div><span></span>";
  auto result = run_query(html, "SELECT COUNT(*) FROM document");
  expect_eq(result.rows.size(), 1, "count star row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].node_id >= 2, "count star value");
  }
}

void test_summarize_star() {
  std::string html = "<div></div><div></div><span></span>";
  auto result = run_query(html, "SELECT summarize(*) FROM document");
  bool saw_div = false;
  for (const auto& row : result.rows) {
    if (row.tag == "div" && row.node_id == 2) {
      saw_div = true;
      break;
    }
  }
  expect_true(saw_div, "summarize star includes div count");
}

void test_summarize_limit() {
  std::string html = "<div></div><div></div><span></span>";
  auto result = run_query(html, "SELECT summarize(*) FROM document LIMIT 1");
  expect_eq(result.rows.size(), 1, "summarize limit row count");
}

void test_summarize_order_by_count() {
  std::string html = "<div></div><div></div><span></span>";
  auto result = run_query(html, "SELECT summarize(*) FROM document ORDER BY count DESC");
  expect_true(!result.rows.empty(), "summarize order by count non-empty");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].tag == "div", "summarize order by count first");
    expect_true(result.rows[0].node_id == 2, "summarize order by count value");
  }
}

void test_to_table_flag() {
  std::string html = "<table><tr><td>A</td></tr></table>";
  auto result = run_query(html, "SELECT table FROM document TO TABLE()");
  expect_true(result.to_table, "to table flag set");
}

void test_to_table_header_on() {
  std::string html = "<table><tr><td>H1</td></tr><tr><td>A</td></tr></table>";
  auto result = run_query(html, "SELECT table FROM document TO TABLE(HEADER=ON)");
  expect_true(result.to_table, "to table header on flag set");
  expect_true(result.table_has_header, "to table header on");
}

void test_to_table_header_off() {
  std::string html = "<table><tr><td>H1</td></tr><tr><td>A</td></tr></table>";
  auto result = run_query(html, "SELECT table FROM document TO TABLE(HEADER=OFF)");
  expect_true(result.to_table, "to table header off flag set");
  expect_true(!result.table_has_header, "to table header off");
}

void test_to_list_flag() {
  std::string html = "<a href='x'></a>";
  auto result = run_query(html, "SELECT a.href FROM document TO LIST()");
  expect_true(result.to_list, "to list flag set");
}

}  // namespace

void register_function_tests(std::vector<TestCase>& tests) {
  tests.push_back({"text_requires_non_tag_filter", test_text_requires_non_tag_filter});
  tests.push_back({"inner_html_function", test_inner_html_function});
  tests.push_back({"inner_html_depth", test_inner_html_depth});
  tests.push_back({"trim_inner_html", test_trim_inner_html});
  tests.push_back({"count_aggregate", test_count_aggregate});
  tests.push_back({"count_star", test_count_star});
  tests.push_back({"summarize_star", test_summarize_star});
  tests.push_back({"summarize_limit", test_summarize_limit});
  tests.push_back({"summarize_order_by_count", test_summarize_order_by_count});
  tests.push_back({"to_table_flag", test_to_table_flag});
  tests.push_back({"to_table_header_on", test_to_table_header_on});
  tests.push_back({"to_table_header_off", test_to_table_header_off});
  tests.push_back({"to_list_flag", test_to_list_flag});
}
