#include "line_editor.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <unordered_set>

#include "../ui/color.h"

namespace xsql::cli {

namespace {

/// Manages raw terminal mode and bracketed paste for interactive editing.
/// MUST restore terminal settings on destruction to avoid leaving the shell broken.
/// Inputs are implicit terminal state; side effects include ioctl and escape writes.
class TermiosGuard {
 public:
  /// Enables raw mode to read individual keystrokes for line editing.
  /// MUST succeed entirely or fall back without partial terminal changes.
  /// Inputs are current termios settings; side effects include terminal updates.
  TermiosGuard() : ok_(false) {
    if (tcgetattr(STDIN_FILENO, &orig_) != 0) return;
    termios raw = orig_;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return;
    // WHY: bracketed paste prevents pasted content from triggering editor controls.
    std::cout << "\033[?2004h" << std::flush;
    ok_ = true;
  }
  /// Restores terminal settings and disables bracketed paste.
  /// MUST run even on early returns to avoid leaving raw mode enabled.
  /// Inputs are stored termios state; side effects include terminal resets.
  ~TermiosGuard() {
    if (ok_) {
      std::cout << "\033[?2004l" << std::flush;
      tcsetattr(STDIN_FILENO, TCSANOW, &orig_);
    }
  }
  /// Reports whether raw mode was successfully enabled.
  /// MUST be checked before relying on raw input behavior.
  /// Inputs are none; outputs are boolean with no side effects.
  bool ok() const { return ok_; }

 private:
  termios orig_{};
  bool ok_;
};

/// Detects terminal width for cursor positioning and line wrapping.
/// MUST fall back to a sane default when detection fails.
/// Inputs are terminal state; outputs are column count with no side effects.
int terminal_width() {
  winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    return ws.ws_col;
  }
  return 80;
}

}  // namespace

/// Provides keyword and command completion for the REPL editor.
/// MUST preserve user casing conventions and MUST not mutate unrelated text.
/// Inputs are buffer/cursor; outputs are edits/suggestions with no side effects.
class LineEditor::AutoCompleter {
 public:
  /// Initializes the completion dictionaries for SQL keywords and commands.
  /// MUST keep lists in sync with supported grammar and REPL commands.
  /// Inputs are none; side effects are internal list initialization only.
  AutoCompleter() {
    keywords_ = {
        "select", "from", "where", "and", "or", "in", "limit", "to", "list", "table",
        "csv", "parquet", "count", "summarize", "order", "by", "asc", "desc", "document", "doc",
        "attributes", "tag", "text", "parent", "child", "ancestor", "descendant",
        "parent_id", "inner_html", "trim", "is", "null"
    };
    commands_ = {
        ".help", ".load", ".mode", ".display_mode", ".max_rows", ".summarize", ".quit", ".q",
        ":help", ":load", ":quit", ":exit"
    };
  }

  /// Attempts to complete the word under the cursor and gather suggestions.
  /// MUST only modify the buffer on unambiguous completions.
  /// Inputs are buffer/cursor; outputs are updated buffer/cursor and suggestions.
  bool complete(std::string& buffer, size_t& cursor, std::vector<std::string>& suggestions) const {
    if (cursor > buffer.size()) cursor = buffer.size();
    size_t start = cursor;
    while (start > 0 && is_word_char(buffer[start - 1])) {
      --start;
    }
    if (start == cursor) return false;
    std::string word = buffer.substr(start, cursor - start);
    if (word.empty()) return false;
    bool is_cmd = word[0] == '.' || word[0] == ':';
    const auto& list = is_cmd ? commands_ : keywords_;
    std::string prefix = to_lower(word);
    std::vector<std::string> matches;
    for (const auto& cand : list) {
      if (starts_with(to_lower(cand), prefix)) {
        matches.push_back(cand);
      }
    }
    if (matches.empty()) return false;
    std::string common = longest_common_prefix_lower(matches);
    std::string cased_common = apply_case(word, common);
    if (cased_common.size() > word.size() || matches.size() == 1) {
      buffer.replace(start, cursor - start, cased_common);
      cursor = start + cased_common.size();
      return true;
    }
    suggestions.reserve(matches.size());
    for (const auto& cand : matches) {
      suggestions.push_back(apply_case(word, cand));
    }
    return false;
  }

 private:
    /// Defines which characters belong to a completion token.
    /// MUST match parser expectations to avoid broken completions.
    /// Inputs are a character; outputs are boolean with no side effects.
    static bool is_word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == ':';
  }

    /// Normalizes a token for case-insensitive matching.
    /// MUST preserve byte order and MUST not alter non-ASCII bytes.
    /// Inputs are raw strings; outputs are lowercase strings.
    static std::string to_lower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
  }

    /// Checks prefix matching used for completion filtering.
    /// MUST be exact on normalized input to keep suggestions consistent.
    /// Inputs are strings; outputs are boolean with no side effects.
    static bool starts_with(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
  }

    /// Finds the shared lowercase prefix to decide automatic completion.
    /// MUST return a prefix common to all values or empty when none.
    /// Inputs are candidate strings; outputs are the common prefix.
    static std::string longest_common_prefix_lower(const std::vector<std::string>& values) {
    if (values.empty()) return "";
    std::string prefix = to_lower(values[0]);
    for (size_t i = 1; i < values.size(); ++i) {
      size_t j = 0;
      std::string lower = to_lower(values[i]);
      while (j < prefix.size() && j < lower.size() && prefix[j] == lower[j]) {
        ++j;
      }
      prefix.resize(j);
      if (prefix.empty()) break;
    }
    return prefix;
  }

    /// Applies a casing pattern to the suggested completion.
    /// MUST preserve the user's casing intent without altering semantic text.
    /// Inputs are pattern/value; outputs are cased suggestion strings.
    static std::string apply_case(const std::string& pattern, const std::string& value) {
    if (pattern.empty()) return value;
    bool all_upper = true;
    bool all_lower = true;
    for (char c : pattern) {
      if (std::isalpha(static_cast<unsigned char>(c))) {
        if (std::islower(static_cast<unsigned char>(c))) {
          all_upper = false;
        } else if (std::isupper(static_cast<unsigned char>(c))) {
          all_lower = false;
        }
      }
    }
    if (all_upper) {
      std::string out;
      out.reserve(value.size());
      for (char c : value) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
      }
      return out;
    }
    if (all_lower) {
      return to_lower(value);
    }
    bool capitalized = std::isupper(static_cast<unsigned char>(pattern[0]));
    for (size_t i = 1; i < pattern.size(); ++i) {
      if (std::isalpha(static_cast<unsigned char>(pattern[i])) &&
          std::isupper(static_cast<unsigned char>(pattern[i]))) {
        capitalized = false;
        break;
      }
    }
    if (capitalized) {
      std::string out = to_lower(value);
      out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
      return out;
    }
    return value;
  }

  std::vector<std::string> keywords_;
  std::vector<std::string> commands_;
};

LineEditor::LineEditor(size_t max_history, std::string prompt, size_t prompt_len)
    : max_history_(max_history),
      prompt_(std::move(prompt)),
      prompt_len_(prompt_len),
      cont_prompt_(""),
      cont_prompt_len_(0),
      completer_(std::make_unique<AutoCompleter>()) {}

LineEditor::~LineEditor() = default;

void LineEditor::set_prompt(std::string prompt, size_t prompt_len) {
  prompt_ = std::move(prompt);
  prompt_len_ = prompt_len;
}

void LineEditor::set_cont_prompt(std::string prompt, size_t prompt_len) {
  cont_prompt_ = std::move(prompt);
  cont_prompt_len_ = prompt_len;
}

void LineEditor::set_keyword_color(bool enabled) {
  keyword_color_ = enabled;
}

void LineEditor::reset_render_state() {
  last_render_lines_ = 1;
  last_cursor_line_ = 0;
}

bool LineEditor::read_line(std::string& out, const std::string& initial) {
  out.clear();
  if (!isatty(fileno(stdin))) {
    return static_cast<bool>(std::getline(std::cin, out));
  }

  TermiosGuard guard;
  if (!guard.ok()) {
    return static_cast<bool>(std::getline(std::cin, out));
  }

  std::string buffer = initial;
  size_t cursor = buffer.size();
  history_index_ = history_.size();
  redraw_line(buffer, cursor);

  bool paste_mode = false;
  std::string paste_buffer;
  while (true) {
    char c = 0;
    ssize_t n = ::read(STDIN_FILENO, &c, 1);
    if (n <= 0) return false;

    if (!paste_mode && (c == '\n' || c == '\r')) {
      bool is_command = !buffer.empty() && (buffer[0] == '.' || buffer[0] == ':');
      if (!buffer.empty() && buffer.find(';') == std::string::npos && !is_command) {
        buffer.insert(buffer.begin() + static_cast<long>(cursor), '\n');
        ++cursor;
        redraw_line(buffer, cursor);
        continue;
      }
      std::cout << std::endl;
      out = buffer;
      return true;
    }

    if (c == 127 || c == 8) {
      if (cursor > 0) {
        buffer.erase(cursor - 1, 1);
        --cursor;
        redraw_line(buffer, cursor);
      }
      continue;
    }

    if (c == 12) {
      std::cout << "\033[2J\033[H" << std::flush;
      redraw_line(buffer, cursor);
      continue;
    }

    if (c == 9) {
      std::vector<std::string> suggestions;
      bool changed = completer_->complete(buffer, cursor, suggestions);
      if (!suggestions.empty() && !changed) {
        std::cout << "\n";
        for (size_t i = 0; i < suggestions.size(); ++i) {
          if (i > 0) std::cout << " ";
          std::cout << suggestions[i];
        }
        std::cout << "\n";
      }
      redraw_line(buffer, cursor);
      continue;
    }

    if (c == 27) {
      char seq[6] = {0, 0, 0, 0, 0, 0};
      if (::read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
      if (::read(STDIN_FILENO, &seq[1], 1) <= 0) continue;
      if (seq[0] == '[') {
        if (seq[1] == '2') {
          if (::read(STDIN_FILENO, &seq[2], 1) <= 0) continue;
          if (::read(STDIN_FILENO, &seq[3], 1) <= 0) continue;
          if (seq[2] == '0' && seq[3] == '0') {
            if (::read(STDIN_FILENO, &seq[4], 1) <= 0) continue;
            if (seq[4] == '~') {
              paste_mode = true;
              paste_buffer.clear();
              continue;
            }
          }
          if (seq[2] == '0' && seq[3] == '1') {
            if (::read(STDIN_FILENO, &seq[4], 1) <= 0) continue;
            if (seq[4] == '~') {
              paste_mode = false;
              for (char pc : paste_buffer) {
                if (pc == '\r') continue;
                buffer.insert(buffer.begin() + static_cast<long>(cursor), pc);
                ++cursor;
              }
              redraw_line(buffer, cursor);
              continue;
            }
          }
        }
        if (seq[1] == 'A') {
          if (buffer.find('\n') != std::string::npos) {
            size_t line_start = buffer.rfind('\n', cursor > 0 ? cursor - 1 : 0);
            size_t current_line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
            if (current_line_start == 0) {
              continue;
            }
            size_t prev_line_end = current_line_start - 1;
            size_t prev_line_start = buffer.rfind('\n', prev_line_end > 0 ? prev_line_end - 1 : 0);
            prev_line_start = (prev_line_start == std::string::npos) ? 0 : prev_line_start + 1;
            size_t col = cursor - current_line_start;
            size_t prev_len = prev_line_end - prev_line_start;
            cursor = prev_line_start + std::min(col, prev_len);
            redraw_line(buffer, cursor);
          } else if (!history_.empty() && history_index_ > 0) {
            if (history_index_ == history_.size()) {
              current_buffer_ = buffer;
            }
            --history_index_;
            buffer = history_[history_index_];
            cursor = buffer.size();
            redraw_line(buffer, cursor);
          }
          continue;
        }
        if (seq[1] == 'B') {
          if (buffer.find('\n') != std::string::npos) {
            size_t line_start = buffer.rfind('\n', cursor > 0 ? cursor - 1 : 0);
            size_t current_line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
            size_t line_end = buffer.find('\n', current_line_start);
            if (line_end == std::string::npos) {
              continue;
            }
            size_t next_line_start = line_end + 1;
            size_t next_line_end = buffer.find('\n', next_line_start);
            if (next_line_end == std::string::npos) {
              next_line_end = buffer.size();
            }
            size_t col = cursor - current_line_start;
            size_t next_len = next_line_end - next_line_start;
            cursor = next_line_start + std::min(col, next_len);
            redraw_line(buffer, cursor);
          } else if (history_index_ < history_.size()) {
            ++history_index_;
            if (history_index_ == history_.size()) {
              buffer = current_buffer_;
            } else {
              buffer = history_[history_index_];
            }
            cursor = buffer.size();
            redraw_line(buffer, cursor);
          }
          continue;
        }
        if (seq[1] == 'C') {
          if (cursor < buffer.size()) {
            ++cursor;
            redraw_line(buffer, cursor);
          }
          continue;
        }
        if (seq[1] == 'D') {
          if (cursor > 0) {
            --cursor;
            redraw_line(buffer, cursor);
          }
          continue;
        }
      }
      continue;
    }

    if (paste_mode) {
      paste_buffer.push_back(c);
      continue;
    }

    if (std::isprint(static_cast<unsigned char>(c))) {
      buffer.insert(buffer.begin() + static_cast<long>(cursor), c);
      ++cursor;
      redraw_line(buffer, cursor);
    }
  }
}

void LineEditor::add_history(const std::string& line) {
  if (line.empty()) return;
  if (!history_.empty() && history_.back() == line) return;
  history_.push_back(line);
  if (history_.size() > max_history_) {
    history_.erase(history_.begin());
  }
}

void LineEditor::redraw_line(const std::string& buffer, size_t cursor) {
  int width = terminal_width();
  if (width <= 0) width = 80;
  if (last_render_lines_ <= 0) {
    std::cout << "\r\033[2K";
  } else {
    if (last_cursor_line_ > 0) {
      std::cout << "\033[" << last_cursor_line_ << "A";
    }
    for (int i = 0; i < last_render_lines_; ++i) {
      std::cout << "\r\033[2K";
      if (i + 1 < last_render_lines_) {
        std::cout << "\033[1B";
      }
    }
    if (last_render_lines_ > 1) {
      std::cout << "\033[" << (last_render_lines_ - 1) << "A";
    }
    std::cout << "\r";
  }

  std::cout << prompt_;
  render_buffer(buffer);

  int end_line = 0;
  int end_col = static_cast<int>(prompt_len_);
  int cursor_line = 0;
  int cursor_col = static_cast<int>(prompt_len_);
  int line = 0;
  int col = static_cast<int>(prompt_len_);
  for (size_t i = 0; i < buffer.size(); ++i) {
    if (i == cursor) {
      cursor_line = line;
      cursor_col = col;
    }
    char c = buffer[i];
    if (c == '\n') {
      line++;
      col = static_cast<int>(cont_prompt_len_);
      continue;
    }
    col++;
    if (col >= width) {
      line++;
      col = 0;
    }
  }
  if (cursor == buffer.size()) {
    cursor_line = line;
    cursor_col = col;
  }
  end_line = line;
  end_col = col;

  last_render_lines_ = end_line + 1;
  last_cursor_line_ = cursor_line;

  int up = end_line - cursor_line;
  if (up > 0) {
    std::cout << "\033[" << up << "A";
  }
  std::cout << "\r";
  if (cursor_col > 0) {
    std::cout << "\033[" << cursor_col << "C";
  }
  std::cout << std::flush;
}

void LineEditor::render_buffer(const std::string& buffer) {
  bool in_single = false;
  bool in_double = false;
  size_t i = 0;
  while (i < buffer.size()) {
    char c = buffer[i];
    if (c == '\n') {
      std::cout << "\n" << cont_prompt_;
      ++i;
      continue;
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
      if (keyword_color_ && is_sql_keyword(word)) {
        std::cout << kColor.cyan << word << kColor.reset;
      } else {
        std::cout << word;
      }
      continue;
    }
    std::cout << c;
    ++i;
  }
}

bool LineEditor::is_sql_keyword(const std::string& word) {
  static const std::unordered_set<std::string> keywords = {
      "select", "from", "where", "and", "or", "in", "limit", "order", "by",
      "asc", "desc", "to", "list", "table", "csv", "parquet", "count", "summarize", "exclude",
      "is", "null"
  };
  std::string lower;
  lower.reserve(word.size());
  for (char c : word) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return keywords.find(lower) != keywords.end();
}

}  // namespace xsql::cli
