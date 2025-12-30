#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cctype>
#include <optional>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

#include "xsql/xsql.h"
#include "html_parser.h"
#include "query_parser.h"
#include "render/duckbox_renderer.h"

#ifdef XSQL_USE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#endif
#ifdef XSQL_USE_CURL
#include <curl/curl.h>
#endif

namespace {

struct Color {
  const char* reset = "\033[0m";
  const char* red = "\033[31m";
  const char* green = "\033[32m";
  const char* yellow = "\033[33m";
  const char* blue = "\033[34m";
  const char* magenta = "\033[35m";
  const char* cyan = "\033[36m";
  const char* dim = "\033[2m";
};

Color kColor;

struct TruncateResult {
  std::string output;
  bool truncated = false;
};

std::string read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + path);
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string read_stdin() {
  std::ostringstream buffer;
  buffer << std::cin.rdbuf();
  return buffer.str();
}

std::string trim_semicolon(std::string value) {
  while (!value.empty() && (value.back() == ';' || std::isspace(static_cast<unsigned char>(value.back())))) {
    value.pop_back();
  }
  return value;
}

std::string colorize_json(const std::string& input, bool enable) {
  if (!enable) return input;
  std::string out;
  out.reserve(input.size() * 2);
  bool in_string = false;
  bool escape = false;
  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    if (in_string) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        in_string = false;
        out += '"';
        out += kColor.reset;
        continue;
      }
      out += c;
      continue;
    }

    if (c == '"') {
      in_string = true;
      out += kColor.green;
      out += '"';
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
      out += kColor.cyan;
      while (i < input.size() &&
             (std::isdigit(static_cast<unsigned char>(input[i])) || input[i] == '.' || input[i] == '-' ||
              input[i] == 'e' || input[i] == 'E' || input[i] == '+')) {
        out += input[i++];
      }
      --i;
      out += kColor.reset;
      continue;
    }

    if (input.compare(i, 4, "true") == 0 || input.compare(i, 5, "false") == 0) {
      size_t len = input.compare(i, 4, "true") == 0 ? 4 : 5;
      out += kColor.yellow;
      out.append(input, i, len);
      out += kColor.reset;
      i += len - 1;
      continue;
    }

    if (input.compare(i, 4, "null") == 0) {
      out += kColor.magenta;
      out.append(input, i, 4);
      out += kColor.reset;
      i += 3;
      continue;
    }

    if (c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',') {
      out += kColor.dim;
      out += c;
      out += kColor.reset;
      continue;
    }

    out += c;
  }
  return out;
}

TruncateResult truncate_output(const std::string& text, size_t head_lines, size_t tail_lines) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }

  if (lines.size() <= head_lines + tail_lines) {
    return {text, false};
  }

  std::ostringstream oss;
  for (size_t i = 0; i < head_lines; ++i) {
    oss << lines[i] << "\n";
  }
  oss << "... (abbreviated; use .display_mode more to show all or redirect to a file) ...\n";
  for (size_t i = lines.size() - tail_lines; i < lines.size(); ++i) {
    oss << lines[i];
    if (i + 1 < lines.size()) {
      oss << "\n";
    }
  }
  return {oss.str(), true};
}

bool is_url(const std::string& value) {
  return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

std::string load_html_input(const std::string& input, int timeout_ms) {
  if (is_url(input)) {
#ifdef XSQL_USE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
      throw std::runtime_error("Failed to initialize curl");
    }
    std::string buffer;
    curl_easy_setopt(curl, CURLOPT_URL, input.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void* contents, size_t size, size_t nmemb, void* userp) {
      size_t total = size * nmemb;
      auto* out = static_cast<std::string*>(userp);
      out->append(static_cast<const char*>(contents), total);
      return total;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "xsql/0.1");
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
      throw std::runtime_error(std::string("Failed to fetch URL: ") + curl_easy_strerror(res));
    }
    return buffer;
#else
    (void)timeout_ms;
    throw std::runtime_error("URL fetching is disabled (libcurl not available)");
#endif
  }
  return read_file(input);
}

std::string rewrite_from_path_if_needed(const std::string& query) {
  std::string lower = query;
  for (char& c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  size_t pos = 0;
  while (true) {
    pos = lower.find("from", pos);
    if (pos == std::string::npos) return query;
    bool left_ok = (pos == 0) || std::isspace(static_cast<unsigned char>(lower[pos - 1]));
    bool right_ok = (pos + 4 >= lower.size()) || std::isspace(static_cast<unsigned char>(lower[pos + 4]));
    if (!left_ok || !right_ok) {
      pos += 4;
      continue;
    }
    size_t i = pos + 4;
    while (i < query.size() && std::isspace(static_cast<unsigned char>(query[i]))) {
      ++i;
    }
    if (i >= query.size()) return query;
    char first = query[i];
    if (first == '\'' || first == '"') return query;
    size_t start = i;
    while (i < query.size() && !std::isspace(static_cast<unsigned char>(query[i])) && query[i] != ';') {
      ++i;
    }
    if (start == i) return query;
    std::string token = query.substr(start, i - start);
    bool looks_like_path = token.find('/') != std::string::npos ||
                           token.find('.') != std::string::npos ||
                           (!token.empty() && (token[0] == '.' || token[0] == '~'));
    if (!looks_like_path) return query;
    std::string rewritten = query.substr(0, start);
    rewritten.push_back('\'');
    rewritten += token;
    rewritten.push_back('\'');
    rewritten += query.substr(i);
    return rewritten;
  }
}

std::string sanitize_pasted_line(std::string line) {
  const std::string prompt = "xsql> ";
  size_t pos = line.rfind(prompt);
  if (pos != std::string::npos) {
    line = line.substr(pos + prompt.size());
  }
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
    line.erase(line.begin());
  }
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
    line.pop_back();
  }
  return line;
}

struct QuerySource {
  xsql::Source::Kind kind = xsql::Source::Kind::Document;
  std::string value;
};

std::optional<QuerySource> parse_query_source(const std::string& query) {
  std::string cleaned = trim_semicolon(query);
  auto parsed = xsql::parse_query(cleaned);
  if (!parsed.query.has_value()) return std::nullopt;
  QuerySource source;
  source.kind = parsed.query->source.kind;
  source.value = parsed.query->source.value;
  return source;
}

class LineEditor {
 public:
  LineEditor(size_t max_history, std::string prompt, size_t prompt_len)
      : max_history_(max_history),
        prompt_(std::move(prompt)),
        prompt_len_(prompt_len),
        cont_prompt_(""),
        cont_prompt_len_(0) {}

  void set_prompt(std::string prompt, size_t prompt_len) {
    prompt_ = std::move(prompt);
    prompt_len_ = prompt_len;
  }

  void set_cont_prompt(std::string prompt, size_t prompt_len) {
    cont_prompt_ = std::move(prompt);
    cont_prompt_len_ = prompt_len;
  }

  void set_keyword_color(bool enabled) {
    keyword_color_ = enabled;
  }

  void reset_render_state() {
    last_render_lines_ = 1;
    last_cursor_line_ = 0;
  }

  bool read_line(std::string& out, const std::string& initial = {}) {
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
        bool changed = completer_.complete(buffer, cursor, suggestions);
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

  void add_history(const std::string& line) {
    if (line.empty()) return;
    if (!history_.empty() && history_.back() == line) return;
    history_.push_back(line);
    if (history_.size() > max_history_) {
      history_.erase(history_.begin());
    }
  }

 private:
  class TermiosGuard {
   public:
    TermiosGuard() : ok_(false) {
      if (tcgetattr(STDIN_FILENO, &orig_) != 0) return;
      termios raw = orig_;
      raw.c_lflag &= ~(ECHO | ICANON);
      raw.c_cc[VMIN] = 1;
      raw.c_cc[VTIME] = 0;
      if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return;
      std::cout << "\033[?2004h" << std::flush;
      ok_ = true;
    }
    ~TermiosGuard() {
      if (ok_) {
        std::cout << "\033[?2004l" << std::flush;
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_);
      }
    }
    bool ok() const { return ok_; }

   private:
    termios orig_{};
    bool ok_;
  };

  int terminal_width() const {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
      return ws.ws_col;
    }
    return 80;
  }

  void clear_rendered_lines() {
    if (last_render_lines_ <= 0) {
      std::cout << "\r\033[2K";
      return;
    }
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

  void redraw_line(const std::string& buffer, size_t cursor) {
    int width = terminal_width();
    if (width <= 0) width = 80;
    clear_rendered_lines();

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

  size_t max_history_;
  std::vector<std::string> history_;
  size_t history_index_ = 0;
  std::string current_buffer_;
  std::string prompt_;
  size_t prompt_len_ = 0;
  std::string cont_prompt_;
  size_t cont_prompt_len_ = 0;
  int last_render_lines_ = 1;
  int last_cursor_line_ = 0;
  bool keyword_color_ = false;

  void render_buffer(const std::string& buffer) {
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

  static bool is_sql_keyword(const std::string& word) {
    static const std::unordered_set<std::string> keywords = {
        "select", "from", "where", "and", "or", "in", "limit", "order", "by",
        "asc", "desc", "to", "list", "table", "count", "summarize", "exclude",
        "is", "null"
    };
    std::string lower;
    lower.reserve(word.size());
    for (char c : word) {
      lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return keywords.find(lower) != keywords.end();
  }

  class AutoCompleter {
   public:
    AutoCompleter() {
      keywords_ = {
          "select", "from", "where", "and", "or", "in", "limit", "to", "list", "table",
          "count", "summarize", "order", "by", "asc", "desc", "document", "doc",
          "attributes", "tag", "text", "parent", "child", "ancestor", "descendant",
          "parent_id", "inner_html", "trim", "is", "null"
      };
      commands_ = {
          ".help", ".load", ".mode", ".display_mode", ".max_rows", ".summarize", ".quit", ".q",
          ":help", ":load", ":quit", ":exit"
      };
    }

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
    static bool is_word_char(char c) {
      return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == ':';
    }

    static std::string to_lower(const std::string& s) {
      std::string out;
      out.reserve(s.size());
      for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      }
      return out;
    }

    static bool starts_with(const std::string& value, const std::string& prefix) {
      return value.rfind(prefix, 0) == 0;
    }

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

  AutoCompleter completer_;
};

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 4);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

#ifndef XSQL_USE_NLOHMANN_JSON
std::string attributes_to_json(const xsql::QueryResultRow& row) {
  std::string out = "{";
  size_t count = 0;
  for (const auto& kv : row.attributes) {
    if (count++ > 0) out += ",";
    out += "\"";
    out += json_escape(kv.first);
    out += "\":\"";
    out += json_escape(kv.second);
    out += "\"";
  }
  out += "}";
  return out;
}
#endif

void print_field(std::ostream& os, const std::string& field, const xsql::QueryResultRow& row) {
  if (field == "node_id") {
    os << row.node_id;
  } else if (field == "count") {
    os << row.node_id;
  } else if (field == "tag") {
    os << "\"" << json_escape(row.tag) << "\"";
  } else if (field == "text") {
    os << "\"" << json_escape(row.text) << "\"";
  } else if (field == "inner_html") {
    os << "\"" << json_escape(row.inner_html) << "\"";
  } else if (field == "parent_id") {
    if (row.parent_id.has_value()) {
      os << *row.parent_id;
    } else {
      os << "null";
    }
  } else if (field == "source_uri") {
    os << "\"" << json_escape(row.source_uri) << "\"";
  } else if (field == "attributes") {
#ifndef XSQL_USE_NLOHMANN_JSON
    os << attributes_to_json(row);
#else
    os << "null";
#endif
  } else {
    auto it = row.attributes.find(field);
    if (it != row.attributes.end()) {
      os << "\"" << json_escape(it->second) << "\"";
    } else {
      os << "null";
    }
  }
}

std::string build_json(const xsql::QueryResult& result) {
#ifdef XSQL_USE_NLOHMANN_JSON
  using nlohmann::json;
  std::vector<std::string> columns = result.columns;
  if (columns.empty()) {
    columns = {"node_id", "tag", "attributes", "parent_id", "source_uri"};
  }
  json out = json::array();
  for (const auto& row : result.rows) {
    json obj = json::object();
    for (const auto& field : columns) {
      if (field == "node_id") {
        obj[field] = row.node_id;
      } else if (field == "count") {
        obj[field] = row.node_id;
      } else if (field == "tag") {
        obj[field] = row.tag;
      } else if (field == "text") {
        obj[field] = row.text;
      } else if (field == "inner_html") {
        obj[field] = row.inner_html;
      } else if (field == "parent_id") {
        obj[field] = row.parent_id.has_value() ? json(*row.parent_id) : json(nullptr);
      } else if (field == "source_uri") {
        obj[field] = row.source_uri;
      } else if (field == "attributes") {
        json attrs = json::object();
        for (const auto& kv : row.attributes) {
          attrs[kv.first] = kv.second;
        }
        obj[field] = attrs;
      } else {
        auto it = row.attributes.find(field);
        obj[field] = (it != row.attributes.end()) ? json(it->second) : json(nullptr);
      }
    }
    out.push_back(obj);
  }
  return out.dump(2);
#else
  std::vector<std::string> columns = result.columns;
  if (columns.empty()) {
    columns = {"node_id", "tag", "attributes", "parent_id", "source_uri"};
  }
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < result.rows.size(); ++i) {
    const auto& row = result.rows[i];
    if (i > 0) oss << ",";
    oss << "{";
    for (size_t c = 0; c < columns.size(); ++c) {
      if (c > 0) oss << ",";
      const auto& field = columns[c];
      oss << "\"" << json_escape(field) << "\":";
      print_field(oss, field, row);
    }
    oss << "}";
  }
  oss << "]";
  return oss.str();
#endif
}

std::string build_json_list(const xsql::QueryResult& result) {
#ifdef XSQL_USE_NLOHMANN_JSON
  using nlohmann::json;
  std::vector<std::string> columns = result.columns;
    if (columns.size() != 1) {
      throw std::runtime_error("TO LIST() requires a single projected column");
    }
    const std::string& field = columns[0];
    json out = json::array();
    for (const auto& row : result.rows) {
      if (field == "node_id") {
        out.push_back(row.node_id);
      } else if (field == "count") {
        out.push_back(row.node_id);
      } else if (field == "tag") {
        out.push_back(row.tag);
      } else if (field == "text") {
        out.push_back(row.text);
      } else if (field == "inner_html") {
        out.push_back(row.inner_html);
      } else if (field == "parent_id") {
        out.push_back(row.parent_id.has_value() ? json(*row.parent_id) : json(nullptr));
      } else if (field == "source_uri") {
      out.push_back(row.source_uri);
    } else if (field == "attributes") {
      json attrs = json::object();
      for (const auto& kv : row.attributes) {
        attrs[kv.first] = kv.second;
      }
      out.push_back(attrs);
    } else {
      auto it = row.attributes.find(field);
      out.push_back((it != row.attributes.end()) ? json(it->second) : json(nullptr));
    }
  }
  return out.dump(2);
#else
  std::vector<std::string> columns = result.columns;
  if (columns.size() != 1) {
    throw std::runtime_error("TO LIST() requires a single projected column");
  }
  const std::string& field = columns[0];
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < result.rows.size(); ++i) {
    const auto& row = result.rows[i];
    if (i > 0) oss << ",";
    print_field(oss, field, row);
  }
  oss << "]";
  return oss.str();
#endif
}

std::string build_table_json(const xsql::QueryResult& result) {
#ifdef XSQL_USE_NLOHMANN_JSON
  using nlohmann::json;
  if (result.tables.size() == 1) {
    json rows = json::array();
    for (const auto& row : result.tables[0].rows) {
      rows.push_back(row);
    }
    return rows.dump(2);
  }
  json out = json::array();
  for (const auto& table : result.tables) {
    json rows = json::array();
    for (const auto& row : table.rows) {
      rows.push_back(row);
    }
    out.push_back({{"node_id", table.node_id}, {"rows", rows}});
  }
  return out.dump(2);
#else
  std::ostringstream oss;
  if (result.tables.size() == 1) {
    oss << "[";
    const auto& rows = result.tables[0].rows;
    for (size_t i = 0; i < rows.size(); ++i) {
      if (i > 0) oss << ",";
      oss << "[";
      for (size_t j = 0; j < rows[i].size(); ++j) {
        if (j > 0) oss << ",";
        oss << "\"" << json_escape(rows[i][j]) << "\"";
      }
      oss << "]";
    }
    oss << "]";
    return oss.str();
  }
  oss << "[";
  for (size_t i = 0; i < result.tables.size(); ++i) {
    if (i > 0) oss << ",";
    oss << "{\"node_id\":" << result.tables[i].node_id << ",\"rows\":[";
    const auto& rows = result.tables[i].rows;
    for (size_t r = 0; r < rows.size(); ++r) {
      if (r > 0) oss << ",";
      oss << "[";
      for (size_t c = 0; c < rows[r].size(); ++c) {
        if (c > 0) oss << ",";
        oss << "\"" << json_escape(rows[r][c]) << "\"";
      }
      oss << "]";
    }
    oss << "]}";
  }
  oss << "]";
  return oss.str();
#endif
}
}  // namespace

std::string render_table_duckbox(const xsql::QueryResult::TableResult& table,
                                 bool highlight,
                                 bool is_tty,
                                 size_t max_rows) {
  size_t max_cols = 0;
  for (const auto& row : table.rows) {
    if (row.size() > max_cols) {
      max_cols = row.size();
    }
  }
  if (max_cols == 0) {
    return "(empty table)";
  }
  xsql::QueryResult view;
  size_t data_start = 0;
  std::vector<std::string> column_keys;
  column_keys.reserve(max_cols);
  if (!table.rows.empty()) {
    std::vector<std::string> headers = table.rows.front();
    data_start = 1;
    if (headers.size() < max_cols) {
      headers.resize(max_cols);
    }
    std::unordered_map<std::string, int> seen;
    for (size_t i = 0; i < max_cols; ++i) {
      std::string name = headers[i];
      if (name.empty()) {
        name = "col" + std::to_string(i + 1);
      }
      auto& count = seen[name];
      std::string key = name;
      if (count > 0) {
        key = name + "_" + std::to_string(count + 1);
      }
      ++count;
      column_keys.push_back(key);
    }
  } else {
    for (size_t i = 0; i < max_cols; ++i) {
      column_keys.push_back("col" + std::to_string(i + 1));
    }
  }
  view.columns = column_keys;
  for (size_t r = data_start; r < table.rows.size(); ++r) {
    const auto& row_values = table.rows[r];
    xsql::QueryResultRow row;
    size_t limit = std::min(row_values.size(), column_keys.size());
    for (size_t i = 0; i < limit; ++i) {
      row.attributes[column_keys[i]] = row_values[i];
    }
    view.rows.push_back(std::move(row));
  }
  xsql::render::DuckboxOptions options;
  options.max_width = 0;
  options.max_rows = max_rows;
  options.highlight = highlight;
  options.is_tty = is_tty;
  return xsql::render::render_duckbox(view, options);
}

std::string build_summary_json(const std::vector<std::pair<std::string, size_t>>& summary) {
#ifdef XSQL_USE_NLOHMANN_JSON
  using nlohmann::json;
  json out = json::array();
  for (const auto& item : summary) {
    out.push_back({{"tag", item.first}, {"count", item.second}});
  }
  return out.dump(2);
#else
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < summary.size(); ++i) {
    if (i > 0) oss << ",";
    oss << "{\"tag\":\"" << json_escape(summary[i].first) << "\",\"count\":" << summary[i].second << "}";
  }
  oss << "]";
  return oss.str();
#endif
}

int main(int argc, char** argv) {
  std::string query;
  std::string query_file;
  std::string input;
  bool interactive = false;
  bool color = true;
  std::string output_mode = "duckbox";
  // Assumption: default highlight is on (per CLI flag requirement); auto-disabled on non-TTY.
  bool highlight = true;
  int timeout_ms = 5000;

  if (argc == 1) {
    std::cout << "xsql - XSQL command line interface\n\n";
    std::cout << "Usage:\n";
    std::cout << "  xsql --query <query> [--input <path>]\n";
    std::cout << "  xsql --query-file <file> [--input <path>]\n";
    std::cout << "  xsql --interactive [--input <path>]\n";
    std::cout << "  xsql --mode duckbox|json|plain\n";
    std::cout << "  xsql --highlight on|off\n";
    std::cout << "  xsql --timeout-ms <n>\n";
    std::cout << "  xsql --color=disabled\n\n";
    std::cout << "Notes:\n";
    std::cout << "  - If --input is omitted, HTML is read from stdin.\n";
    std::cout << "  - URLs are supported when libcurl is available.\n";
    std::cout << "  - TO LIST() outputs a JSON list for a single projected column.\n";
    std::cout << "  - TO TABLE() extracts HTML tables into rows.\n\n";
    std::cout << "Examples:\n";
    std::cout << "  xsql --query \"SELECT table FROM doc\" --input ./data/index.html\n";
    std::cout << "  xsql --query \"SELECT link.href FROM doc WHERE attributes.rel = 'preload' TO LIST()\" --input ./data/index.html\n";
    std::cout << "  xsql --interactive --input ./data/index.html\n";
    return 0;
  }

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--query" && i + 1 < argc) {
      query = argv[++i];
    } else if (arg == "--query-file" && i + 1 < argc) {
      query_file = argv[++i];
    } else if (arg == "--input" && i + 1 < argc) {
      input = argv[++i];
    } else if (arg == "--interactive") {
      interactive = true;
    } else if (arg == "--mode" && i + 1 < argc) {
      output_mode = argv[++i];
    } else if (arg == "--highlight" && i + 1 < argc) {
      std::string value = argv[++i];
      if (value == "on") {
        highlight = true;
      } else if (value == "off") {
        highlight = false;
      } else {
        std::cerr << "Invalid --highlight value (use on|off)\n";
        return 1;
      }
    } else if (arg == "--color=disabled") {
      color = false;
    } else if (arg == "--timeout-ms" && i + 1 < argc) {
      timeout_ms = std::stoi(argv[++i]);
    } else if (arg == "--help") {
      std::cout << "Usage: xsql --query <query> [--input <path>]\n";
      std::cout << "       xsql --query-file <file> [--input <path>]\n";
      std::cout << "       xsql --interactive [--input <path>]\n";
      std::cout << "       xsql --mode duckbox|json|plain\n";
      std::cout << "       xsql --highlight on|off\n";
      std::cout << "       xsql --timeout-ms <n>\n";
      std::cout << "       xsql --color=disabled\n";
      std::cout << "If --input is omitted, HTML is read from stdin.\n";
      return 0;
    }
  }

  if (output_mode != "duckbox" && output_mode != "json" && output_mode != "plain") {
    std::cerr << "Invalid --mode value (use duckbox|json|plain)\n";
    return 1;
  }

  try {
    if (!isatty(fileno(stdout))) {
      color = false;
      highlight = false;
    }
    if (interactive) {
      const bool use_prompt = isatty(fileno(stdin)) != 0;
      std::string prompt = color ? (std::string(kColor.blue) + "xsql> " + kColor.reset) : "xsql> ";
      LineEditor editor(5, prompt, 6);
      editor.set_keyword_color(color);
      std::string cont_prompt = color ? (std::string(kColor.cyan) + "----> " + kColor.reset) : "----> ";
      editor.set_cont_prompt(cont_prompt, 5);
      std::string active_source = input;
      std::optional<std::string> active_html;
      std::string last_full_output;
      bool display_full = true;
      size_t max_rows = 40;
      std::string line;
      while (true) {
        editor.set_prompt(prompt, 6);
        if (!editor.read_line(line)) {
          break;
        }
        line = sanitize_pasted_line(line);
        if (line == ":quit" || line == ":exit" || line == ".quit" || line == ".q") {
          break;
        }
        if (line == ".help" || line == ":help") {
          std::cout << "Commands:\n";
          std::cout << "  .help                 Show this help\n";
          std::cout << "  .load <path|url>       Load input (or :load)\n";
          std::cout << "  .mode duckbox|json|plain  Set output mode\n";
          std::cout << "  .display_mode more|less   Control truncation\n";
          std::cout << "  .max_rows <n>           Set duckbox max rows (0 = no limit)\n";
          std::cout << "  .summarize [doc|path|url]  Show tag counts\n";
          std::cout << "  .quit / .q             Exit\n";
          continue;
        }
        if (line.rfind(".display_mode", 0) == 0) {
          std::istringstream iss(line);
          std::string cmd;
          std::string mode;
          iss >> cmd >> mode;
          if (mode == "more") {
            display_full = true;
            std::cout << "Display mode: more" << std::endl;
          } else if (mode == "less") {
            display_full = false;
            std::cout << "Display mode: less" << std::endl;
          } else {
            std::cerr << "Usage: .display_mode more|less" << std::endl;
          }
          continue;
        }
        if (line.rfind(".mode", 0) == 0) {
          std::istringstream iss(line);
          std::string cmd;
          std::string mode;
          iss >> cmd >> mode;
          if (mode == "duckbox" || mode == "json" || mode == "plain") {
            output_mode = mode;
            std::cout << "Output mode: " << output_mode << std::endl;
          } else {
            std::cerr << "Usage: .mode duckbox|json|plain" << std::endl;
          }
          continue;
        }
        if (line.rfind(".max_rows", 0) == 0) {
          std::istringstream iss(line);
          std::string cmd;
          size_t value = 0;
          iss >> cmd >> value;
          if (!iss.fail()) {
            max_rows = value;
            if (max_rows == 0) {
              std::cout << "Duckbox max rows: unlimited" << std::endl;
            } else {
              std::cout << "Duckbox max rows: " << max_rows << std::endl;
            }
          } else {
            std::cerr << "Usage: .max_rows <n>" << std::endl;
          }
          continue;
        }
        if (line.rfind(".summarize", 0) == 0) {
          std::istringstream iss(line);
          std::string cmd;
          std::string target;
          iss >> cmd >> target;
          target = trim_semicolon(target);
          bool use_active = target.empty() || target == "doc" || target == "document";
          if (use_active) {
            if (active_html.has_value()) {
              target = active_source;
            } else if (!active_source.empty()) {
              try {
                active_html = load_html_input(active_source, timeout_ms);
              } catch (const std::exception& ex) {
                if (color) std::cerr << kColor.red;
                std::cerr << "Error: " << ex.what() << std::endl;
                if (color) std::cerr << kColor.reset;
                continue;
              }
              target = active_source;
            } else {
              std::cerr << "No input loaded. Use .load <path|url> or start with --input <path|url>." << std::endl;
              continue;
            }
          }
          try {
            std::string html;
            if (use_active && active_html.has_value()) {
              html = *active_html;
            } else {
              html = load_html_input(target, timeout_ms);
            }
            xsql::HtmlDocument doc = xsql::parse_html(html);
            std::unordered_map<std::string, size_t> counts;
            for (const auto& node : doc.nodes) {
              ++counts[node.tag];
            }
            std::vector<std::pair<std::string, size_t>> summary;
            summary.reserve(counts.size());
            for (const auto& kv : counts) {
              summary.emplace_back(kv.first, kv.second);
            }
            std::sort(summary.begin(), summary.end(),
                      [](const auto& a, const auto& b) {
                        if (a.second != b.second) return a.second > b.second;
                        return a.first < b.first;
                      });
            if (output_mode == "duckbox") {
              xsql::QueryResult result;
              result.columns = {"tag", "count"};
              for (const auto& item : summary) {
                xsql::QueryResultRow row;
                row.tag = item.first;
                row.node_id = static_cast<int64_t>(item.second);
                result.rows.push_back(std::move(row));
              }
              xsql::render::DuckboxOptions options;
              options.max_width = 0;
              options.max_rows = max_rows;
              options.highlight = highlight;
              options.is_tty = color;
              std::cout << xsql::render::render_duckbox(result, options) << std::endl;
            } else {
              std::string json_out = build_summary_json(summary);
              last_full_output = json_out;
              if (display_full) {
                std::cout << colorize_json(json_out, color) << std::endl;
              } else {
                TruncateResult truncated = truncate_output(json_out, 10, 10);
                std::cout << colorize_json(truncated.output, color) << std::endl;
              }
            }
            editor.reset_render_state();
          } catch (const std::exception& ex) {
            if (color) std::cerr << kColor.red;
            std::cerr << "Error: " << ex.what() << std::endl;
            if (color) std::cerr << kColor.reset;
          }
          continue;
        }
        if (line.rfind(":load", 0) == 0 || line.rfind(".load", 0) == 0) {
          std::istringstream iss(line);
          std::string cmd;
          iss >> cmd;
          std::string path;
          iss >> path;
          if (path.empty()) {
            std::cerr << "Usage: .load <path|url> or :load <path|url>" << std::endl;
            continue;
          }
          path = trim_semicolon(path);
          if (path == "doc" || path == "document") {
            std::cerr << "Error: .load doc is not valid. Use .load <path|url> to load data." << std::endl;
            continue;
          }
          if (is_url(path)) {
#ifndef XSQL_USE_CURL
            std::cerr << "Error: URL fetching is disabled (libcurl not available). Install libcurl and rebuild." << std::endl;
            continue;
#endif
          } else {
            if (!std::filesystem::exists(path)) {
              std::cerr << "Error: file not found: " << path << std::endl;
              continue;
            }
          }
          try {
            active_html = load_html_input(path, timeout_ms);
          } catch (const std::exception& ex) {
            if (color) std::cerr << kColor.red;
            std::cerr << "Error: " << ex.what() << std::endl;
            if (color) std::cerr << kColor.reset;
            continue;
          }
          active_source = path;
          std::cout << "Loaded: " << active_source << std::endl;
          continue;
        }
        if (line.empty()) {
          continue;
        }
        std::string query_text = trim_semicolon(line);
        query_text = rewrite_from_path_if_needed(query_text);
        editor.add_history(query_text);
        try {
          xsql::QueryResult result;
          auto source = parse_query_source(query_text);
          if (source.has_value() && source->kind == xsql::Source::Kind::Url) {
            result = xsql::execute_query_from_url(source->value, query_text, timeout_ms);
          } else if (source.has_value() && source->kind == xsql::Source::Kind::Path) {
            result = xsql::execute_query_from_file(source->value, query_text);
          } else {
            if (active_source.empty() && !active_html.has_value()) {
              if (color) std::cerr << kColor.red;
              std::cerr << "No input loaded. Use :load <path|url> or start with --input <path|url>." << std::endl;
              if (color) std::cerr << kColor.reset;
              continue;
            }
            if (!active_html.has_value()) {
              active_html = load_html_input(active_source, timeout_ms);
            }
            result = xsql::execute_query_from_document(*active_html, query_text);
            if (!active_source.empty()) {
              for (auto& row : result.rows) {
                row.source_uri = active_source;
              }
            }
          }
          if (output_mode == "duckbox") {
            if (result.to_table) {
              if (result.tables.empty()) {
                std::cout << "(empty table)" << std::endl;
              } else {
                for (size_t i = 0; i < result.tables.size(); ++i) {
                  if (result.tables.size() > 1) {
                    std::cout << "Table node_id=" << result.tables[i].node_id << std::endl;
                  }
                  std::cout << render_table_duckbox(result.tables[i], highlight, color, max_rows) << std::endl;
                }
              }
            } else if (!result.to_list) {
              xsql::render::DuckboxOptions options;
              options.max_width = 0;
              options.max_rows = max_rows;
              options.highlight = highlight;
              options.is_tty = color;
              std::cout << xsql::render::render_duckbox(result, options) << std::endl;
            } else {
              std::string json_out = build_json_list(result);
              last_full_output = json_out;
              if (display_full) {
                std::cout << colorize_json(json_out, color) << std::endl;
              } else {
                TruncateResult truncated = truncate_output(json_out, 10, 10);
                std::cout << colorize_json(truncated.output, color) << std::endl;
              }
            }
          } else {
            std::string json_out = result.to_table ? build_table_json(result)
                                  : (result.to_list ? build_json_list(result) : build_json(result));
            last_full_output = json_out;
            if (output_mode == "plain") {
              std::cout << json_out << std::endl;
            } else if (display_full) {
              std::cout << colorize_json(json_out, color) << std::endl;
            } else {
              TruncateResult truncated = truncate_output(json_out, 10, 10);
              std::cout << colorize_json(truncated.output, color) << std::endl;
            }
          }
          editor.reset_render_state();
        } catch (const std::exception& ex) {
          if (color) std::cerr << kColor.red;
          std::cerr << "Error: " << ex.what() << std::endl;
          if (color) std::cerr << kColor.reset;
          if (color) std::cerr << kColor.yellow;
          std::cerr << "Tip: Check SELECT/FROM/WHERE syntax." << std::endl;
          if (color) std::cerr << kColor.reset;
          editor.reset_render_state();
        }
      }
      return 0;
    }

    if (!query_file.empty()) {
      query = read_file(query_file);
    }
    if (query.empty()) {
      throw std::runtime_error("Missing --query or --query-file");
    }

    query = rewrite_from_path_if_needed(query);
    xsql::QueryResult result;
    auto source = parse_query_source(query);
    if (source.has_value() && source->kind == xsql::Source::Kind::Url) {
      result = xsql::execute_query_from_url(source->value, query, timeout_ms);
    } else if (source.has_value() && source->kind == xsql::Source::Kind::Path) {
      result = xsql::execute_query_from_file(source->value, query);
    } else if (input.empty() || input == "document") {
      std::string html = read_stdin();
      result = xsql::execute_query_from_document(html, query);
    } else {
      if (is_url(input)) {
        result = xsql::execute_query_from_url(input, query, timeout_ms);
      } else {
        result = xsql::execute_query_from_file(input, query);
      }
    }

    if (output_mode == "duckbox") {
      if (result.to_table) {
        if (result.tables.empty()) {
          std::cout << "(empty table)" << std::endl;
        } else {
          for (size_t i = 0; i < result.tables.size(); ++i) {
            if (result.tables.size() > 1) {
              std::cout << "Table node_id=" << result.tables[i].node_id << std::endl;
            }
            std::cout << render_table_duckbox(result.tables[i], highlight, color, 40) << std::endl;
          }
        }
      } else if (!result.to_list) {
        xsql::render::DuckboxOptions options;
        options.max_width = 0;
        options.max_rows = 40;
        options.highlight = highlight;
        options.is_tty = color;
        std::cout << xsql::render::render_duckbox(result, options) << std::endl;
      } else {
        std::string json_out = build_json_list(result);
        if (output_mode == "plain") {
          std::cout << json_out << std::endl;
        } else {
          TruncateResult truncated = truncate_output(json_out, 10, 10);
          std::cout << colorize_json(truncated.output, color) << std::endl;
        }
      }
    } else {
      std::string json_out = result.to_table ? build_table_json(result)
                            : (result.to_list ? build_json_list(result) : build_json(result));
      if (output_mode == "plain") {
        std::cout << json_out << std::endl;
      } else {
        TruncateResult truncated = truncate_output(json_out, 10, 10);
        std::cout << colorize_json(truncated.output, color) << std::endl;
      }
    }
    return 0;
  } catch (const std::exception& ex) {
    if (color) std::cerr << kColor.red;
    std::cerr << "Error: " << ex.what() << std::endl;
    if (color) std::cerr << kColor.reset;
    return 1;
  }
}
