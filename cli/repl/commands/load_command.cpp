#include "load_command.h"

#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>

#include "../../cli_utils.h"
#include "../../ui/color.h"

namespace xsql::cli {

namespace {

std::vector<std::string> split_args(const std::string& line, std::string& error) {
  std::vector<std::string> out;
  std::string current;
  bool in_single = false;
  bool in_double = false;
  for (size_t i = 0; i < line.size(); ++i) {
    unsigned char ch = static_cast<unsigned char>(line[i]);
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (ch == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (!in_single && !in_double && std::isspace(ch)) {
      if (!current.empty()) {
        out.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(static_cast<char>(ch));
  }
  if (in_single || in_double) {
    error = "Error: unterminated quoted path in .load";
    return {};
  }
  if (!current.empty()) {
    out.push_back(current);
  }
  return out;
}

}  // namespace

CommandHandler make_load_command() {
  return [](const std::string& line, CommandContext& ctx) -> bool {
    if (line.rfind(":load", 0) != 0 && line.rfind(".load", 0) != 0) {
      return false;
    }
    std::string error;
    auto args = split_args(trim_semicolon(line), error);
    if (!error.empty()) {
      std::cerr << error << std::endl;
      return true;
    }
    if (args.size() < 2) {
      std::cerr << "Usage: .load <path|url> [--alias <name>] or :load <path|url> [--alias <name>]"
                << std::endl;
      return true;
    }
    std::string path;
    std::string alias;
    for (size_t i = 1; i < args.size(); ++i) {
      const std::string& arg = args[i];
      if (arg == "--alias") {
        if (i + 1 >= args.size()) {
          std::cerr << "Error: missing value for --alias" << std::endl;
          return true;
        }
        alias = args[++i];
        continue;
      }
      if (!arg.empty() && arg[0] == '-') {
        std::cerr << "Error: unknown option " << arg << std::endl;
        return true;
      }
      if (path.empty()) {
        path = arg;
      } else {
        std::cerr << "Error: only one path or URL is allowed for .load" << std::endl;
        return true;
      }
    }
    if (path.empty()) {
      std::cerr << "Usage: .load <path|url> [--alias <name>] or :load <path|url> [--alias <name>]"
                << std::endl;
      return true;
    }
    if (alias.empty()) {
      alias = "doc";
    }
    if (path == "doc" || path == "document") {
      std::cerr << "Error: .load doc is not valid. Use .load <path|url> to load data." << std::endl;
      return true;
    }
    if (is_url(path)) {
#ifndef XSQL_USE_CURL
      std::cerr << "Error: URL fetching is disabled (libcurl not available). Install libcurl and rebuild." << std::endl;
      return true;
#endif
    } else {
      if (!std::filesystem::exists(path)) {
        std::cerr << "Error: file not found: " << path << std::endl;
        return true;
      }
    }
    try {
      LoadedSource entry;
      entry.source = path;
      entry.html = load_html_input(path, ctx.config.timeout_ms);
      ctx.sources[alias] = std::move(entry);
    } catch (const std::exception& ex) {
      if (ctx.config.color) std::cerr << kColor.red;
      std::cerr << "Error: " << ex.what() << std::endl;
      if (ctx.config.color) std::cerr << kColor.reset;
      return true;
    }
    ctx.active_alias = alias;
    std::cout << "Loaded: " << path << " as " << alias << std::endl;
    return true;
  };
}

}  // namespace xsql::cli
