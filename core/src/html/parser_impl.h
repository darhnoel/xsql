#pragma once

#include <string>

#include "../html_parser.h"

namespace xsql {

/// Parses HTML using the minimal fallback parser for environments without libxml2.
/// MUST be deterministic and MUST not execute scripts.
/// Inputs are HTML strings; outputs are HtmlDocument with no side effects.
HtmlDocument parse_html_naive(const std::string& html);
/// Parses HTML using libxml2 when available.
/// MUST follow libxml2 recovery behavior and MUST not execute scripts.
/// Inputs are HTML strings; outputs are HtmlDocument with no side effects.
HtmlDocument parse_html_libxml2(const std::string& html);

}  // namespace xsql
