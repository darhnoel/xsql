#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "test_harness.h"

void register_query_basic_tests(std::vector<TestCase>& tests);
void register_source_alias_tests(std::vector<TestCase>& tests);
void register_shorthand_tests(std::vector<TestCase>& tests);
void register_projection_tests(std::vector<TestCase>& tests);
void register_function_tests(std::vector<TestCase>& tests);
void register_axis_tests(std::vector<TestCase>& tests);
void register_predicate_tests(std::vector<TestCase>& tests);
void register_order_by_tests(std::vector<TestCase>& tests);
void register_duckbox_tests(std::vector<TestCase>& tests);
void register_export_tests(std::vector<TestCase>& tests);
void register_repl_tests(std::vector<TestCase>& tests);
void register_malformed_html_tests(std::vector<TestCase>& tests);
void register_fragments_tests(std::vector<TestCase>& tests);
void register_guardrails_tests(std::vector<TestCase>& tests);
void register_meta_command_tests(std::vector<TestCase>& tests);
void register_cli_utils_tests(std::vector<TestCase>& tests);

int main(int argc, char** argv) {
  std::vector<TestCase> tests;
  tests.reserve(64);
  register_query_basic_tests(tests);
  register_source_alias_tests(tests);
  register_shorthand_tests(tests);
  register_projection_tests(tests);
  register_function_tests(tests);
  register_axis_tests(tests);
  register_predicate_tests(tests);
  register_order_by_tests(tests);
  register_duckbox_tests(tests);
  register_export_tests(tests);
  register_repl_tests(tests);
  register_malformed_html_tests(tests);
  register_fragments_tests(tests);
  register_guardrails_tests(tests);
  register_meta_command_tests(tests);
  register_cli_utils_tests(tests);

  if (argc > 1) {
    std::string target = argv[1];
    for (const auto& test : tests) {
      if (target == test.name) {
        int failures = run_test(test);
        return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
      }
    }
    std::cerr << "Unknown test: " << target << std::endl;
    std::cerr << "Available tests:" << std::endl;
    for (const auto& test : tests) {
      std::cerr << "  " << test.name << std::endl;
    }
    return EXIT_FAILURE;
  }

  return run_all_tests(tests);
}
