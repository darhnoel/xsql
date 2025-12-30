#include "render/duckbox_renderer.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <sstream>
#include <string>
#include <vector>
#include <clocale>
#include <cwchar>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace xsql::render {

namespace {

size_t detect_terminal_width() {
#ifdef _WIN32
  HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
  if (handle == INVALID_HANDLE_VALUE) return 120;
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (!GetConsoleScreenBufferInfo(handle, &info)) return 120;
  if (info.srWindow.Right <= info.srWindow.Left) return 120;
  return static_cast<size_t>(info.srWindow.Right - info.srWindow.Left + 1);
#else
  struct winsize w {};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
    return static_cast<size_t>(w.ws_col);
  }
  return 120;
#endif
}

void ensure_locale() {
  static bool initialized = false;
  if (!initialized) {
    std::setlocale(LC_CTYPE, "");
    initialized = true;
  }
}

size_t display_width(const std::string& value) {
  ensure_locale();
  mbstate_t state{};
  size_t width = 0;
  const char* ptr = value.c_str();
  size_t remaining = value.size();
  while (remaining > 0) {
    wchar_t wc;
    size_t len = std::mbrtowc(&wc, ptr, remaining, &state);
    if (len == static_cast<size_t>(-1) || len == static_cast<size_t>(-2)) {
      // Invalid or incomplete sequence: treat as single byte.
      ++width;
      ++ptr;
      --remaining;
      std::memset(&state, 0, sizeof(state));
      continue;
    }
    if (len == 0) {
      break;
    }
    int w = ::wcwidth(wc);
    width += (w < 0) ? 1 : static_cast<size_t>(w);
    ptr += len;
    remaining -= len;
  }
  return width;
}

std::string sanitize_cell(std::string value) {
  for (char& c : value) {
    if (c == '\n') c = ' ';
    if (c == '\r') c = ' ';
    if (c == '\t') c = ' ';
  }
  return value;
}

bool is_numeric(const std::string& value) {
  if (value.empty()) return false;
  if (value == "NULL") return false;
  size_t i = 0;
  if (value[0] == '-') i = 1;
  bool has_digit = false;
  bool has_dot = false;
  for (; i < value.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(value[i]);
    if (std::isdigit(c)) {
      has_digit = true;
      continue;
    }
    if (c == '.' && !has_dot) {
      has_dot = true;
      continue;
    }
    return false;
  }
  return has_digit;
}

std::string truncate_with_ellipsis(const std::string& value, size_t width) {
  if (display_width(value) <= width) return value;
  if (width == 0) return "";
  if (width == 1) return "…";
  const std::string ellipsis = "…";
  size_t ellipsis_width = display_width(ellipsis);
  size_t target = (width > ellipsis_width) ? width - ellipsis_width : 0;
  ensure_locale();
  mbstate_t state{};
  std::string out;
  const char* ptr = value.c_str();
  size_t remaining = value.size();
  size_t used = 0;
  while (remaining > 0 && used < target) {
    wchar_t wc;
    size_t len = std::mbrtowc(&wc, ptr, remaining, &state);
    if (len == static_cast<size_t>(-1) || len == static_cast<size_t>(-2)) {
      if (used + 1 > target) break;
      out.push_back(*ptr);
      ++ptr;
      --remaining;
      ++used;
      std::memset(&state, 0, sizeof(state));
      continue;
    }
    if (len == 0) {
      break;
    }
    int w = ::wcwidth(wc);
    size_t add = (w < 0) ? 1 : static_cast<size_t>(w);
    if (used + add > target) break;
    out.append(ptr, len);
    ptr += len;
    remaining -= len;
    used += add;
  }
  out += ellipsis;
  return out;
}

std::string pad_cell(const std::string& value, size_t width, bool right_align) {
  size_t w = display_width(value);
  if (w >= width) return value;
  size_t pad = width - w;
  if (right_align) {
    return std::string(pad, ' ') + value;
  }
  return value + std::string(pad, ' ');
}

std::vector<std::string> default_columns() {
  return {"node_id", "tag", "attributes", "parent_id", "source_uri"};
}

std::string attributes_to_string(const std::unordered_map<std::string, std::string>& attrs) {
  if (attrs.empty()) return "{}";
  std::vector<std::string> keys;
  keys.reserve(attrs.size());
  for (const auto& kv : attrs) {
    keys.push_back(kv.first);
  }
  std::sort(keys.begin(), keys.end());
  std::ostringstream oss;
  oss << "{";
  for (size_t i = 0; i < keys.size(); ++i) {
    if (i > 0) oss << ",";
    const auto& key = keys[i];
    oss << key << "=" << attrs.at(key);
  }
  oss << "}";
  return oss.str();
}

std::string field_value(const xsql::QueryResultRow& row, const std::string& field) {
  if (field == "node_id") return std::to_string(row.node_id);
  if (field == "count") return std::to_string(row.node_id);
  if (field == "tag") return row.tag;
  if (field == "text") return row.text;
  if (field == "inner_html") return row.inner_html;
  if (field == "parent_id") {
    return row.parent_id.has_value() ? std::to_string(*row.parent_id) : "NULL";
  }
  if (field == "source_uri") return row.source_uri;
  if (field == "attributes") return attributes_to_string(row.attributes);
  auto it = row.attributes.find(field);
  if (it == row.attributes.end()) return "NULL";
  return it->second;
}

std::string build_separator(const std::vector<size_t>& widths,
                            const std::string& left,
                            const std::string& mid,
                            const std::string& right) {
  std::ostringstream oss;
  oss << left;
  for (size_t i = 0; i < widths.size(); ++i) {
    for (size_t j = 0; j < widths[i] + 2; ++j) {
      oss << "─";
    }
    if (i + 1 < widths.size()) {
      oss << mid;
    }
  }
  oss << right;
  return oss.str();
}

}  // namespace

std::string render_duckbox(const xsql::QueryResult& result, const DuckboxOptions& options) {
  std::vector<std::string> columns = result.columns.empty() ? default_columns() : result.columns;
  size_t max_rows = options.max_rows == 0 ? result.rows.size() : options.max_rows;
  size_t rows_to_render = std::min(result.rows.size(), max_rows);
  size_t max_width = options.max_width == 0 ? detect_terminal_width() : options.max_width;
  if (max_width < 20) max_width = 20;

  std::vector<std::vector<std::string>> table_rows;
  table_rows.reserve(rows_to_render);
  for (size_t i = 0; i < rows_to_render; ++i) {
    const auto& row = result.rows[i];
    std::vector<std::string> cells;
    cells.reserve(columns.size());
    for (const auto& col : columns) {
      cells.push_back(sanitize_cell(field_value(row, col)));
    }
    table_rows.push_back(std::move(cells));
  }

  std::vector<size_t> widths(columns.size(), 0);
  for (size_t i = 0; i < columns.size(); ++i) {
    widths[i] = std::max(widths[i], display_width(columns[i]));
  }
  for (const auto& row : table_rows) {
    for (size_t i = 0; i < row.size(); ++i) {
      widths[i] = std::max(widths[i], display_width(row[i]));
    }
  }
  for (auto& w : widths) {
    w = std::max<size_t>(w, 4);
  }

  auto total_width = [&]() {
    size_t total = 1;
    for (auto w : widths) {
      total += w + 3;
    }
    return total;
  };

  while (total_width() > max_width) {
    size_t idx = 0;
    size_t max_w = 0;
    for (size_t i = 0; i < widths.size(); ++i) {
      if (widths[i] > max_w) {
        max_w = widths[i];
        idx = i;
      }
    }
    if (max_w <= 4) break;
    widths[idx]--;
  }

  std::ostringstream oss;
  oss << build_separator(widths, "┌", "┬", "┐") << "\n";
  oss << "│";
  for (size_t i = 0; i < columns.size(); ++i) {
    std::string header = truncate_with_ellipsis(columns[i], widths[i]);
    std::string padded = pad_cell(header, widths[i], false);
    if (options.highlight && options.is_tty) {
      padded = "\033[1m" + padded + "\033[0m";
    }
    oss << " " << padded << " │";
  }
  oss << "\n";
  oss << build_separator(widths, "├", "┼", "┤") << "\n";

  for (const auto& row : table_rows) {
    oss << "│";
    for (size_t i = 0; i < columns.size(); ++i) {
      std::string cell = truncate_with_ellipsis(row[i], widths[i]);
      bool right_align = is_numeric(row[i]);
      oss << " " << pad_cell(cell, widths[i], right_align) << " │";
    }
    oss << "\n";
  }

  if (result.rows.size() > rows_to_render) {
    size_t content_width = total_width() >= 4 ? total_width() - 4 : 0;
    std::ostringstream msg;
    msg << "… truncated, showing first " << rows_to_render << " of " << result.rows.size() << " rows …";
    std::string text = msg.str();
    text = truncate_with_ellipsis(text, content_width);
    text = pad_cell(text, content_width, false);
    oss << "│ " << text << " │\n";
  }

  oss << build_separator(widths, "└", "┴", "┘");
  return oss.str();
}

}  // namespace xsql::render
