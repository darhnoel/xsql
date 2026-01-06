#include "load_command.h"

#include <filesystem>
#include <iostream>
#include <sstream>

#include "../../cli_utils.h"
#include "../../ui/color.h"

namespace xsql::cli {

CommandHandler make_load_command() {
  return [](const std::string& line, CommandContext& ctx) -> bool {
    if (line.rfind(":load", 0) != 0 && line.rfind(".load", 0) != 0) {
      return false;
    }
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    std::string path;
    iss >> path;
    if (path.empty()) {
      std::cerr << "Usage: .load <path|url> or :load <path|url>" << std::endl;
      return true;
    }
    path = trim_semicolon(path);
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
      ctx.active_html = load_html_input(path, ctx.config.timeout_ms);
    } catch (const std::exception& ex) {
      if (ctx.config.color) std::cerr << kColor.red;
      std::cerr << "Error: " << ex.what() << std::endl;
      if (ctx.config.color) std::cerr << kColor.reset;
      return true;
    }
    ctx.active_source = path;
    std::cout << "Loaded: " << ctx.active_source << std::endl;
    return true;
  };
}

}  // namespace xsql::cli
