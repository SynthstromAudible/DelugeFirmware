#pragma once

#include "serialization/value.h"

#include <string>

namespace deluge::serialization {

/// @brief @p document as JSON text: a Map as an object (entries in order, duplicate keys kept), an Array as an array,
/// TEXT scalars as escaped strings, NUMBER and LITERAL scalars bare; one entry per line, tab-indented, with a trailing
/// newline. parseJson() reads it back to an equal tree.
[[nodiscard]] std::string write_json(const Value& document);

} // namespace deluge::serialization
