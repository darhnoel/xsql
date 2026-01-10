#include "line_editor.h"

#include <iostream>
#include <unistd.h>
#include "repl/ui/autocomplete.h"
#include "repl/ui/render.h"
#include "repl/input/terminal.h"
#include "repl/input/text_util.h"

namespace xsql::cli {

namespace {

}  // namespace

LineEditor::LineEditor(size_t max_history, std::string prompt, size_t prompt_len)
    : history_(max_history),
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
  history_.reset_navigation();
  redraw_line(buffer, cursor);

  bool paste_mode = false;
  std::string paste_buffer;
  std::string utf8_pending;
  size_t utf8_expected = 0;
  while (true) {
    char c = 0;
    ssize_t n = ::read(STDIN_FILENO, &c, 1);
    if (n <= 0) return false;

    auto flush_utf8_pending = [&]() {
      if (!utf8_pending.empty()) {
        buffer.insert(buffer.begin() + static_cast<long>(cursor),
                      utf8_pending.begin(),
                      utf8_pending.end());
        cursor += utf8_pending.size();
        utf8_pending.clear();
        utf8_expected = 0;
      }
    };

    if (!paste_mode && (c == '\n' || c == '\r')) {
      flush_utf8_pending();
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
      flush_utf8_pending();
      if (cursor > 0) {
        size_t prev = prev_codepoint_start(buffer, cursor);
        buffer.erase(prev, cursor - prev);
        cursor = prev;
        redraw_line(buffer, cursor);
      }
      continue;
    }

    if (c == 12) {
      flush_utf8_pending();
      std::cout << "\033[2J\033[H" << std::flush;
      redraw_line(buffer, cursor);
      continue;
    }

    if (c == 9) {
      flush_utf8_pending();
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
      flush_utf8_pending();
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
            size_t col = column_width(buffer, current_line_start, cursor);
            size_t prev_len = column_width(buffer, prev_line_start, prev_line_end);
            cursor = column_to_index(buffer,
                                     prev_line_start,
                                     prev_line_end,
                                     std::min(col, prev_len));
            redraw_line(buffer, cursor);
          } else if (!history_.empty() && history_.prev(buffer)) {
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
            size_t col = column_width(buffer, current_line_start, cursor);
            size_t next_len = column_width(buffer, next_line_start, next_line_end);
            cursor = column_to_index(buffer,
                                     next_line_start,
                                     next_line_end,
                                     std::min(col, next_len));
            redraw_line(buffer, cursor);
          } else if (history_.next(buffer)) {
            cursor = buffer.size();
            redraw_line(buffer, cursor);
          }
          continue;
        }
        if (seq[1] == 'C') {
          if (cursor < buffer.size()) {
            cursor = next_codepoint_start(buffer, cursor);
            redraw_line(buffer, cursor);
          }
          continue;
        }
        if (seq[1] == 'D') {
          if (cursor > 0) {
            cursor = prev_codepoint_start(buffer, cursor);
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

    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 0x20 && uc != 0x7F) {
      if ((uc & 0x80) == 0x80) {
        size_t expected = utf8_expected;
        if (utf8_pending.empty()) {
          expected = utf8_sequence_length(uc);
          if (expected == 1) {
            buffer.insert(buffer.begin() + static_cast<long>(cursor), c);
            ++cursor;
            redraw_line(buffer, cursor);
            continue;
          }
          utf8_expected = expected;
        } else if (expected == 0) {
          expected = utf8_sequence_length(uc);
          utf8_expected = expected;
        }
        utf8_pending.push_back(c);
        if (utf8_expected > 0 && utf8_pending.size() >= utf8_expected) {
          buffer.insert(buffer.begin() + static_cast<long>(cursor),
                        utf8_pending.begin(),
                        utf8_pending.end());
          cursor += utf8_pending.size();
          utf8_pending.clear();
          utf8_expected = 0;
          redraw_line(buffer, cursor);
        }
        continue;
      }
      flush_utf8_pending();
      buffer.insert(buffer.begin() + static_cast<long>(cursor), c);
      ++cursor;
      redraw_line(buffer, cursor);
    }
  }
}

void LineEditor::add_history(const std::string& line) {
  history_.add(line);
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
  render_buffer(buffer, keyword_color_, cont_prompt_);

  int end_line = compute_render_lines(buffer, prompt_, prompt_len_,
                                      cont_prompt_, cont_prompt_len_, width) - 1;
  int cursor_line = compute_cursor_line(buffer, cursor, prompt_, prompt_len_,
                                        cont_prompt_, cont_prompt_len_, width);

  int cursor_col = 0;
  if (cursor_line > 0) {
    size_t line_start = 0;
    int line = 0;
    while (line < cursor_line && line_start < buffer.size()) {
      size_t next = buffer.find('\n', line_start);
      if (next == std::string::npos) {
        line_start = buffer.size();
        break;
      }
      line_start = next + 1;
      ++line;
    }
    cursor_col = static_cast<int>(cont_prompt_len_ + column_width(buffer, line_start, cursor));
  } else {
    cursor_col = static_cast<int>(prompt_len_ + column_width(buffer, 0, cursor));
  }

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

}  // namespace xsql::cli
