#include "test_harness.h"
#include "test_utils.h"

namespace {

void test_projection_parent_id() {
  std::string html = "<div id='root'><span></span></div>";
  auto result = run_query(html, "SELECT span.parent_id FROM document");
  expect_eq(result.columns.size(), 1, "projection has one column");
  if (!result.columns.empty()) {
    expect_true(result.columns[0] == "parent_id", "projection column name");
  }
  expect_eq(result.rows.size(), 1, "projection row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].parent_id.has_value(), "projection parent_id value");
  }
}

void test_projection_attributes() {
  std::string html = "<a id='x' href='test'></a>";
  auto result = run_query(html, "SELECT a.attributes FROM document");
  expect_eq(result.columns.size(), 1, "attributes projection has one column");
  if (!result.columns.empty()) {
    expect_true(result.columns[0] == "attributes", "attributes projection column name");
  }
  expect_eq(result.rows.size(), 1, "attributes projection row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].attributes.count("id") == 1, "attributes contains id");
  }
}

void test_projection_tag_field_list() {
  std::string html = "<div id='root'><span></span></div>";
  auto result = run_query(html, "SELECT div(node_id, tag, parent_id) FROM document");
  expect_eq(result.columns.size(), 3, "tag field list column count");
  if (result.columns.size() == 3) {
    expect_true(result.columns[0] == "node_id", "tag field list column 1");
    expect_true(result.columns[1] == "tag", "tag field list column 2");
    expect_true(result.columns[2] == "parent_id", "tag field list column 3");
  }
  expect_eq(result.rows.size(), 1, "tag field list row count");
}

void test_attribute_projection_value() {
  std::string html = "<a href='x'></a>";
  auto result = run_query(html, "SELECT a.href FROM document");
  expect_eq(result.rows.size(), 1, "attribute projection row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].attributes["href"] == "x", "attribute projection value");
  }
}

void test_projection_sibling_pos() {
  std::string html = "<ul><li>First</li><li>Second</li></ul>";
  auto result = run_query(html, "SELECT li.sibling_pos FROM document");
  expect_eq(result.rows.size(), 2, "sibling_pos projection row count");
  if (result.rows.size() >= 2) {
    expect_true(result.rows[0].sibling_pos == 1, "sibling_pos first");
    expect_true(result.rows[1].sibling_pos == 2, "sibling_pos second");
  }
}

void test_select_exclude_single() {
  std::string html = "<div></div>";
  auto result = run_query(html, "SELECT * EXCLUDE source_uri FROM document");
  expect_eq(result.columns.size(), 6, "exclude removes one column");
}

void test_select_exclude_list() {
  std::string html = "<div></div>";
  auto result = run_query(html, "SELECT * EXCLUDE (source_uri, tag) FROM document");
  expect_eq(result.columns.size(), 5, "exclude removes two columns");
}

}  // namespace

void register_projection_tests(std::vector<TestCase>& tests) {
  tests.push_back({"projection_parent_id", test_projection_parent_id});
  tests.push_back({"projection_attributes", test_projection_attributes});
  tests.push_back({"projection_tag_field_list", test_projection_tag_field_list});
  tests.push_back({"attribute_projection_value", test_attribute_projection_value});
  tests.push_back({"projection_sibling_pos", test_projection_sibling_pos});
  tests.push_back({"select_exclude_single", test_select_exclude_single});
  tests.push_back({"select_exclude_list", test_select_exclude_list});
}
