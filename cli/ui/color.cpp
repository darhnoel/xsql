#include "color.h"

namespace xsql::cli {

/// Holds the singleton color palette used by CLI rendering.
/// MUST match the declaration in the header and MUST not be redefined elsewhere.
/// Inputs are none; side effects occur when consumers print ANSI codes.
Color kColor;

}  // namespace xsql::cli
