#include "display_mode_command.h"

#include <iostream>
#include <sstream>

namespace xsql::cli {

CommandHandler make_display_mode_command() {
  return [](const std::string& line, CommandContext& ctx) -> bool {
    if (line.rfind(".display_mode", 0) != 0) {
      return false;
    }
    std::istringstream iss(line);
    std::string cmd;
    std::string mode;
    iss >> cmd >> mode;
    if (mode == "more") {
      ctx.display_full = true;
      std::cout << "Display mode: more" << std::endl;
    } else if (mode == "less") {
      ctx.display_full = false;
      std::cout << "Display mode: less" << std::endl;
    } else {
      std::cerr << "Usage: .display_mode more|less" << std::endl;
    }
    return true;
  };
}

}  // namespace xsql::cli
