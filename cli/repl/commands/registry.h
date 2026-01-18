#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "repl/core/repl.h"
#include "repl/core/line_editor.h"

namespace xsql::cli {

class PluginManager;

struct CommandContext {
  ReplConfig& config;
  LineEditor& editor;
  std::unordered_map<std::string, LoadedSource>& sources;
  std::string& active_alias;
  std::string& last_full_output;
  bool& display_full;
  size_t& max_rows;
  PluginManager& plugin_manager;
};

using CommandHandler = std::function<bool(const std::string&, CommandContext&)>;

class CommandRegistry {
 public:
  void add(CommandHandler handler);
  bool try_handle(const std::string& line, CommandContext& ctx) const;

 private:
  std::vector<CommandHandler> handlers_;
};

void register_default_commands(CommandRegistry& registry);

}  // namespace xsql::cli
