#include "test_harness.h"
#include "test_utils.h"

namespace {

void test_attributes_is_null() {
  std::string html = "<div></div><div id='x'></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE attributes IS NULL");
  expect_eq(result.rows.size(), 1, "attributes is null");
}

void test_attributes_is_not_null() {
  std::string html = "<div></div><div id='x'></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE attributes IS NOT NULL");
  expect_eq(result.rows.size(), 1, "attributes is not null");
}

void test_not_equal_attribute() {
  std::string html = "<a href='x'></a><a href='y'></a>";
  auto result = run_query(html, "SELECT a FROM document WHERE attributes.href <> 'x'");
  expect_eq(result.rows.size(), 1, "not equal attribute");
}

void test_is_not_null_attribute() {
  std::string html = "<a></a><a href='x'></a>";
  auto result = run_query(html, "SELECT a FROM document WHERE attributes.href IS NOT NULL");
  expect_eq(result.rows.size(), 1, "is not null attribute");
}

void test_is_null_attribute() {
  std::string html = "<a></a><a href='x'></a>";
  auto result = run_query(html, "SELECT a FROM document WHERE attributes.href IS NULL");
  expect_eq(result.rows.size(), 1, "is null attribute");
}

void test_text_not_equal() {
  std::string html = "<div>Hi</div><div></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE text <> ''");
  expect_eq(result.rows.size(), 1, "text not equal");
}

void test_regex_attribute() {
  std::string html = "<a href='file.pdf'></a><a href='file.txt'></a>";
  auto result = run_query(html, "SELECT a FROM document WHERE attributes.href ~ '.*\\.pdf$'");
  expect_eq(result.rows.size(), 1, "regex attribute");
}

void test_contains_attribute() {
  std::string html = "<a href='https://techkhmer.net'></a><a href='https://example.com'></a>";
  auto result = run_query(html, "SELECT a FROM document WHERE attributes.href CONTAINS 'TECHKHMEr'");
  expect_eq(result.rows.size(), 1, "contains attribute");
}

void test_has_direct_text() {
  std::string html = "<div>Something <section>Else</section></div><div><section>Something</section></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE div HAS_DIRECT_TEXT 'something'");
  expect_eq(result.rows.size(), 1, "has direct text");
}

void test_parenthesized_predicates() {
  std::string html =
      "<a id='keep' href='x'></a><a id='keep' href='y'></a><a id='skip' href='y'></a>";
  auto result = run_query(html,
                          "SELECT a FROM document WHERE attributes.id = 'keep' AND "
                          "(attributes.href = 'x' OR attributes.href = 'y')");
  expect_eq(result.rows.size(), 2, "parenthesized predicates");
}

}  // namespace

void register_predicate_tests(std::vector<TestCase>& tests) {
  tests.push_back({"attributes_is_null", test_attributes_is_null});
  tests.push_back({"attributes_is_not_null", test_attributes_is_not_null});
  tests.push_back({"not_equal_attribute", test_not_equal_attribute});
  tests.push_back({"is_not_null_attribute", test_is_not_null_attribute});
  tests.push_back({"is_null_attribute", test_is_null_attribute});
  tests.push_back({"text_not_equal", test_text_not_equal});
  tests.push_back({"regex_attribute", test_regex_attribute});
  tests.push_back({"contains_attribute", test_contains_attribute});
  tests.push_back({"has_direct_text", test_has_direct_text});
  tests.push_back({"parenthesized_predicates", test_parenthesized_predicates});
}
