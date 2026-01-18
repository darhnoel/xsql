#include "reload_config_command.h"

#include <filesystem>
#include <iostream>

#include "../config.h"
#include "../../ui/color.h"

namespace xsql::cli {

CommandHandler make_reload_config_command() {
  return [](const std::string& line, CommandContext& ctx) -> bool {
    if (line != ".reload_config" && line != ":reload_config") {
      return false;
    }
    std::string path = resolve_repl_config_path();
    ReplSettings settings;
    std::string error;
    if (!load_repl_config(path, settings, error)) {
      if (!error.empty()) {
        if (ctx.config.color) std::cerr << kColor.red;
        std::cerr << "Error: " << error << std::endl;
        if (ctx.config.color) std::cerr << kColor.reset;
        return true;
      }
      if (!std::filesystem::exists(path)) {
        std::cerr << "No config found at: " << path << std::endl;
        return true;
      }
    }
    std::string apply_error;
    if (!apply_repl_settings(settings, ctx.config, ctx.editor, ctx.display_full,
                             ctx.max_rows, apply_error)) {
      if (ctx.config.color) std::cerr << kColor.red;
      std::cerr << "Error: " << apply_error << std::endl;
      if (ctx.config.color) std::cerr << kColor.reset;
      return true;
    }
    std::cout << "Reloaded config: " << path << std::endl;
    return true;
  };
}

}  // namespace xsql::cli
