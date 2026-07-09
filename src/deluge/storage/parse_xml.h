#pragma once

#include "storage/value.h"
#include <optional>
#include <string_view>

namespace deluge::storage {

/// Parses one Deluge-format XML document into a format-agnostic Value tree.
///
/// An element becomes a Map; each attribute `name="val"` becomes a Scalar
/// entry (name -> val), in document order; a text-only element with no
/// attributes and no child elements (`<n>text</n>`) becomes a Scalar entry
/// (n -> text); a structured child element becomes a nested Map entry;
/// repeated child element names produce repeated ordered entries in the
/// parent Map (never merged/overwritten); a self-closing element (`<n/>`)
/// becomes an empty Map entry. The `<?xml ...?>` prolog, if present, is
/// skipped, as is a leading/trailing DOCTYPE-free run of whitespace.
///
/// This parser does not disambiguate the attributes-and-text-together case:
/// the Deluge format never mixes them on one element, so "has attributes or
/// child elements" always yields a Map and "text only, no attributes, no
/// children" always yields a Scalar.
///
/// Values are raw text with the five predefined XML entities unescaped
/// (&amp; &lt; &gt; &quot; &apos;); no other numeric/entity references are
/// interpreted. Comments (<!-- ... -->) are skipped wherever whitespace is
/// permitted.
///
/// Returns std::nullopt on any malformed input (unterminated tags, mismatched
/// open/close names, trailing garbage after the root element, etc).
[[nodiscard]] std::optional<Value> parseXml(std::string_view xml);

} // namespace deluge::storage
