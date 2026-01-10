#include <exception>

#include "test_harness.h"
#include "test_utils.h"

namespace {

void test_raw_source_literal() {
  std::string html = "<div></div>";
  auto result = run_query(html, "SELECT li FROM RAW('<ul><li>1</li><li>2</li></ul>')");
  expect_eq(result.rows.size(), 2, "RAW() source literal parses list items");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].tag == "li", "RAW() yields li tags");
  }
}

void test_fragments_from_raw() {
  std::string html = "<div></div>";
  auto result = run_query(html,
                          "SELECT li FROM FRAGMENTS(RAW('<ul><li>1</li><li>2</li></ul>')) AS frag");
  expect_eq(result.rows.size(), 2, "FRAGMENTS() parses RAW() fragments");
}

void test_fragments_from_inner_html() {
  std::string html = "<div class='pagination'><ul><li>1</li><li>2</li></ul></div>";
  auto result = run_query(html,
                          "SELECT li FROM FRAGMENTS(SELECT inner_html(div, 2) FROM document "
                          "WHERE attributes.class = 'pagination') AS frag");
  expect_eq(result.rows.size(), 2, "FRAGMENTS() parses inner_html fragments");
}

void test_fragments_non_html_error() {
  bool threw = false;
  try {
    std::string html = "<a href='x'></a>";
    run_query(html,
              "SELECT li FROM FRAGMENTS(SELECT attributes.href FROM document) AS frag");
  } catch (const std::exception&) {
    threw = true;
  }
  expect_true(threw, "FRAGMENTS rejects non-HTML input");
}

}  // namespace

void register_fragments_tests(std::vector<TestCase>& tests) {
  tests.push_back({"raw_source_literal", test_raw_source_literal});
  tests.push_back({"fragments_from_raw", test_fragments_from_raw});
  tests.push_back({"fragments_from_inner_html", test_fragments_from_inner_html});
  tests.push_back({"fragments_non_html_error", test_fragments_non_html_error});
}
