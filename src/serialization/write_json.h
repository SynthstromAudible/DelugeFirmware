#pragma once

#include "serialization/value.h"

#include <string>

namespace deluge::serialization {

/// @brief Serialize @p document as JSON text.
///
/// A Map becomes an object (entries in order, duplicate keys kept), an Array an array, TEXT scalars escaped strings,
/// NUMBER and LITERAL scalars bare; one entry per line, tab-indented, with a trailing newline. parseJson() reads it
/// back to an equal tree.
/// @param document The tree to write.
/// @return The JSON text.
[[nodiscard]] std::string write_json(const Value& document);

} // namespace deluge::serialization
