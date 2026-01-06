#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace xsql::cli {

/// Manages REPL command history with navigation state.
/// MUST preserve the current buffer when navigating history.
/// Inputs are edits/lines; outputs are history mutations with no I/O.
class History {
 public:
  /// Initializes history with a maximum number of entries.
  /// MUST keep max_entries > 0 to avoid unbounded growth.
  /// Inputs are limits; side effects are internal storage setup.
  explicit History(size_t max_entries);

  /// Resets navigation to the end of history.
  /// MUST be called before a new input session to preserve buffer restore.
  /// Inputs are none; side effects are navigation state reset.
  void reset_navigation();

  /// Returns true if there are no history entries.
  /// MUST be safe to call at any time without side effects.
  bool empty() const;

  /// Appends a line to history, avoiding consecutive duplicates.
  /// MUST trim history to max size.
  /// Inputs are line text; side effects include history mutations.
  void add(const std::string& line);

  /// Moves to the previous history entry.
  /// MUST capture the current buffer when entering history navigation.
  /// Inputs are current buffer; outputs are updated buffer and navigation state.
  bool prev(std::string& buffer);

  /// Moves to the next history entry.
  /// MUST restore the saved buffer when exiting history navigation.
  /// Inputs are current buffer; outputs are updated buffer and navigation state.
  bool next(std::string& buffer);

 private:
  size_t max_entries_;
  std::vector<std::string> entries_;
  size_t index_ = 0;
  std::string current_buffer_;
};

}  // namespace xsql::cli
