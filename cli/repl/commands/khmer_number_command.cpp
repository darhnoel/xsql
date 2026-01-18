#include "khmer_number_command.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string_view>

#include "xsql/khmer_number.h"

namespace xsql::cli {

namespace {

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

}  // namespace

CommandHandler make_khmer_number_command() {
  return [](const std::string& line, CommandContext& ctx) -> bool {
    const std::string_view kWordsCmd = ".number_to_khmer";
    const std::string_view kNumberCmd = ".khmer_to_number";
    bool to_words = false;
    std::string_view rest;
    bool compact = false;
    bool numerals = false;
    std::string arg;

    if (line.rfind(kWordsCmd, 0) == 0) {
      to_words = true;
      rest = std::string_view(line).substr(kWordsCmd.size());
    } else if (line.rfind(kNumberCmd, 0) == 0) {
      rest = std::string_view(line).substr(kNumberCmd.size());
    } else {
      return false;
    }

    std::istringstream iss(std::string(trim_ws(rest)));
    std::string token;
    while (iss >> token) {
      if (token == "--khmer-digits" || token == "--numerals") {
        numerals = true;
        continue;
      }
      if (token == "--compact" || token == "--no-sep") {
        compact = true;
        continue;
      }
      if (arg.empty()) {
        arg = token;
      } else {
        if (to_words) {
          arg += token;
        } else {
          arg += " " + token;
        }
      }
    }
    if (arg.empty()) {
      if (to_words) {
        std::cerr << "Usage: .number_to_khmer <number> [--compact] [--khmer-digits]"
                  << std::endl;
      } else {
        std::cerr << "Usage: .khmer_to_number <khmer_text> [--khmer-digits]"
                  << std::endl;
      }
      ctx.editor.reset_render_state();
      return true;
    }

    if (to_words) {
      auto result = numerals
                        ? xsql::khmer_number::number_to_khmer_numerals(arg)
                        : xsql::khmer_number::number_to_khmer_words(arg);
      if (!result.ok) {
        std::cerr << "Error: " << result.error << std::endl;
        ctx.editor.reset_render_state();
        return true;
      }
      if (compact && !numerals) {
        result.value.erase(
            std::remove(result.value.begin(), result.value.end(), '-'),
            result.value.end());
      }
      std::cout << result.value << std::endl;
      ctx.editor.reset_render_state();
      return true;
    }

    auto result = xsql::khmer_number::khmer_words_to_number(arg);
    if (!result.ok) {
      std::cerr << "Error: " << result.error << std::endl;
      ctx.editor.reset_render_state();
      return true;
    }
    if (numerals) {
      auto numerals_result =
          xsql::khmer_number::number_to_khmer_numerals(result.value);
      if (!numerals_result.ok) {
        std::cerr << "Error: " << numerals_result.error << std::endl;
        ctx.editor.reset_render_state();
        return true;
      }
      std::cout << numerals_result.value << std::endl;
      ctx.editor.reset_render_state();
      return true;
    }
    std::cout << result.value << std::endl;
    ctx.editor.reset_render_state();
    return true;
  };
}

}  // namespace xsql::cli
