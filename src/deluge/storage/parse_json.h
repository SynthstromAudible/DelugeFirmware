#pragma once

#include "storage/value.h"
#include <optional>
#include <string_view>

namespace deluge::storage {

/// Parses a JSON document into a format-agnostic Value tree.
///
/// Objects become an ordered Map (document order preserved); arrays become an
/// Array; strings/numbers/bools/null all become a Scalar holding the *raw
/// token text* -- strings are unescaped and have their surrounding quotes
/// stripped, but numbers/true/false/null are stored verbatim. This lets the
/// resulting text round-trip through the same readTagOrAttributeValue*
/// parsing the XML/file-loading path uses.
///
/// Returns std::nullopt on any malformed input (unterminated structures,
/// missing ':'/',', trailing garbage, invalid escapes, etc).
[[nodiscard]] std::optional<Value> parseJson(std::string_view json);

} // namespace deluge::storage
