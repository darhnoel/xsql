#include <filesystem>
#include <iostream>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "test_harness.h"

#include "xsql/khmer_number.h"
#include "repl/commands/khmer_number_command.h"
#include "repl/commands/registry.h"
#include "repl/core/line_editor.h"
#include "repl/core/repl.h"
#include "repl/plugin_manager.h"

namespace {

struct StreamCapture {
  std::ostream* stream = nullptr;
  std::ostringstream buffer;
  std::streambuf* original = nullptr;

  explicit StreamCapture(std::ostream& target)
      : stream(&target), original(target.rdbuf(buffer.rdbuf())) {}
  ~StreamCapture() {
    if (stream && original) {
      buffer.flush();
      stream->rdbuf(original);
    }
  }

  std::string str() const { return buffer.str(); }
};

void expect_ok(const xsql::khmer_number::Result<std::string>& result,
               const std::string& message) {
  expect_true(result.ok, message + " (error: " + result.error + ")");
}

void expect_round_trip(const std::string& number, const std::string& label) {
  auto words = xsql::khmer_number::number_to_khmer_words(number);
  expect_ok(words, label + " number_to_khmer_words");
  if (!words.ok) {
    return;
  }
  auto back = xsql::khmer_number::khmer_words_to_number(words.value);
  expect_ok(back, label + " khmer_words_to_number");
  if (!back.ok) {
    return;
  }
  expect_true(back.value == number, label + " round-trip mismatch");
}

std::string make_power_of_ten(int exponent) {
  if (exponent < 0) {
    return "0";
  }
  std::string value = "1";
  value.append(static_cast<size_t>(exponent), '0');
  return value;
}

std::optional<std::filesystem::path> find_fixture_path() {
  std::vector<std::filesystem::path> candidates = {
      "tests/fixtures/khmer_number_vectors.tsv",
      "../tests/fixtures/khmer_number_vectors.tsv",
      "../../tests/fixtures/khmer_number_vectors.tsv",
  };
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return std::nullopt;
}

static void test_khmer_digits() {
  std::vector<std::string> expected = {
      "សូន្យ", "មួយ", "ពីរ", "បី", "បួន",
      "ប្រាំ", "ប្រាំមួយ", "ប្រាំពីរ", "ប្រាំបី", "ប្រាំបួន",
  };
  for (size_t i = 0; i < expected.size(); ++i) {
    auto result = xsql::khmer_number::number_to_khmer_words(std::to_string(i));
    expect_ok(result, "digits conversion");
    if (!result.ok) {
      continue;
    }
    expect_true(result.value == expected[i], "digit mapping mismatch");
  }
}

static void test_khmer_scales_boundaries() {
  expect_round_trip("99", "below hundred");
  expect_round_trip("100", "hundred");
  expect_round_trip("101", "hundred plus one");
  expect_round_trip("999", "below thousand");
  expect_round_trip("1000", "thousand");
  expect_round_trip("1001", "thousand plus one");
  expect_round_trip("9999", "below ten-thousand");
  expect_round_trip("10000", "ten-thousand");
  expect_round_trip("10001", "ten-thousand plus one");
  expect_round_trip("99999", "below hundred-thousand");
  expect_round_trip("100000", "hundred-thousand");
  expect_round_trip("100001", "hundred-thousand plus one");
  expect_round_trip("999999", "below million");
  expect_round_trip("1000000", "million");
  expect_round_trip("1000001", "million plus one");
  expect_round_trip("999999999", "below billion");
  expect_round_trip("1000000000", "billion");
  expect_round_trip("1000000001", "billion plus one");
  expect_round_trip("999999999999", "below trillion");
  expect_round_trip("1000000000000", "trillion");
  expect_round_trip("1000000000001", "trillion plus one");
  expect_round_trip(make_power_of_ten(15), "quadrillion");
  expect_round_trip(make_power_of_ten(18), "quintillion");
  expect_round_trip(make_power_of_ten(21), "sextillion");
  expect_round_trip(make_power_of_ten(24), "septillion");
  expect_round_trip(make_power_of_ten(27), "octillion");
  expect_round_trip(make_power_of_ten(30), "nonillion");
  expect_round_trip(make_power_of_ten(33), "decillion");
  expect_round_trip(make_power_of_ten(36), "undecillion");
}

static void test_khmer_decimals_and_negatives() {
  expect_round_trip("0.5", "decimal 0.5");
  expect_round_trip("0.05", "decimal 0.05");
  expect_round_trip("12.30", "decimal 12.30");
  expect_round_trip("100.01", "decimal 100.01");
  expect_round_trip("-1", "negative -1");
  expect_round_trip("-10", "negative -10");
  expect_round_trip("-100.50", "negative -100.50");
}

static void test_khmer_numerals_output() {
  auto result = xsql::khmer_number::number_to_khmer_numerals("12.30");
  expect_ok(result, "numerals 12.30");
  if (result.ok) {
    expect_true(result.value == "១២.៣០", "numerals 12.30 mismatch");
  }

  auto result2 = xsql::khmer_number::number_to_khmer_numerals("-001,234");
  expect_ok(result2, "numerals -001,234");
  if (result2.ok) {
    expect_true(result2.value == "-១២៣៤", "numerals -001,234 mismatch");
  }
}

static void test_khmer_concatenated_input() {
  auto result = xsql::khmer_number::khmer_words_to_number("មួយរយម្ភៃបី");
  expect_ok(result, "concat words 123");
  if (result.ok) {
    expect_true(result.value == "123", "concat words 123 mismatch");
  }

  auto result2 = xsql::khmer_number::khmer_words_to_number("ដប់ពីរ");
  expect_ok(result2, "concat words 12");
  if (result2.ok) {
    expect_true(result2.value == "12", "concat words 12 mismatch");
  }

  auto result3 = xsql::khmer_number::khmer_words_to_number("ដកមួយរយ");
  expect_ok(result3, "concat negative 100");
  if (result3.ok) {
    expect_true(result3.value == "-100", "concat negative mismatch");
  }

  auto result4 = xsql::khmer_number::khmer_words_to_number("សូន្យក្បៀសប្រាំ");
  expect_ok(result4, "concat decimal 0.5");
  if (result4.ok) {
    expect_true(result4.value == "0.5", "concat decimal mismatch");
  }
}

static void test_khmer_round_trip_random() {
  std::mt19937 rng(1337);
  std::uniform_int_distribution<uint64_t> int_dist(0, 999999999999ULL);
  std::uniform_int_distribution<int> dec_len_dist(0, 4);
  std::uniform_int_distribution<int> digit_dist(0, 9);
  std::uniform_int_distribution<int> sign_dist(0, 1);

  for (int i = 0; i < 1000; ++i) {
    uint64_t integer = int_dist(rng);
    int dec_len = dec_len_dist(rng);
    std::string number = std::to_string(integer);
    if (dec_len > 0) {
      number.push_back('.');
      for (int j = 0; j < dec_len; ++j) {
        number.push_back(static_cast<char>('0' + digit_dist(rng)));
      }
    }
    bool negative = sign_dist(rng) == 1;
    if (negative && (integer > 0 || dec_len > 0)) {
      number.insert(number.begin(), '-');
    }
    expect_round_trip(number, "random round-trip");
  }
}

static void test_khmer_golden_vectors() {
  auto fixture = find_fixture_path();
  expect_true(fixture.has_value(), "fixture path should exist");
  if (!fixture.has_value()) {
    return;
  }

  std::ifstream file(*fixture);
  expect_true(file.good(), "fixture file should be readable");
  if (!file.good()) {
    return;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::vector<std::string> cols;
    std::string cell;
    std::istringstream iss(line);
    while (std::getline(iss, cell, '\t')) {
      cols.push_back(cell);
    }
    expect_true(cols.size() == 3, "fixture row should have 3 columns");
    if (cols.size() != 3) {
      continue;
    }
    auto words = xsql::khmer_number::number_to_khmer_words(cols[0]);
    expect_ok(words, "fixture number_to_khmer_words");
    if (words.ok) {
      expect_true(words.value == cols[1], "fixture words mismatch");
    }
    auto back = xsql::khmer_number::khmer_words_to_number(cols[1]);
    expect_ok(back, "fixture khmer_words_to_number");
    if (back.ok) {
      expect_true(back.value == cols[2], "fixture parsed mismatch");
    }
  }
}

static void test_khmer_command_to_words() {
  StreamCapture capture(std::cout);
  xsql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  config.input = "";

  xsql::cli::LineEditor editor(5, "xsql> ", 6);
  std::unordered_map<std::string, xsql::cli::LoadedSource> sources;
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;

  xsql::cli::CommandRegistry registry;
  xsql::cli::PluginManager plugin_manager(registry);
  xsql::cli::CommandContext ctx{
      config,
      editor,
      sources,
      active_alias,
      last_full_output,
      display_full,
      max_rows,
      plugin_manager,
  };

  auto handler = xsql::cli::make_khmer_number_command();
  bool handled = handler(".number_to_khmer 12.30", ctx);
  expect_true(handled, "number_to_khmer should handle command");
  std::string output = capture.str();
  expect_true(output.find("ដប់-ពីរ-ក្បៀស-បី-សូន្យ") != std::string::npos,
              "number_to_khmer output should include Khmer words");
}

static void test_khmer_command_compact() {
  StreamCapture capture(std::cout);
  xsql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  config.input = "";

  xsql::cli::LineEditor editor(5, "xsql> ", 6);
  std::unordered_map<std::string, xsql::cli::LoadedSource> sources;
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;

  xsql::cli::CommandRegistry registry;
  xsql::cli::PluginManager plugin_manager(registry);
  xsql::cli::CommandContext ctx{
      config,
      editor,
      sources,
      active_alias,
      last_full_output,
      display_full,
      max_rows,
      plugin_manager,
  };

  auto handler = xsql::cli::make_khmer_number_command();
  bool handled = handler(".number_to_khmer --compact 12.30", ctx);
  expect_true(handled, "number_to_khmer compact should handle command");
  std::string output = capture.str();
  expect_true(output.find("ដប់ពីរក្បៀសបីសូន្យ") != std::string::npos,
              "number_to_khmer compact output should omit separators");
}

static void test_khmer_command_numerals() {
  StreamCapture capture(std::cout);
  xsql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  config.input = "";

  xsql::cli::LineEditor editor(5, "xsql> ", 6);
  std::unordered_map<std::string, xsql::cli::LoadedSource> sources;
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;

  xsql::cli::CommandRegistry registry;
  xsql::cli::PluginManager plugin_manager(registry);
  xsql::cli::CommandContext ctx{
      config,
      editor,
      sources,
      active_alias,
      last_full_output,
      display_full,
      max_rows,
      plugin_manager,
  };

  auto handler = xsql::cli::make_khmer_number_command();
  bool handled = handler(".number_to_khmer --khmer-digits 12.30", ctx);
  expect_true(handled, "number_to_khmer numerals should handle command");
  std::string output = capture.str();
  expect_true(output.find("១២.៣០") != std::string::npos,
              "number_to_khmer numerals output should include Khmer digits");
}

static void test_khmer_command_to_number_numerals() {
  StreamCapture capture(std::cout);
  xsql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  config.input = "";

  xsql::cli::LineEditor editor(5, "xsql> ", 6);
  std::unordered_map<std::string, xsql::cli::LoadedSource> sources;
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;

  xsql::cli::CommandRegistry registry;
  xsql::cli::PluginManager plugin_manager(registry);
  xsql::cli::CommandContext ctx{
      config,
      editor,
      sources,
      active_alias,
      last_full_output,
      display_full,
      max_rows,
      plugin_manager,
  };

  auto handler = xsql::cli::make_khmer_number_command();
  bool handled = handler(".khmer_to_number --khmer-digits ដប់-ពីរ", ctx);
  expect_true(handled, "khmer_to_number numerals should handle command");
  std::string output = capture.str();
  expect_true(output.find("១២") != std::string::npos,
              "khmer_to_number numerals output should include Khmer digits");
}

}  // namespace

void register_khmer_number_tests(std::vector<TestCase>& tests) {
  tests.push_back({"khmer_number_digits", test_khmer_digits});
  tests.push_back({"khmer_number_scale_boundaries", test_khmer_scales_boundaries});
  tests.push_back({"khmer_number_decimals_negatives", test_khmer_decimals_and_negatives});
  tests.push_back({"khmer_number_numerals_output", test_khmer_numerals_output});
  tests.push_back({"khmer_number_concatenated_input", test_khmer_concatenated_input});
  tests.push_back({"khmer_number_round_trip_random", test_khmer_round_trip_random});
  tests.push_back({"khmer_number_golden_vectors", test_khmer_golden_vectors});
  tests.push_back({"khmer_number_command_to_words", test_khmer_command_to_words});
  tests.push_back({"khmer_number_command_compact", test_khmer_command_compact});
  tests.push_back({"khmer_number_command_numerals", test_khmer_command_numerals});
  tests.push_back({"khmer_number_command_to_number_numerals",
                   test_khmer_command_to_number_numerals});
}
