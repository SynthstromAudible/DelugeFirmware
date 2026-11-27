#pragma once

/// @file
/// @brief The schema as data: what the schema lock records, so that any change to the format is a visible diff.

#include "serialization/schema/descriptor.h"
#include "serialization/value.h"

#include <cstdint>
#include <span>

namespace deluge::serialization::schema {

/// @brief The schema of @p roots and of every record they reach.
/// @param roots    The top-level records (a song's, a preset's).
/// @param revision The schema revision the documents record.
/// @return `{"revision": n, "records": […]}`: each record once, after the records its fields reach.
[[nodiscard]] Value dump_schema(std::span<const RecordDesc* const> roots, uint32_t revision);

} // namespace deluge::serialization::schema
