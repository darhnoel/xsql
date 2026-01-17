#include "help_command.h"

#include <iostream>

namespace xsql::cli {

CommandHandler make_help_command() {
  return [](const std::string& line, CommandContext&) -> bool {
    if (line != ".help" && line != ":help") {
      return false;
    }
    std::cout << "Commands:\n";
    std::cout << "  .help                 Show this help\n";
    std::cout << "  .load <path|url> [--alias <name>]  Load input (or :load)\n";
    std::cout << "  .mode duckbox|json|plain  Set output mode\n";
    std::cout << "  .display_mode more|less   Control truncation\n";
    std::cout << "  .max_rows <n|inf>        Set duckbox max rows (inf = no limit)\n";
    std::cout << "  .reload_config           Reload REPL config\n";
    std::cout << "  .summarize [doc|alias|path|url]  Show tag counts\n";
    std::cout << "  .plugin list\n"
                 "  .plugin load <name|path>\n"
                 "  .plugin unload <name>\n"
                 "  .plugin install <name> [--verbose]\n"
                 "  .plugin remove <name>\n"
                 "    Manage plugins (load/install/remove/list)\n";
    std::cout << "  .summarize_content [doc|alias|path|url] [--lang <code>] [--max_tokens <n>]\n"
                 "    Summarize text content\n";
    std::cout << "  SHOW INPUT(S) / SHOW FUNCTIONS / SHOW AXES / SHOW OPERATORS\n"
                 "  DESCRIBE DOC / DESCRIBE LANGUAGE\n";
    std::cout << "  Query export: TO CSV('file.csv') / TO PARQUET('file.parquet')\n";
    std::cout << "  .quit / .q             Exit\n";
    return true;
  };
}

}  // namespace xsql::cli
