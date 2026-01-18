#include "xsql/khmer_number.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/multiprecision/cpp_int.hpp>

namespace xsql::khmer_number {

namespace {

using boost::multiprecision::cpp_int;

constexpr std::array<std::string_view, 10> kDigitWords = {
    "សូន្យ", "មួយ", "ពីរ", "បី", "បួន",
    "ប្រាំ", "ប្រាំមួយ", "ប្រាំពីរ", "ប្រាំបី", "ប្រាំបួន"};

constexpr std::array<std::string_view, 10> kDigitNumerals = {
    "០", "១", "២", "៣", "៤", "៥", "៦", "៧", "៨", "៩"};

constexpr std::array<std::string_view, 10> kTensWords = {
    "", "", "ម្ភៃ", "សាមសិប", "សែសិប",
    "ហាសិប", "ហុកសិប", "ចិតសិប", "ប៉ែតសិប", "កៅសិប"};

constexpr std::string_view kDecimalMarker = "ក្បៀស";
constexpr std::string_view kNegativeMarker = "ដក";

struct ScaleInfo {
  std::string_view token;
  unsigned int exponent;
  bool large;
};

constexpr std::array<ScaleInfo, 16> kScales = {{
    {"អាន់ដេស៊ីលាន", 36, true},
    {"ដេស៊ីលាន", 33, true},
    {"ណូនីលាន", 30, true},
    {"អុកទីលាន", 27, true},
    {"សិបទីលាន", 24, true},
    {"សិចទីលាន", 21, true},
    {"គ្វីនទីលាន", 18, true},
    {"ក្វាឌ្រីលាន", 15, true},
    {"ទ្រីលាន", 12, true},
    {"ប៊ីលាន", 9, true},
    {"លាន", 6, true},
    {"សែន", 5, true},
    {"ម៉ឺន", 4, true},
    {"ពាន់", 3, true},
    {"រយ", 2, false},
    {"ដប់", 1, false},
}};

const std::unordered_map<std::string_view, int>& digit_map() {
  static const std::unordered_map<std::string_view, int> map = []() {
    std::unordered_map<std::string_view, int> values;
    for (size_t i = 0; i < kDigitWords.size(); ++i) {
      values.emplace(kDigitWords[i], static_cast<int>(i));
    }
    return values;
  }();
  return map;
}

const std::unordered_map<std::string_view, int>& tens_map() {
  static const std::unordered_map<std::string_view, int> map = []() {
    std::unordered_map<std::string_view, int> values;
    for (size_t i = 0; i < kTensWords.size(); ++i) {
      if (!kTensWords[i].empty()) {
        values.emplace(kTensWords[i], static_cast<int>(i));
      }
    }
    return values;
  }();
  return map;
}

const std::unordered_map<std::string_view, ScaleInfo>& scale_map() {
  static const std::unordered_map<std::string_view, ScaleInfo> map = []() {
    std::unordered_map<std::string_view, ScaleInfo> values;
    for (const auto& scale : kScales) {
      values.emplace(scale.token, scale);
    }
    return values;
  }();
  return map;
}

const std::vector<std::string_view>& known_tokens() {
  static const std::vector<std::string_view> tokens = []() {
    std::vector<std::string_view> values;
    values.reserve(kDigitWords.size() + kTensWords.size() + kScales.size() + 2);
    for (const auto& word : kDigitWords) {
      values.push_back(word);
    }
    for (const auto& word : kTensWords) {
      if (!word.empty()) {
        values.push_back(word);
      }
    }
    for (const auto& scale : kScales) {
      values.push_back(scale.token);
    }
    values.push_back(kDecimalMarker);
    values.push_back(kNegativeMarker);
    std::sort(values.begin(), values.end(),
              [](std::string_view a, std::string_view b) {
                if (a.size() != b.size()) {
                  return a.size() > b.size();
                }
                return a < b;
              });
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
  }();
  return tokens;
}

int edit_distance_bytes(std::string_view a, std::string_view b, int max_dist) {
  if (a.empty()) return static_cast<int>(b.size());
  if (b.empty()) return static_cast<int>(a.size());
  int size_diff = static_cast<int>(a.size() > b.size() ? a.size() - b.size() : b.size() - a.size());
  if (max_dist >= 0 && size_diff > max_dist) {
    return max_dist + 1;
  }
  std::vector<int> prev(b.size() + 1);
  std::vector<int> cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) {
    prev[j] = static_cast<int>(j);
  }
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = static_cast<int>(i);
    int row_best = cur[0];
    for (size_t j = 1; j <= b.size(); ++j) {
      int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      int insert_cost = cur[j - 1] + 1;
      int delete_cost = prev[j] + 1;
      int replace_cost = prev[j - 1] + cost;
      int best = insert_cost;
      if (delete_cost < best) best = delete_cost;
      if (replace_cost < best) best = replace_cost;
      cur[j] = best;
      if (best < row_best) row_best = best;
    }
    if (max_dist >= 0 && row_best > max_dist) {
      return max_dist + 1;
    }
    prev.swap(cur);
  }
  return prev[b.size()];
}

bool is_space(char c) {
  return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string trim_ws(std::string_view input) {
  size_t start = 0;
  while (start < input.size() && is_space(input[start])) {
    ++start;
  }
  size_t end = input.size();
  while (end > start && is_space(input[end - 1])) {
    --end;
  }
  return std::string(input.substr(start, end - start));
}

cpp_int pow10(unsigned int exponent) {
  cpp_int value = 1;
  for (unsigned int i = 0; i < exponent; ++i) {
    value *= 10;
  }
  return value;
}

Result<std::string> convert_integer(cpp_int value) {
  if (value < 0) {
    return Result<std::string>::failure("Negative values require a sign prefix.");
  }
  if (value == 0) {
    return Result<std::string>::success(std::string(kDigitWords[0]));
  }

  std::string out;
  for (const auto& scale : kScales) {
    cpp_int scale_value = pow10(scale.exponent);
    if (value < scale_value) {
      continue;
    }
    cpp_int count = value / scale_value;
    if (count > 0) {
      if (scale.exponent == 1) {
        int count_value = count.convert_to<int>();
        if (count_value == 1) {
          if (!out.empty()) {
            out += "-";
          }
          out += scale.token;
          value -= count * scale_value;
          continue;
        }
        if (count_value >= 2 && count_value <= 9) {
          if (!out.empty()) {
            out += "-";
          }
          out += kTensWords[static_cast<size_t>(count_value)];
          value -= count * scale_value;
          continue;
        }
      }
      auto count_words = convert_integer(count);
      if (!count_words.ok) {
        return count_words;
      }
      if (!out.empty()) {
        out += "-";
      }
      out += count_words.value;
      out += "-";
      out += scale.token;
      value -= count * scale_value;
    }
  }
  if (value > 0) {
    int digit = static_cast<int>(value);
    if (!out.empty()) {
      out += "-";
    }
    out += kDigitWords[static_cast<size_t>(digit)];
  }
  return Result<std::string>::success(out);
}

bool all_digits(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  for (char c : text) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

Result<cpp_int> parse_digits_to_int(std::string_view digits) {
  cpp_int value = 0;
  for (char c : digits) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return Result<cpp_int>::failure("Expected digits only in number.");
    }
    value *= 10;
    value += static_cast<int>(c - '0');
  }
  return Result<cpp_int>::success(value);
}

struct NumberParts {
  bool negative = false;
  std::string integer_digits;
  std::string decimal_digits;
  bool has_decimal = false;
};

Result<NumberParts> parse_number(std::string_view input) {
  std::string trimmed = trim_ws(input);
  if (trimmed.empty()) {
    return Result<NumberParts>::failure("Empty input.");
  }

  NumberParts parts;
  if (trimmed[0] == '+' || trimmed[0] == '-') {
    parts.negative = trimmed[0] == '-';
    trimmed.erase(trimmed.begin());
  }
  if (trimmed.empty()) {
    return Result<NumberParts>::failure("Missing digits after sign.");
  }

  size_t dot = trimmed.find('.');
  if (dot != std::string::npos && trimmed.find('.', dot + 1) != std::string::npos) {
    return Result<NumberParts>::failure("Multiple decimal points are not allowed.");
  }

  std::string_view integer_part = trimmed;
  std::string_view decimal_part;
  if (dot != std::string::npos) {
    integer_part = std::string_view(trimmed).substr(0, dot);
    decimal_part = std::string_view(trimmed).substr(dot + 1);
    parts.has_decimal = true;
  }

  std::string digits;
  for (char c : integer_part) {
    if (std::isdigit(static_cast<unsigned char>(c))) {
      digits.push_back(c);
      continue;
    }
    if (c == ',' || c == '_') {
      continue;
    }
    return Result<NumberParts>::failure("Invalid character in integer part.");
  }

  if (parts.has_decimal) {
    if (decimal_part.empty()) {
      return Result<NumberParts>::failure("Decimal point requires digits after it.");
    }
    if (!all_digits(decimal_part)) {
      return Result<NumberParts>::failure("Invalid character in decimal part.");
    }
    parts.decimal_digits = std::string(decimal_part);
  }

  if (digits.empty()) {
    if (parts.has_decimal) {
      digits = "0";
    } else {
      return Result<NumberParts>::failure("No digits in input.");
    }
  }

  size_t first_nonzero = digits.find_first_not_of('0');
  if (first_nonzero == std::string::npos) {
    parts.integer_digits = "0";
  } else {
    parts.integer_digits = digits.substr(first_nonzero);
  }

  return Result<NumberParts>::success(parts);
}

Result<std::vector<std::string>> segment_khmer_number_words(std::string_view text) {
  std::vector<std::string> tokens;
  std::string current;
  std::string error;
  auto flush_segment = [&]() -> bool {
    if (current.empty()) {
      return true;
    }
    size_t pos = 0;
    const auto& dictionary = known_tokens();
    while (pos < current.size()) {
      bool matched = false;
      for (const auto& token : dictionary) {
        if (token.empty()) {
          continue;
        }
        if (current.compare(pos, token.size(), token) == 0) {
          tokens.emplace_back(token);
          pos += token.size();
          matched = true;
          break;
        }
      }
      if (!matched) {
        std::string_view remaining = std::string_view(current).substr(pos);
        size_t snippet_len = std::min<size_t>(remaining.size(), 24);
        std::string snippet = std::string(remaining.substr(0, snippet_len));
        std::string_view best_token;
        int best_dist = 3;
        const auto& dictionary = known_tokens();
        for (const auto& token : dictionary) {
          size_t cand_len = std::min(remaining.size(), token.size());
          if (cand_len == 0) {
            continue;
          }
          std::string_view candidate = remaining.substr(0, cand_len);
          int dist = edit_distance_bytes(token, candidate, best_dist - 1);
          if (dist < best_dist) {
            best_dist = dist;
            best_token = token;
          }
        }
        error = "Unknown token in Khmer input near '" + snippet + "'.";
        if (best_dist <= 2 && !best_token.empty()) {
          error += " Did you mean '" + std::string(best_token) + "'?";
        }
        return false;
      }
    }
    current.clear();
    return true;
  };

  for (char c : text) {
    if (c == '-' || is_space(c)) {
      if (!flush_segment()) {
        return Result<std::vector<std::string>>::failure(error);
      }
      continue;
    }
    current.push_back(c);
  }
  if (!flush_segment()) {
    return Result<std::vector<std::string>>::failure(error);
  }
  return Result<std::vector<std::string>>::success(std::move(tokens));
}

std::string digits_to_khmer_numerals(std::string_view digits) {
  std::string out;
  for (char c : digits) {
    if (c < '0' || c > '9') {
      continue;
    }
    out += kDigitNumerals[static_cast<size_t>(c - '0')];
  }
  return out;
}

Result<cpp_int> parse_integer_tokens(const std::vector<std::string>& tokens,
                                     size_t start,
                                     size_t end) {
  if (start >= end) {
    return Result<cpp_int>::failure("Missing integer tokens.");
  }

  if (end - start == 1 && tokens[start] == kDigitWords[0]) {
    return Result<cpp_int>::success(cpp_int(0));
  }

  cpp_int total = 0;
  cpp_int group_total = 0;
  int pending_digit = -1;
  int last_small_exponent = 3;
  int last_large_exponent = 100;

  for (size_t i = start; i < end; ++i) {
    std::string_view token = tokens[i];
    auto digit_it = digit_map().find(token);
    if (digit_it != digit_map().end()) {
      if (digit_it->second == 0) {
        return Result<cpp_int>::failure("Zero can only appear alone in the integer part.");
      }
      if (pending_digit != -1) {
        return Result<cpp_int>::failure("Unexpected digit ordering.");
      }
      pending_digit = digit_it->second;
      continue;
    }

    auto tens_it = tens_map().find(token);
    if (tens_it != tens_map().end()) {
      if (pending_digit != -1) {
        return Result<cpp_int>::failure("Unexpected digit before tens token.");
      }
      if (1 >= last_small_exponent) {
        return Result<cpp_int>::failure("Tens tokens must descend.");
      }
      group_total += cpp_int(tens_it->second) * pow10(1);
      last_small_exponent = 1;
      continue;
    }

    auto scale_it = scale_map().find(token);
    if (scale_it == scale_map().end()) {
      return Result<cpp_int>::failure("Unknown token in integer part.");
    }

    const ScaleInfo& scale = scale_it->second;
    if (scale.large) {
      if (scale.exponent >= last_large_exponent) {
        return Result<cpp_int>::failure("Scale tokens must appear in descending order.");
      }
      cpp_int segment = group_total;
      if (pending_digit != -1) {
        segment += pending_digit;
      }
      if (segment == 0) {
        return Result<cpp_int>::failure("Scale token missing a leading value.");
      }
      total += segment * pow10(scale.exponent);
      group_total = 0;
      pending_digit = -1;
      last_small_exponent = 3;
      last_large_exponent = static_cast<int>(scale.exponent);
    } else {
      if (pending_digit == -1) {
        if (scale.exponent == 1) {
          pending_digit = 1;
        } else {
          return Result<cpp_int>::failure("Small scale token missing a leading digit.");
        }
      }
      if (static_cast<int>(scale.exponent) >= last_small_exponent) {
        return Result<cpp_int>::failure("Small scale tokens must descend.");
      }
      group_total += cpp_int(pending_digit) * pow10(scale.exponent);
      pending_digit = -1;
      last_small_exponent = static_cast<int>(scale.exponent);
    }
  }

  cpp_int remainder = group_total;
  if (pending_digit != -1) {
    remainder += pending_digit;
  }
  total += remainder;

  return Result<cpp_int>::success(total);
}

}  // namespace

Result<std::string> integer_to_khmer_words(std::string_view integer_string) {
  std::string trimmed = trim_ws(integer_string);
  if (trimmed.empty()) {
    return Result<std::string>::failure("Empty integer input.");
  }
  if (!all_digits(trimmed)) {
    return Result<std::string>::failure("Integer input must be digits only.");
  }
  auto parsed = parse_digits_to_int(trimmed);
  if (!parsed.ok) {
    return Result<std::string>::failure(parsed.error);
  }
  return convert_integer(parsed.value);
}

Result<std::string> decimal_to_khmer_words(std::string_view decimal_digits_string) {
  std::string trimmed = trim_ws(decimal_digits_string);
  if (!all_digits(trimmed)) {
    return Result<std::string>::failure("Decimal input must be digits only.");
  }
  std::string out;
  for (char c : trimmed) {
    int digit = c - '0';
    if (!out.empty()) {
      out += "-";
    }
    out += kDigitWords[static_cast<size_t>(digit)];
  }
  return Result<std::string>::success(out);
}

Result<std::string> number_to_khmer_words(std::string_view number_string) {
  auto parts = parse_number(number_string);
  if (!parts.ok) {
    return Result<std::string>::failure(parts.error);
  }

  auto integer_words = integer_to_khmer_words(parts.value.integer_digits);
  if (!integer_words.ok) {
    return Result<std::string>::failure(integer_words.error);
  }

  std::string out = integer_words.value;
  if (parts.value.has_decimal) {
    auto decimal_words = decimal_to_khmer_words(parts.value.decimal_digits);
    if (!decimal_words.ok) {
      return Result<std::string>::failure(decimal_words.error);
    }
    out += "-";
    out += kDecimalMarker;
    out += "-";
    out += decimal_words.value;
  }

  if (parts.value.negative) {
    out = std::string(kNegativeMarker) + "-" + out;
  }

  return Result<std::string>::success(out);
}

Result<std::string> number_to_khmer_numerals(std::string_view number_string) {
  auto parts = parse_number(number_string);
  if (!parts.ok) {
    return Result<std::string>::failure(parts.error);
  }

  std::string out = digits_to_khmer_numerals(parts.value.integer_digits);
  if (parts.value.has_decimal) {
    out += ".";
    out += digits_to_khmer_numerals(parts.value.decimal_digits);
  }
  if (parts.value.negative) {
    out.insert(out.begin(), '-');
  }
  return Result<std::string>::success(out);
}

Result<std::string> khmer_words_to_number(std::string_view text) {
  std::string trimmed = trim_ws(text);
  if (trimmed.empty()) {
    return Result<std::string>::failure("Empty Khmer input.");
  }

  auto token_result = segment_khmer_number_words(trimmed);
  if (!token_result.ok) {
    return Result<std::string>::failure(token_result.error);
  }
  std::vector<std::string> tokens = std::move(token_result.value);
  if (tokens.empty()) {
    return Result<std::string>::failure("No tokens found in input.");
  }

  bool negative = false;
  size_t start = 0;
  if (tokens[0] == kNegativeMarker) {
    negative = true;
    start = 1;
  }
  if (start >= tokens.size()) {
    return Result<std::string>::failure("Missing number after negative marker.");
  }

  size_t decimal_index = tokens.size();
  for (size_t i = start; i < tokens.size(); ++i) {
    if (tokens[i] == kDecimalMarker) {
      if (decimal_index != tokens.size()) {
        return Result<std::string>::failure("Multiple decimal markers found.");
      }
      decimal_index = i;
    }
  }

  size_t int_end = decimal_index;
  size_t dec_start = decimal_index + 1;
  if (decimal_index == tokens.size()) {
    int_end = tokens.size();
    dec_start = tokens.size();
  }

  auto integer_value = parse_integer_tokens(tokens, start, int_end);
  if (!integer_value.ok) {
    return Result<std::string>::failure(integer_value.error);
  }

  std::string decimal_digits;
  if (decimal_index != tokens.size()) {
    if (dec_start >= tokens.size()) {
      return Result<std::string>::failure("Decimal marker requires digits after it.");
    }
    for (size_t i = dec_start; i < tokens.size(); ++i) {
      std::string_view token = tokens[i];
      auto digit_it = digit_map().find(token);
      if (digit_it == digit_map().end()) {
        return Result<std::string>::failure("Invalid token in decimal part.");
      }
      decimal_digits.push_back(static_cast<char>('0' + digit_it->second));
    }
  }

  std::string out = integer_value.value.convert_to<std::string>();
  if (!decimal_digits.empty()) {
    out += ".";
    out += decimal_digits;
  }
  if (negative) {
    out = "-" + out;
  }
  return Result<std::string>::success(out);
}

}  // namespace xsql::khmer_number
