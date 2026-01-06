#pragma once

#include <string>
#include <vector>

namespace xsql::cli {

/// Provides keyword and command completion for the REPL editor.
/// MUST preserve user casing conventions and MUST not mutate unrelated text.
/// Inputs are buffer/cursor; outputs are edits/suggestions with no side effects.
class AutoCompleter {
 public:
  /// Initializes the completion dictionaries for SQL keywords and commands.
  /// MUST keep lists in sync with supported grammar and REPL commands.
  /// Inputs are none; side effects are internal list initialization only.
  AutoCompleter();

  /// Attempts to complete the word under the cursor and gather suggestions.
  /// MUST only modify the buffer on unambiguous completions.
  /// Inputs are buffer/cursor; outputs are updated buffer/cursor and suggestions.
  bool complete(std::string& buffer, size_t& cursor, std::vector<std::string>& suggestions) const;

 private:
  /// Defines which characters belong to a completion token.
  /// MUST match parser expectations to avoid broken completions.
  /// Inputs are a character; outputs are boolean with no side effects.
  static bool is_word_char(char c);

  /// Normalizes a token for case-insensitive matching.
  /// MUST preserve byte order and MUST not alter non-ASCII bytes.
  /// Inputs are raw strings; outputs are lowercase strings.
  static std::string to_lower(const std::string& s);

  /// Checks prefix matching used for completion filtering.
  /// MUST be exact on normalized input to keep suggestions consistent.
  /// Inputs are strings; outputs are boolean with no side effects.
  static bool starts_with(const std::string& value, const std::string& prefix);

  /// Finds the shared lowercase prefix to decide automatic completion.
  /// MUST return a prefix common to all values or empty when none.
  /// Inputs are candidate strings; outputs are the common prefix.
  static std::string longest_common_prefix_lower(const std::vector<std::string>& values);

  /// Applies a casing pattern to the suggested completion.
  /// MUST preserve the user's casing intent without altering semantic text.
  /// Inputs are pattern/value; outputs are cased suggestion strings.
  static std::string apply_case(const std::string& pattern, const std::string& value);

  std::vector<std::string> keywords_;
  std::vector<std::string> commands_;
};

}  // namespace xsql::cli
