#pragma once

#include <string>

namespace xsql::util {

/// Converts a string to lowercase for case-insensitive comparisons.
/// MUST avoid locale-sensitive behavior to keep parsing deterministic.
/// Inputs are strings; outputs are lowercase strings with no side effects.
std::string to_lower(const std::string& s);
/// Converts a string to uppercase for keyword matching.
/// MUST avoid locale-sensitive behavior to keep parsing deterministic.
/// Inputs are strings; outputs are uppercase strings with no side effects.
std::string to_upper(const std::string& s);
/// Trims leading and trailing ASCII whitespace.
/// MUST preserve internal whitespace and MUST not modify the input.
/// Inputs are strings; outputs are trimmed strings with no side effects.
std::string trim_ws(const std::string& s);

}  // namespace xsql::util
