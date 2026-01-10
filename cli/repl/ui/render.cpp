#include "render.h"

#include <cctype>
#include <iostream>
#include <unordered_set>

#include "ui/color.h"
#include "repl/input/text_util.h"

namespace xsql::cli {
namespace {

bool is_sql_keyword(const std::string& word) {
  static const std::unordered_set<std::string> keywords = {
      "select", "from", "where", "and", "or", "in", "limit", "order", "by",
      "asc", "desc", "to", "list", "csv", "parquet", "count", "summarize", "exclude",
      "raw", "fragments", "contains", "all", "any", "has_direct_text", "sibling_pos", "is", "null"
  };
  std::string lower;
  lower.reserve(word.size());
  for (char c : word) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return keywords.find(lower) != keywords.end();
}

}  // namespace

int compute_render_lines(const std::string& buffer,
                         const std::string& prompt,
                         size_t prompt_len,
                         const std::string& cont_prompt,
                         size_t cont_prompt_len,
                         int width) {
  int line = 0;
  int col = static_cast<int>(prompt_len);
  size_t i = 0;
  while (i < buffer.size()) {
    if (buffer[i] == '\n') {
      line++;
      col = static_cast<int>(cont_prompt_len);
      ++i;
      continue;
    }
    size_t bytes = 0;
    uint32_t cp = decode_utf8(buffer, i, &bytes);
    int width_cp = display_width(cp);
    if (col + width_cp > width) {
      line++;
      col = 0;
    }
    col += width_cp;
    if (col >= width) {
      line++;
      col = 0;
    }
    i += bytes ? bytes : 1;
  }
  return line + 1;
}

int compute_cursor_line(const std::string& buffer,
                        size_t cursor,
                        const std::string& prompt,
                        size_t prompt_len,
                        const std::string& cont_prompt,
                        size_t cont_prompt_len,
                        int width) {
  int line = 0;
  int col = static_cast<int>(prompt_len);
  size_t i = 0;
  while (i < buffer.size()) {
    if (i == cursor) {
      return line;
    }
    if (buffer[i] == '\n') {
      line++;
      col = static_cast<int>(cont_prompt_len);
      ++i;
      continue;
    }
    size_t bytes = 0;
    uint32_t cp = decode_utf8(buffer, i, &bytes);
    int width_cp = display_width(cp);
    if (col + width_cp > width) {
      line++;
      col = 0;
    }
    col += width_cp;
    if (col >= width) {
      line++;
      col = 0;
    }
    i += bytes ? bytes : 1;
  }
  return line;
}

void render_buffer(const std::string& buffer,
                   bool keyword_color,
                   const std::string& cont_prompt) {
  bool in_single = false;
  bool in_double = false;
  bool command_line = false;
  bool at_line_start = true;
  bool line_has_prefix = false;
  std::string last_word;
  size_t i = 0;
  while (i < buffer.size()) {
    char c = buffer[i];
    if (c == '\n') {
      std::cout << "\n" << cont_prompt;
      command_line = false;
      at_line_start = true;
      line_has_prefix = false;
      last_word.clear();
      ++i;
      continue;
    }
    if (at_line_start) {
      if (!line_has_prefix && (c == '.' || c == ':')) {
        command_line = true;
        line_has_prefix = true;
      } else if (!std::isspace(static_cast<unsigned char>(c))) {
        line_has_prefix = true;
      }
      if (!std::isspace(static_cast<unsigned char>(c))) {
        at_line_start = false;
      }
    }
    if (!in_double && c == '\'') {
      in_single = !in_single;
      std::cout << c;
      ++i;
      continue;
    }
    if (!in_single && c == '"') {
      in_double = !in_double;
      std::cout << c;
      ++i;
      continue;
    }
    if (!in_single && !in_double && std::isalpha(static_cast<unsigned char>(c))) {
      size_t start = i;
      while (i < buffer.size()) {
        char wc = buffer[i];
        if (std::isalnum(static_cast<unsigned char>(wc)) || wc == '_') {
          ++i;
          continue;
        }
        break;
      }
      std::string word = buffer.substr(start, i - start);
      std::string lower_word;
      lower_word.reserve(word.size());
      for (char wc : word) {
        lower_word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(wc))));
      }
      bool highlight_table = (lower_word == "table" && last_word == "to");
      if (keyword_color && !command_line &&
          (highlight_table || is_sql_keyword(word))) {
        std::cout << kColor.cyan << word << kColor.reset;
      } else {
        std::cout << word;
      }
      last_word = lower_word;
      continue;
    }
    std::cout << c;
    ++i;
  }
}

}  // namespace xsql::cli
