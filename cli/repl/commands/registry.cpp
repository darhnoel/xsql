#include "registry.h"

#include "display_mode_command.h"
#include "help_command.h"
#include "load_command.h"
#include "max_rows_command.h"
#include "mode_command.h"
#include "plugin_command.h"
#include "reload_config_command.h"
#include "summarize_content_command.h"
#include "summarize_command.h"

namespace xsql::cli {

void CommandRegistry::add(CommandHandler handler) {
  handlers_.push_back(std::move(handler));
}

bool CommandRegistry::try_handle(const std::string& line, CommandContext& ctx) const {
  for (const auto& handler : handlers_) {
    if (handler(line, ctx)) {
      return true;
    }
  }
  return false;
}

void register_default_commands(CommandRegistry& registry) {
  registry.add(make_help_command());
  registry.add(make_display_mode_command());
  registry.add(make_mode_command());
  registry.add(make_max_rows_command());
  registry.add(make_reload_config_command());
  registry.add(make_plugin_command());
  registry.add(make_summarize_content_command());
  registry.add(make_summarize_command());
  registry.add(make_load_command());
}

}  // namespace xsql::cli
