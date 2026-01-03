#include "html_parser.h"

#include "html/parser_impl.h"

namespace xsql {

/// Dispatches HTML parsing to the selected backend.
/// MUST choose libxml2 when enabled and MUST fall back deterministically otherwise.
/// Inputs are HTML strings; outputs are HtmlDocument with no side effects.
HtmlDocument parse_html(const std::string& html) {
#ifdef XSQL_USE_LIBXML2
  return parse_html_libxml2(html);
#else
  // WHY: fallback parser keeps offline builds working without libxml2.
  return parse_html_naive(html);
#endif
}

}  // namespace xsql
