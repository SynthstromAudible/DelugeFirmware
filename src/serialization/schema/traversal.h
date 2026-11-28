#pragma once

/// @file
/// @brief The generic walk: a record written and read through its descriptor.

#include "serialization/schema/descriptor.h"

#include <expected>

namespace deluge::serialization::schema {

/// @brief Write @p record as a record: its fields in table order (a field whose key its extras hold is written from
/// them instead), then its kept unknown keys.
/// @param desc    The record's descriptor.
/// @param record  The object @p desc describes.
/// @param out     Where to write.
/// @param context The walk's context; encoding notes nothing.
void encode_record(const RecordDesc& desc, const void* record, Writer& out, const WalkContext& context);

/// @brief Read the record @p in is on into @p record. A key the document lacks keeps @p record's value.
/// @param desc    The record's descriptor.
/// @param record  The object @p desc describes, holding the values to keep.
/// @param in      Where to read; positioned on the record.
/// @param context The walk's context: its path names where, its report collects what the document did not say.
/// @return Nothing, or why the document could not be read and where.
[[nodiscard]] std::expected<void, LoadError> decode_record(const RecordDesc& desc, void* record, Reader& in,
                                                           WalkContext& context);

} // namespace deluge::serialization::schema
