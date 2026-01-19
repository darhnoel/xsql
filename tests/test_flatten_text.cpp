#include "test_harness.h"
#include "test_utils.h"

namespace {

void test_max_depth_values() {
  std::string html = "<div id='root'><span><em></em></span></div>";
  auto div_result = run_query(html, "SELECT div.max_depth FROM document");
  expect_eq(div_result.rows.size(), 1, "div max_depth row count");
  if (!div_result.rows.empty()) {
    expect_true(div_result.rows[0].max_depth == 2, "div max_depth value");
  }

  auto em_result = run_query(html, "SELECT em.max_depth FROM document");
  expect_eq(em_result.rows.size(), 1, "em max_depth row count");
  if (!em_result.rows.empty()) {
    expect_true(em_result.rows[0].max_depth == 0, "em max_depth value");
  }
}

void test_flatten_text_default_all_descendants() {
  std::string html =
      "<div id='a'><section><p>One</p><span>Two</span></section></div>"
      "<div id='b'><section><p>Three</p></section></div>";
  auto result = run_query(
      html,
      "SELECT div.node_id, FLATTEN_TEXT(div) AS (col1, col2) "
      "FROM document WHERE descendant.tag IN ('p','span')");
  expect_eq(result.rows.size(), 2, "flatten_text default row count");
  if (result.rows.size() == 2) {
    expect_true(result.rows[0].node_id < result.rows[1].node_id, "flatten_text preserves doc order");
    expect_true(result.rows[0].computed_fields["col1"] == "One", "flatten_text first value");
    expect_true(result.rows[0].computed_fields["col2"] == "Two", "flatten_text second value");
    expect_true(result.rows[1].computed_fields["col1"] == "Three", "flatten_text second row value");
    expect_true(result.rows[1].computed_fields.find("col2") == result.rows[1].computed_fields.end(),
                "flatten_text pads with NULL");
  }
}

void test_flatten_text_explicit_depth() {
  std::string html = "<div><section> Alpha <p>One</p></section></div>";
  auto result = run_query(
      html,
      "SELECT div.node_id, FLATTEN_TEXT(div, 1) AS (col1) FROM document");
  expect_eq(result.rows.size(), 1, "flatten_text depth row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].computed_fields["col1"] == "Alpha",
                "flatten_text depth direct text");
  }
}

void test_flatten_text_descendant_tag_eq() {
  std::string html = "<div><section><p>One</p><span>Two</span></section></div>";
  auto result = run_query(
      html,
      "SELECT div.node_id, FLATTEN_TEXT(div) AS (col1, col2) "
      "FROM document WHERE descendant.tag = 'p'");
  expect_eq(result.rows.size(), 1, "flatten_text descendant tag row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].computed_fields["col1"] == "One", "flatten_text descendant tag value");
    expect_true(result.rows[0].computed_fields.find("col2") == result.rows[0].computed_fields.end(),
                "flatten_text descendant tag truncates");
  }
}

void test_flatten_text_truncation() {
  std::string html = "<div><p>One</p><p>Two</p></div>";
  auto result = run_query(
      html,
      "SELECT div.node_id, FLATTEN_TEXT(div) AS (col1) FROM document WHERE descendant.tag = 'p'");
  expect_eq(result.rows.size(), 1, "flatten_text truncation row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].computed_fields["col1"] == "One",
                "flatten_text truncates extras");
  }
}

void test_flatten_text_default_uses_deepest_text() {
  std::string html = "<div><span><i></i></span><p>Text</p></div>";
  auto result = run_query(html, "SELECT FLATTEN_TEXT(div) FROM document");
  expect_eq(result.rows.size(), 1, "flatten_text deepest text row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].computed_fields["flatten_text"] == "Text",
                "flatten_text uses deepest text depth");
  }
}

void test_flatten_text_default_alias() {
  std::string html = "<div><p>One</p></div>";
  auto result = run_query(html, "SELECT FLATTEN_TEXT(div) FROM document");
  expect_eq(result.columns.size(), 1, "flatten_text default alias column count");
  if (!result.columns.empty()) {
    expect_true(result.columns[0] == "flatten_text", "flatten_text default alias name");
  }
  expect_eq(result.rows.size(), 1, "flatten_text default alias row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].computed_fields["flatten_text"] == "One",
                "flatten_text default alias value");
  }
}

void test_flatten_text_descendant_attribute_filter() {
  std::string html =
      "<div>"
      "<span data-testid='flight-time-1'>08:00</span>"
      "<span>Skip</span>"
      "<span data-testid='flight_price_1'>US$1</span>"
      "</div>";
  auto result = run_query(
      html,
      "SELECT FLATTEN_TEXT(div) AS (c1, c2) FROM document "
      "WHERE descendant.attributes.data-testid CONTAINS ANY ('flight-time-', 'flight_price_')");
  expect_eq(result.rows.size(), 1, "flatten_text descendant attr row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].computed_fields["c1"] == "08:00",
                "flatten_text descendant attr first");
    expect_true(result.rows[0].computed_fields["c2"] == "US$1",
                "flatten_text descendant attr second");
  }
}

void test_flatten_text_realistic_flight_card() {
  std::string html =
      "<div class='section-root'>"
      "<div class='carrier-name' data-testid='carrier-name'>"
      "<div><div class='carrier-text'>Example Airline</div></div>"
      "</div>"
      "<div data-testid='time-depart'><span>08:00</span></div>"
      "<div class='duration-box' data-testid='duration'><span>10h</span></div>"
      "<span data-testid='layover'>3h 20m in Sample City</span>"
      "<div data-testid='time-arrive'><span>20:00</span></div>"
      "<div class='price-area' aria-label='Total price: US$463'>"
      "<span data-testid='price'>US$463</span>"
      "</div>"
      "</div>";
  auto result = run_query(
      html,
      "SELECT div.node_id, FLATTEN_TEXT(div) AS (t1, dur, layover, t2, price) "
      "FROM document WHERE div.attributes.class = 'section-root' "
      "AND descendant.attributes.data-testid CONTAINS ANY "
      "('time-depart', 'duration', 'layover', 'time-arrive', 'price')");
  expect_eq(result.rows.size(), 1, "flatten_text realistic row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].computed_fields["t1"] == "08:00",
                "flatten_text realistic t1");
    expect_true(result.rows[0].computed_fields["dur"] == "10h",
                "flatten_text realistic duration");
    expect_true(result.rows[0].computed_fields["layover"] == "3h 20m in Guangzhou",
                "flatten_text realistic layover");
    expect_true(result.rows[0].computed_fields["t2"] == "20:00",
                "flatten_text realistic t2");
    expect_true(result.rows[0].computed_fields["price"] == "US$463",
                "flatten_text realistic price");
  }
}

}  // namespace

void register_flatten_text_tests(std::vector<TestCase>& tests) {
  tests.push_back({"max_depth_values", test_max_depth_values});
  tests.push_back({"flatten_text_default_all_descendants", test_flatten_text_default_all_descendants});
  tests.push_back({"flatten_text_explicit_depth", test_flatten_text_explicit_depth});
  tests.push_back({"flatten_text_descendant_tag_eq", test_flatten_text_descendant_tag_eq});
  tests.push_back({"flatten_text_truncation", test_flatten_text_truncation});
  tests.push_back({"flatten_text_default_uses_deepest_text", test_flatten_text_default_uses_deepest_text});
  tests.push_back({"flatten_text_default_alias", test_flatten_text_default_alias});
  tests.push_back({"flatten_text_descendant_attribute_filter", test_flatten_text_descendant_attribute_filter});
  tests.push_back({"flatten_text_realistic_flight_card", test_flatten_text_realistic_flight_card});
}
