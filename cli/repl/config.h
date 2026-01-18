#pragma once

#include <optional>
#include <string>

#include "repl/core/repl.h"

namespace xsql::cli {

class LineEditor;

struct ReplSettings {
  std::optional<size_t> history_max_entries;
  std::optional<std::string> history_path;
  std::optional<std::string> output_mode;
  std::optional<bool> highlight;
  std::optional<bool> display_full;
  std::optional<size_t> max_rows;
};

std::string resolve_repl_config_path();
bool load_repl_config(const std::string& path, ReplSettings& out, std::string& error);
std::string resolve_default_history_path();
bool apply_repl_settings(const ReplSettings& settings,
                         ReplConfig& config,
                         LineEditor& editor,
                         bool& display_full,
                         size_t& max_rows,
                         std::string& error);

}  // namespace xsql::cli
