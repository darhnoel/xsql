#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "repl/state/history.h"

namespace xsql::cli {

class AutoCompleter;

/// Provides interactive line editing with history and multi-line support.
/// MUST operate only on TTY input and MUST fall back to std::getline otherwise.
/// Inputs are keystrokes; outputs are completed lines with terminal side effects.
class LineEditor {
 public:
  /// Constructs the editor with initial prompt and history capacity.
  /// MUST keep prompt_len consistent with visible prompt width.
  /// Inputs are history size/prompt; side effects include terminal state changes.
  LineEditor(size_t max_history, std::string prompt, size_t prompt_len);
  /// Destroys the editor and restores terminal state when needed.
  /// MUST leave the terminal in a sane state even on early exits.
  /// Inputs are none; side effects include terminal restoration.
  ~LineEditor();

  /// Updates the primary prompt and its visible width for cursor math.
  /// MUST keep prompt_len accurate to avoid cursor drift.
  /// Inputs are prompt text/length; side effects include next redraw behavior.
  void set_prompt(std::string prompt, size_t prompt_len);
  /// Updates the continuation prompt for multi-line input.
  /// MUST keep prompt_len accurate for wrapped cursor positioning.
  /// Inputs are prompt text/length; side effects include next redraw behavior.
  void set_cont_prompt(std::string prompt, size_t prompt_len);
  /// Toggles keyword coloring for SQL-like tokens.
  /// MUST be false when output is not a TTY to avoid escape noise.
  /// Inputs are a boolean; side effects are ANSI-colored output.
  void set_keyword_color(bool enabled);
  /// Resets render bookkeeping after printing external output.
  /// MUST be called after writes that disturb the cursor position.
  /// Inputs are none; side effects are internal state resets only.
  void reset_render_state();
  /// Reads a line with editing, history, and multi-line support.
  /// MUST return false on EOF and MUST preserve buffer contents on edits.
  /// Inputs are optional initial text; outputs are the completed line.
  bool read_line(std::string& out, const std::string& initial = {});
  /// Appends a line to history when appropriate.
  /// MUST avoid duplicate consecutive entries to reduce noise.
  /// Inputs are line text; side effects are history mutations.
  void add_history(const std::string& line);

 private:
  /// Repaints the input buffer and moves the cursor to the correct position.
  /// MUST keep cursor math consistent with wrapping and continuation prompts.
  /// Inputs are buffer/cursor; side effects include terminal escape output.
  void redraw_line(const std::string& buffer, size_t cursor);
  /// Checks whether a token should be colored as a SQL keyword.
  /// MUST be case-insensitive and MUST remain synchronized with keywords list.
  /// Inputs are a token string; outputs are boolean with no side effects.
  static bool is_sql_keyword(const std::string& word);

  History history_;
  std::string prompt_;
  size_t prompt_len_ = 0;
  std::string cont_prompt_;
  size_t cont_prompt_len_ = 0;
  int last_render_lines_ = 1;
  int last_cursor_line_ = 0;
  bool keyword_color_ = false;

  std::unique_ptr<AutoCompleter> completer_;
};

}  // namespace xsql::cli
