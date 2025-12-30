#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "xsql/xsql.h"
#include "render/duckbox_renderer.h"

namespace {

struct TestCase {
  const char* name;
  void (*fn)();
};

int g_failures = 0;
std::string g_current_test;

void expect_true(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL [" << g_current_test << "]: " << message << std::endl;
    ++g_failures;
  }
}

void expect_eq(size_t actual, size_t expected, const std::string& message) {
  if (actual != expected) {
    std::cerr << "FAIL [" << g_current_test << "]: " << message
              << " (expected " << expected << ", got " << actual << ")" << std::endl;
    ++g_failures;
  }
}

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

void test_select_ul_by_id() {
  std::string html = "<ul id='countries'><li>US</li></ul>";
  auto result = run_query(html, "SELECT ul FROM document WHERE attributes.id = 'countries'");
  expect_eq(result.rows.size(), 1, "select ul by id");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].tag == "ul", "tag is ul");
  }
}

void test_class_in_matches_token() {
  std::string html = "<div class=\"subtle newest\"></div><div class=\"old\"></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE attributes.class IN ('newest')");
  expect_eq(result.rows.size(), 1, "class IN matches token");
}

void test_parent_attribute_filter() {
  std::string html = "<div id='table-01'><table></table></div><div id='table-02'><table></table></div>";
  auto result = run_query(html, "SELECT table FROM document WHERE parent.attributes.id = 'table-01'");
  expect_eq(result.rows.size(), 1, "parent attribute filter");
}

void test_multi_tag_select() {
  std::string html = "<h1></h1><h2></h2><p></p>";
  auto result = run_query(html, "SELECT h1,h2 FROM document");
  expect_eq(result.rows.size(), 2, "multi-tag select");
}

void test_select_star() {
  std::string html = "<div></div><span></span>";
  auto result = run_query(html, "SELECT * FROM document");
  expect_true(result.rows.size() >= 2, "select star returns at least html nodes");
  bool saw_div = false;
  bool saw_span = false;
  for (const auto& row : result.rows) {
    if (row.tag == "div") saw_div = true;
    if (row.tag == "span") saw_span = true;
  }
  expect_true(saw_div && saw_span, "select star includes div/span");
}

void test_class_eq_matches_token() {
  std::string html = "<div class=\"subtle newest\"></div><div class=\"newest\"></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE attributes.class = 'subtle'");
  expect_eq(result.rows.size(), 1, "class = matches token");
}

void test_missing_attribute_no_match() {
  std::string html = "<div></div><div id='a'></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE attributes.id = 'missing'");
  expect_eq(result.rows.size(), 0, "missing attribute yields no match");
}

void test_invalid_query_throws() {
  bool threw = false;
  try {
    std::string html = "<div></div>";
    run_query(html, "SELECT FROM document");
  } catch (const std::exception&) {
    threw = true;
  }
  expect_true(threw, "invalid query throws");
}

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

void test_child_axis_direct_only() {
  std::string html = "<ul class='dropdown-menu'><li><div><a href='/x'>X</a></div></li><li><a href='/y'>Y</a></li></ul>";
  auto result = run_query(html,
                          "SELECT li FROM document WHERE parent.tag = 'ul' AND parent.attributes.class = 'dropdown-menu' AND child.tag = 'a'");
  expect_eq(result.rows.size(), 1, "child axis matches direct child only");
}

void test_ancestor_filter_on_a() {
  std::string html = "<ul class='dropdown-menu'><li><a href='/x'>X</a></li></ul><ul><li><a href='/y'>Y</a></li></ul>";
  auto result = run_query(html,
                          "SELECT TEXT(a) FROM document WHERE parent.tag = 'li' AND ancestor.attributes.class = 'dropdown-menu' AND attributes.href <> ''");
  expect_eq(result.rows.size(), 1, "ancestor filter keeps only dropdown items");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].text == "X", "ancestor filter returns expected text");
  }
}

void test_limit() {
  std::string html = "<a></a><a></a><a></a>";
  auto result = run_query(html, "SELECT a FROM document LIMIT 2");
  expect_eq(result.rows.size(), 2, "limit returns 2 rows");
}

void test_alias_qualifier() {
  std::string html = "<a id='x'></a>";
  auto result = run_query(html, "SELECT a FROM document AS d WHERE d.attributes.id = 'x'");
  expect_eq(result.rows.size(), 1, "alias qualifier matches");
}

void test_alias_source_only() {
  std::string html = "<a id='x'></a>";
  auto result = run_query(html, "SELECT a FROM d WHERE d.attributes.id = 'x'");
  expect_eq(result.rows.size(), 1, "alias source without document");
}

void test_shorthand_attribute_filter() {
  std::string html = "<a title='Menu'></a><a title='Other'></a>";
  auto result = run_query(html, "SELECT a FROM document WHERE title = 'Menu'");
  expect_eq(result.rows.size(), 1, "shorthand attribute filter");
}

void test_shorthand_qualified_attribute_filter() {
  std::string html = "<a title='Menu'></a><a title='Other'></a>";
  auto result = run_query(html, "SELECT a FROM doc WHERE doc.title = 'Menu'");
  expect_eq(result.rows.size(), 1, "shorthand qualified attribute filter");
}

void test_ancestor_attribute_filter() {
  std::string html = "<div id='root'><section><a></a></section></div>";
  auto result = run_query(html, "SELECT a FROM document WHERE ancestor.attributes.id = 'root'");
  expect_eq(result.rows.size(), 1, "ancestor attribute filter");
}

void test_descendant_attribute_filter() {
  std::string html = "<div id='root'><section><a id='child'></a></section></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE descendant.attributes.id = 'child'");
  expect_eq(result.rows.size(), 1, "descendant attribute filter");
}

void test_parent_tag_filter() {
  std::string html = "<div><a></a></div><span><a></a></span>";
  auto result = run_query(html, "SELECT a FROM document WHERE parent.tag = 'div'");
  expect_eq(result.rows.size(), 1, "parent tag filter");
}

void test_parent_id_filter() {
  std::string html = "<div><span></span></div>";
  auto parent_result = run_query(html, "SELECT span.parent_id FROM document");
  expect_true(!parent_result.rows.empty(), "parent_id projection rows");
  if (!parent_result.rows.empty()) {
    expect_true(parent_result.rows[0].parent_id.has_value(), "parent_id projection value");
    if (parent_result.rows[0].parent_id.has_value()) {
      std::string query = "SELECT span FROM document WHERE parent_id = " +
                          std::to_string(*parent_result.rows[0].parent_id);
      auto result = run_query(html, query);
      expect_true(!result.rows.empty(), "parent_id filter matches");
    }
  }
}

void test_attributes_is_null() {
  std::string html = "<div></div><span id='x'></span>";
  auto result = run_query(html, "SELECT div FROM document WHERE attributes IS NULL");
  expect_eq(result.rows.size(), 1, "attributes is null matches empty map");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].tag == "div", "attributes is null returns div");
  }
}

void test_attributes_is_not_null() {
  std::string html = "<div></div><span id='x'></span>";
  auto result = run_query(html, "SELECT span FROM document WHERE attributes IS NOT NULL");
  expect_eq(result.rows.size(), 1, "attributes is not null matches non-empty map");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].tag == "span", "attributes is not null returns span");
  }
}

void test_node_id_filter() {
  std::string html = "<div></div><span></span>";
  auto id_result = run_query(html, "SELECT span.node_id FROM document");
  expect_true(!id_result.rows.empty(), "node_id projection rows");
  if (!id_result.rows.empty()) {
    int64_t id = id_result.rows[0].node_id;
    auto result = run_query(html, "SELECT span FROM document WHERE node_id = " + std::to_string(id));
    expect_eq(result.rows.size(), 1, "node_id filter matches");
    if (!result.rows.empty()) {
      expect_true(result.rows[0].tag == "span", "node_id filter returns span");
    }
  }
}

void test_to_table_flag() {
  std::string html = "<table><tr><th>H</th></tr></table>";
  auto result = run_query(html, "SELECT table FROM document TO TABLE()");
  expect_true(result.to_table, "to_table flag set");
}

void test_to_list_flag() {
  std::string html = "<a id='x'></a><a id='y'></a>";
  auto result = run_query(html, "SELECT a.attributes FROM document TO LIST()");
  expect_true(result.to_list, "to_list flag set");
  expect_eq(result.columns.size(), 1, "to_list single column");
}

void test_attribute_projection_value() {
  std::string html = "<link href='a.css' rel='stylesheet'><link href='b.css'>";
  auto result = run_query(html, "SELECT link.href FROM document");
  expect_eq(result.rows.size(), 2, "attribute projection row count");
  expect_eq(result.columns.size(), 1, "attribute projection column count");
  if (!result.columns.empty()) {
    expect_true(result.columns[0] == "href", "attribute projection column name");
  }
}

void test_count_aggregate() {
  std::string html = "<a></a><a></a><div></div>";
  auto result = run_query(html, "SELECT COUNT(a) FROM document");
  expect_eq(result.columns.size(), 1, "count column");
  expect_eq(result.rows.size(), 1, "count single row");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].node_id == 2, "count value");
  }
}

void test_count_star() {
  std::string html = "<a></a><a></a><div></div>";
  auto result = run_query(html, "SELECT COUNT(*) FROM document");
  expect_eq(result.rows.size(), 1, "count star row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].node_id >= 3, "count star value");
  }
}

void test_summarize_star() {
  std::string html = "<div></div><span></span><div></div>";
  auto result = run_query(html, "SELECT summarize(*) FROM document");
  expect_eq(result.columns.size(), 2, "summarize columns");
  if (result.columns.size() == 2) {
    expect_true(result.columns[0] == "tag", "summarize tag column");
    expect_true(result.columns[1] == "count", "summarize count column");
  }
  size_t div_count = 0;
  size_t span_count = 0;
  for (const auto& row : result.rows) {
    if (row.tag == "div") div_count = static_cast<size_t>(row.node_id);
    if (row.tag == "span") span_count = static_cast<size_t>(row.node_id);
  }
  expect_true(div_count == 2, "summarize div count");
  expect_true(span_count == 1, "summarize span count");
}

void test_summarize_limit() {
  std::string html = "<div></div><span></span><div></div><p></p>";
  auto result = run_query(html, "SELECT summarize(*) FROM document LIMIT 1");
  expect_eq(result.rows.size(), 1, "summarize limit size");
}

void test_summarize_order_by_count() {
  std::string html = "<div></div><span></span><div></div><p></p>";
  auto result = run_query(html, "SELECT summarize(*) FROM document ORDER BY count DESC");
  expect_true(!result.rows.empty(), "summarize order rows");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].tag == "div", "summarize order by count desc");
    expect_true(result.rows[0].node_id == 2, "summarize order by count value");
  }
}

void test_order_by_tag() {
  std::string html = "<span></span><div></div><a></a>";
  auto result = run_query(html, "SELECT span,div,a FROM document ORDER BY tag");
  expect_eq(result.rows.size(), 3, "order by tag row count");
  if (result.rows.size() == 3) {
    expect_true(result.rows[0].tag == "a", "order by tag first");
    expect_true(result.rows[1].tag == "div", "order by tag second");
    expect_true(result.rows[2].tag == "span", "order by tag third");
  }
}

void test_order_by_node_id_desc() {
  std::string html = "<div></div><span></span><a></a>";
  auto result = run_query(html, "SELECT div,span,a FROM document ORDER BY node_id DESC");
  expect_eq(result.rows.size(), 3, "order by node_id desc row count");
  if (result.rows.size() == 3) {
    expect_true(result.rows[0].node_id >= result.rows[1].node_id, "order by node_id desc 1");
    expect_true(result.rows[1].node_id >= result.rows[2].node_id, "order by node_id desc 2");
  }
}

void test_order_by_multi() {
  std::string html = "<div></div><span></span><span></span><div></div>";
  auto result = run_query(html, "SELECT div,span FROM document ORDER BY tag, parent_id");
  expect_eq(result.rows.size(), 4, "order by multi row count");
  if (result.rows.size() == 4) {
    expect_true(result.rows[0].tag <= result.rows[1].tag, "order by multi tag 1");
    expect_true(result.rows[1].tag <= result.rows[2].tag, "order by multi tag 2");
    expect_true(result.rows[2].tag <= result.rows[3].tag, "order by multi tag 3");
  }
}

void test_not_equal_attribute() {
  std::string html = "<a id='x'></a><a id='y'></a><a></a>";
  auto result = run_query(html, "SELECT a FROM document WHERE attributes.id <> 'x'");
  expect_eq(result.rows.size(), 1, "not equal attribute");
}

void test_is_not_null_attribute() {
  std::string html = "<a href='x'></a><a></a>";
  auto result = run_query(html, "SELECT a FROM document WHERE attributes.href IS NOT NULL");
  expect_eq(result.rows.size(), 1, "is not null attribute");
}

void test_is_null_attribute() {
  std::string html = "<a href='x'></a><a></a>";
  auto result = run_query(html, "SELECT a FROM document WHERE attributes.href IS NULL");
  expect_eq(result.rows.size(), 1, "is null attribute");
}

void test_text_not_equal() {
  std::string html = "<span></span><span>ok</span>";
  auto result = run_query(html, "SELECT span FROM document WHERE text <> ''");
  expect_eq(result.rows.size(), 1, "text not equal empty");
}

void test_regex_attribute() {
  std::string html = "<a href='file.pdf'></a><a href='file.txt'></a>";
  auto result = run_query(html, "SELECT a FROM document WHERE attributes.href ~ '.*\\.pdf$'");
  expect_eq(result.rows.size(), 1, "regex attribute");
}

void test_duckbox_basic_table() {
  auto result = make_result({"node_id", "tag"}, {{"1", "div"}, {"2", "span"}});
  xsql::render::DuckboxOptions options;
  options.max_width = 80;
  options.max_rows = 40;
  options.highlight = false;
  options.is_tty = false;
  std::string out = xsql::render::render_duckbox(result, options);
  std::string expected =
      "┌─────────┬──────┐\n"
      "│ node_id │ tag  │\n"
      "├─────────┼──────┤\n"
      "│       1 │ div  │\n"
      "│       2 │ span │\n"
      "└─────────┴──────┘";
  expect_true(out == expected, "duckbox basic table");
}

void test_duckbox_truncate_cells() {
  auto result = make_result({"text"}, {{"abcdefghijklmnopqrstuvwxyz"}});
  xsql::render::DuckboxOptions options;
  options.max_width = 20;
  options.max_rows = 40;
  options.highlight = false;
  options.is_tty = false;
  std::string out = xsql::render::render_duckbox(result, options);
  std::string expected =
      "┌──────────────────┐\n"
      "│ text             │\n"
      "├──────────────────┤\n"
      "│ abcdefghijklmno… │\n"
      "└──────────────────┘";
  expect_true(out == expected, "duckbox truncates wide cells");
}

void test_duckbox_maxrows_truncate() {
  auto result = make_result({"column_one_headerx", "column_two_headerx"},
                            {{"a", "b"}, {"c", "d"}, {"e", "f"}});
  xsql::render::DuckboxOptions options;
  options.max_width = 120;
  options.max_rows = 2;
  options.highlight = false;
  options.is_tty = false;
  std::string out = xsql::render::render_duckbox(result, options);
  std::string expected =
      "┌────────────────────┬────────────────────┐\n"
      "│ column_one_headerx │ column_two_headerx │\n"
      "├────────────────────┼────────────────────┤\n"
      "│ a                  │ b                  │\n"
      "│ c                  │ d                  │\n"
      "│ … truncated, showing first 2 of 3 rows… │\n"
      "└────────────────────┴────────────────────┘";
  expect_true(out == expected, "duckbox maxrows truncation");
}

void test_duckbox_numeric_alignment() {
  auto result = make_result({"name", "value"}, {{"alpha", "12"}, {"beta", "3.5"}});
  xsql::render::DuckboxOptions options;
  options.max_width = 80;
  options.max_rows = 40;
  options.highlight = false;
  options.is_tty = false;
  std::string out = xsql::render::render_duckbox(result, options);
  std::string expected =
      "┌───────┬───────┐\n"
      "│ name  │ value │\n"
      "├───────┼───────┤\n"
      "│ alpha │    12 │\n"
      "│ beta  │   3.5 │\n"
      "└───────┴───────┘";
  expect_true(out == expected, "duckbox numeric alignment");
}

void test_duckbox_null_rendering() {
  auto result = make_result({"parent_id", "tag"}, {{"NULL", "div"}});
  xsql::render::DuckboxOptions options;
  options.max_width = 80;
  options.max_rows = 40;
  options.highlight = false;
  options.is_tty = false;
  std::string out = xsql::render::render_duckbox(result, options);
  std::string expected =
      "┌───────────┬──────┐\n"
      "│ parent_id │ tag  │\n"
      "├───────────┼──────┤\n"
      "│ NULL      │ div  │\n"
      "└───────────┴──────┘";
  expect_true(out == expected, "duckbox null rendering");
}

const TestCase kTests[] = {
    {"select_ul_by_id", test_select_ul_by_id},
    {"class_in_matches_token", test_class_in_matches_token},
    {"parent_attribute_filter", test_parent_attribute_filter},
    {"multi_tag_select", test_multi_tag_select},
    {"select_star", test_select_star},
    {"class_eq_matches_token", test_class_eq_matches_token},
    {"missing_attribute_no_match", test_missing_attribute_no_match},
    {"invalid_query_throws", test_invalid_query_throws},
    {"text_requires_non_tag_filter", test_text_requires_non_tag_filter},
    {"projection_parent_id", test_projection_parent_id},
    {"projection_attributes", test_projection_attributes},
    {"projection_tag_field_list", test_projection_tag_field_list},
    {"inner_html_function", test_inner_html_function},
    {"inner_html_depth", test_inner_html_depth},
    {"trim_inner_html", test_trim_inner_html},
    {"child_axis_direct_only", test_child_axis_direct_only},
    {"ancestor_filter_on_a", test_ancestor_filter_on_a},
    {"limit", test_limit},
    {"alias_qualifier", test_alias_qualifier},
    {"alias_source_only", test_alias_source_only},
    {"shorthand_attribute_filter", test_shorthand_attribute_filter},
    {"shorthand_qualified_attribute_filter", test_shorthand_qualified_attribute_filter},
    {"ancestor_attribute_filter", test_ancestor_attribute_filter},
    {"descendant_attribute_filter", test_descendant_attribute_filter},
    {"parent_tag_filter", test_parent_tag_filter},
    {"parent_id_filter", test_parent_id_filter},
    {"attributes_is_null", test_attributes_is_null},
    {"attributes_is_not_null", test_attributes_is_not_null},
    {"node_id_filter", test_node_id_filter},
    {"to_table_flag", test_to_table_flag},
    {"to_list_flag", test_to_list_flag},
    {"attribute_projection_value", test_attribute_projection_value},
    {"count_aggregate", test_count_aggregate},
    {"count_star", test_count_star},
    {"summarize_star", test_summarize_star},
    {"summarize_limit", test_summarize_limit},
    {"summarize_order_by_count", test_summarize_order_by_count},
    {"order_by_tag", test_order_by_tag},
    {"order_by_node_id_desc", test_order_by_node_id_desc},
    {"order_by_multi", test_order_by_multi},
    {"not_equal_attribute", test_not_equal_attribute},
    {"is_not_null_attribute", test_is_not_null_attribute},
    {"is_null_attribute", test_is_null_attribute},
    {"text_not_equal", test_text_not_equal},
    {"regex_attribute", test_regex_attribute},
    {"duckbox_basic_table", test_duckbox_basic_table},
    {"duckbox_truncate_cells", test_duckbox_truncate_cells},
    {"duckbox_maxrows_truncate", test_duckbox_maxrows_truncate},
    {"duckbox_numeric_alignment", test_duckbox_numeric_alignment},
    {"duckbox_null_rendering", test_duckbox_null_rendering},
};

int run_test(const TestCase& test) {
  g_current_test = test.name;
  g_failures = 0;
  test.fn();
  return g_failures;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1) {
    std::string target = argv[1];
    for (const auto& test : kTests) {
      if (target == test.name) {
        int failures = run_test(test);
        return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
      }
    }
    std::cerr << "Unknown test: " << target << std::endl;
    std::cerr << "Available tests:" << std::endl;
    for (const auto& test : kTests) {
      std::cerr << "  " << test.name << std::endl;
    }
    return EXIT_FAILURE;
  }

  int total_failures = 0;
  for (const auto& test : kTests) {
    int failures = run_test(test);
    if (failures > 0) {
      std::cerr << "FAILED: " << test.name << " (" << failures << ")" << std::endl;
      total_failures += failures;
    }
  }

  if (total_failures > 0) {
    std::cerr << total_failures << " test(s) failed." << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "All tests passed." << std::endl;
  return EXIT_SUCCESS;
}
