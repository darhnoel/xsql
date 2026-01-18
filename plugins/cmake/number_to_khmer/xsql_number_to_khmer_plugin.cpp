#include "xsql/plugin_api.h"

#include <cctype>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>

#include "xsql/khmer_number.h"

namespace {

struct KhmerNumberPluginState {
  void* host_context = nullptr;
  void (*print)(void*, const char*, bool) = nullptr;
};

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

bool write_error(char* out_error, size_t out_error_size, const std::string& message) {
  if (out_error && out_error_size > 0) {
    std::snprintf(out_error, out_error_size, "%s", message.c_str());
  }
  return false;
}

bool handle_to_words(const char* line,
                     void* user_data,
                     char* out_error,
                     size_t out_error_size) {
  auto* state = static_cast<KhmerNumberPluginState*>(user_data);
  if (!state || !state->print) {
    return write_error(out_error, out_error_size, "Plugin host not available.");
  }
  if (!line) {
    return write_error(out_error, out_error_size, "Missing input line.");
  }
  std::string_view view(line);
  std::string_view cmd = ".number_to_khmer";
  if (view.rfind(cmd, 0) == 0) {
    view.remove_prefix(cmd.size());
  }
  std::istringstream iss(std::string(trim_ws(view)));
  std::string token;
  std::string arg;
  bool compact = false;
  bool numerals = false;
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
      arg += token;
    }
  }
  if (arg.empty()) {
    return write_error(out_error,
                       out_error_size,
                       "Usage: .number_to_khmer <number> [--compact] [--khmer-digits]");
  }
  auto result = numerals
                    ? xsql::khmer_number::number_to_khmer_numerals(arg)
                    : xsql::khmer_number::number_to_khmer_words(arg);
  if (!result.ok) {
    return write_error(out_error, out_error_size, result.error);
  }
  if (compact && !numerals) {
    result.value.erase(
        std::remove(result.value.begin(), result.value.end(), '-'),
        result.value.end());
  }
  state->print(state->host_context, result.value.c_str(), false);
  return true;
}

bool handle_to_number(const char* line,
                      void* user_data,
                      char* out_error,
                      size_t out_error_size) {
  auto* state = static_cast<KhmerNumberPluginState*>(user_data);
  if (!state || !state->print) {
    return write_error(out_error, out_error_size, "Plugin host not available.");
  }
  if (!line) {
    return write_error(out_error, out_error_size, "Missing input line.");
  }
  std::string_view view(line);
  std::string_view cmd = ".khmer_to_number";
  if (view.rfind(cmd, 0) == 0) {
    view.remove_prefix(cmd.size());
  }
  std::istringstream iss(std::string(trim_ws(view)));
  std::string token;
  std::string arg;
  bool numerals = false;
  while (iss >> token) {
    if (token == "--khmer-digits" || token == "--numerals") {
      numerals = true;
      continue;
    }
    if (arg.empty()) {
      arg = token;
    } else {
      arg += " " + token;
    }
  }
  if (arg.empty()) {
    return write_error(out_error,
                       out_error_size,
                       "Usage: .khmer_to_number <khmer_text> [--khmer-digits]");
  }
  auto result = xsql::khmer_number::khmer_words_to_number(arg);
  if (!result.ok) {
    return write_error(out_error, out_error_size, result.error);
  }
  if (numerals) {
    auto numerals_result =
        xsql::khmer_number::number_to_khmer_numerals(result.value);
    if (!numerals_result.ok) {
      return write_error(out_error, out_error_size, numerals_result.error);
    }
    state->print(state->host_context, numerals_result.value.c_str(), false);
    return true;
  }
  state->print(state->host_context, result.value.c_str(), false);
  return true;
}

}  // namespace

extern "C" bool xsql_register_plugin(const XsqlPluginHost* host,
                                      char* out_error,
                                      size_t out_error_size) {
  if (!host || host->api_version != XSQL_PLUGIN_API_VERSION) {
    if (out_error && out_error_size > 0) {
      std::snprintf(out_error, out_error_size, "Unsupported plugin API version.");
    }
    return false;
  }
  static KhmerNumberPluginState state;
  state.host_context = host->host_context;
  state.print = host->print;
  if (!host->register_command(host->host_context,
                              "number_to_khmer",
                              "Convert number to Khmer words or numerals",
                              &handle_to_words,
                              &state,
                              out_error,
                              out_error_size)) {
    return false;
  }
  if (!host->register_command(host->host_context,
                              "khmer_to_number",
                              "Convert Khmer words to number",
                              &handle_to_number,
                              &state,
                              out_error,
                              out_error_size)) {
    return false;
  }
  return true;
}
