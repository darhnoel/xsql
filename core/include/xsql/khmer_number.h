#pragma once

#include <string>
#include <string_view>

namespace xsql::khmer_number {

template <typename T>
struct Result {
  T value{};
  std::string error;
  bool ok = false;

  static Result success(T value) {
    return {std::move(value), "", true};
  }

  static Result failure(std::string error) {
    return {{}, std::move(error), false};
  }
};

Result<std::string> integer_to_khmer_words(std::string_view integer_string);
Result<std::string> decimal_to_khmer_words(std::string_view decimal_digits_string);
Result<std::string> number_to_khmer_words(std::string_view number_string);
Result<std::string> number_to_khmer_numerals(std::string_view number_string);
Result<std::string> khmer_words_to_number(std::string_view text);

}  // namespace xsql::khmer_number
