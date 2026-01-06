#include "max_rows_command.h"

#include <iostream>
#include <sstream>

namespace xsql::cli {

CommandHandler make_max_rows_command() {
  return [](const std::string& line, CommandContext& ctx) -> bool {
    if (line.rfind(".max_rows", 0) != 0) {
      return false;
    }
    std::istringstream iss(line);
    std::string cmd;
    std::string value;
    iss >> cmd >> value;
    if (value == "inf" || value == "infinite" || value == "unlimited") {
      ctx.max_rows = 0;
      std::cout << "Duckbox max rows: unlimited" << std::endl;
    } else if (!value.empty()) {
      try {
        size_t parsed = std::stoull(value);
        if (parsed == 0) {
          std::cerr << "Use .max_rows inf for unlimited" << std::endl;
        } else {
          ctx.max_rows = parsed;
          std::cout << "Duckbox max rows: " << ctx.max_rows << std::endl;
        }
      } catch (const std::exception&) {
        std::cerr << "Usage: .max_rows <n|inf>" << std::endl;
      }
    } else {
      std::cerr << "Usage: .max_rows <n|inf>" << std::endl;
    }
    return true;
  };
}

}  // namespace xsql::cli
