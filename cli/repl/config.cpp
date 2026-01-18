#include "config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "repl/core/line_editor.h"

namespace xsql::cli {

namespace {

std::string get_env(const char* name) {
  if (const char* value = std::getenv(name)) {
    if (*value) return value;
  }
  return {};
}

std::string trim_copy(const std::string& value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

bool parse_bool(const std::string& raw, bool& out) {
  std::string lower;
  lower.reserve(raw.size());
  for (unsigned char c : raw) {
    lower.push_back(static_cast<char>(std::tolower(c)));
  }
  if (lower == "true") {
    out = true;
    return true;
  }
  if (lower == "false") {
    out = false;
    return true;
  }
  return false;
}

bool parse_size(const std::string& raw, size_t& out) {
  try {
    size_t pos = 0;
    unsigned long long value = std::stoull(raw, &pos);
    if (pos != raw.size()) return false;
    if (value == 0) return false;
    out = static_cast<size_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

std::string parse_string_value(const std::string& raw, bool& ok) {
  std::string trimmed = trim_copy(raw);
  if (trimmed.empty()) {
    ok = false;
    return {};
  }
  if ((trimmed.front() == '"' && trimmed.back() == '"') ||
      (trimmed.front() == '\'' && trimmed.back() == '\'')) {
    if (trimmed.size() < 2) {
      ok = false;
      return {};
    }
    ok = true;
    return trimmed.substr(1, trimmed.size() - 2);
  }
  ok = true;
  return trimmed;
}

}  // namespace

std::string resolve_repl_config_path() {
  std::string override = get_env("XSQL_CONFIG");
  if (!override.empty()) {
    return override;
  }
  std::string xdg_config = get_env("XDG_CONFIG_HOME");
  if (!xdg_config.empty()) {
    return (std::filesystem::path(xdg_config) / "xsql" / "config.toml").string();
  }
  std::string home = get_env("HOME");
  if (!home.empty()) {
    return (std::filesystem::path(home) / ".config" / "xsql" / "config.toml").string();
  }
  return "xsql.config.toml";
}

std::string resolve_default_history_path() {
  std::string xdg_state = get_env("XDG_STATE_HOME");
  if (!xdg_state.empty()) {
    return (std::filesystem::path(xdg_state) / "xsql" / "history").string();
  }
  std::string home = get_env("HOME");
  if (!home.empty()) {
    return (std::filesystem::path(home) / ".local" / "state" / "xsql" / "history").string();
  }
  return "xsql.history";
}

std::string expand_user_path(const std::string& raw) {
  if (raw.empty()) return raw;
  if (raw[0] == '~') {
    std::string home = get_env("HOME");
    if (home.empty()) return raw;
    if (raw.size() == 1) return home;
    if (raw[1] == '/') {
      return home + raw.substr(1);
    }
  }
  if (raw.rfind("$HOME/", 0) == 0) {
    std::string home = get_env("HOME");
    if (!home.empty()) {
      return home + raw.substr(5);
    }
  }
  return raw;
}

bool load_repl_config(const std::string& path, ReplSettings& out, std::string& error) {
  out = ReplSettings{};
  if (path.empty()) return false;
  if (!std::filesystem::exists(path)) {
    return false;
  }
  std::ifstream in(path);
  if (!in) {
    error = "Failed to open config: " + path;
    return false;
  }
  std::string section;
  std::string line;
  size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    std::string trimmed = trim_copy(line);
    if (trimmed.empty()) continue;
    if (trimmed[0] == '#') continue;
    if (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/') continue;
    if (trimmed.front() == '[' && trimmed.back() == ']') {
      section = trim_copy(trimmed.substr(1, trimmed.size() - 2));
      continue;
    }
    size_t eq = trimmed.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim_copy(trimmed.substr(0, eq));
    std::string value = trim_copy(trimmed.substr(eq + 1));
    if (key.empty()) continue;
    std::string full_key = section.empty() ? key : section + "." + key;
    bool ok = false;
    if (full_key == "repl.history.max_entries") {
      size_t parsed = 0;
      if (!parse_size(value, parsed)) {
        error = "Invalid repl.history.max_entries at line " + std::to_string(line_no);
        return false;
      }
      out.history_max_entries = parsed;
    } else if (full_key == "repl.history.path") {
      std::string parsed = parse_string_value(value, ok);
      if (!ok) {
        error = "Invalid repl.history.path at line " + std::to_string(line_no);
        return false;
      }
      out.history_path = parsed;
    } else if (full_key == "repl.display_mode") {
      std::string parsed = parse_string_value(value, ok);
      if (!ok) {
        error = "Invalid repl.display_mode at line " + std::to_string(line_no);
        return false;
      }
      std::string lower;
      lower.reserve(parsed.size());
      for (unsigned char c : parsed) lower.push_back(static_cast<char>(std::tolower(c)));
      if (lower == "more") {
        out.display_full = true;
      } else if (lower == "less") {
        out.display_full = false;
      } else {
        error = "Invalid repl.display_mode value at line " + std::to_string(line_no);
        return false;
      }
    } else if (full_key == "repl.max_rows") {
      std::string parsed = parse_string_value(value, ok);
      if (!ok) {
        error = "Invalid repl.max_rows at line " + std::to_string(line_no);
        return false;
      }
      std::string lower;
      lower.reserve(parsed.size());
      for (unsigned char c : parsed) lower.push_back(static_cast<char>(std::tolower(c)));
      if (lower == "inf" || lower == "infinite" || lower == "unlimited") {
        out.max_rows = 0;
      } else {
        size_t parsed_num = 0;
        if (!parse_size(parsed, parsed_num)) {
          error = "Invalid repl.max_rows at line " + std::to_string(line_no);
          return false;
        }
        out.max_rows = parsed_num;
      }
    } else if (full_key == "repl.output_mode") {
      std::string parsed = parse_string_value(value, ok);
      if (!ok) {
        error = "Invalid repl.output_mode at line " + std::to_string(line_no);
        return false;
      }
      out.output_mode = parsed;
    } else if (full_key == "repl.highlight") {
      bool parsed = false;
      if (!parse_bool(value, parsed)) {
        error = "Invalid repl.highlight at line " + std::to_string(line_no);
        return false;
      }
      out.highlight = parsed;
    }
  }
  return true;
}

bool apply_repl_settings(const ReplSettings& settings,
                         ReplConfig& config,
                         LineEditor& editor,
                         bool& display_full,
                         size_t& max_rows,
                         std::string& error) {
  if (settings.history_max_entries.has_value()) {
    editor.set_history_size(*settings.history_max_entries);
  }
  std::string history_path = settings.history_path.value_or(resolve_default_history_path());
  history_path = expand_user_path(history_path);
  if (!editor.set_history_path(history_path, error)) {
    return false;
  }
  if (settings.output_mode.has_value()) {
    config.output_mode = *settings.output_mode;
  }
  if (settings.highlight.has_value()) {
    config.highlight = *settings.highlight;
  }
  if (settings.display_full.has_value()) {
    display_full = *settings.display_full;
  }
  if (settings.max_rows.has_value()) {
    max_rows = *settings.max_rows;
  }
  return true;
}

}  // namespace xsql::cli
