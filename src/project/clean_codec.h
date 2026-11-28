#pragma once

/// @file
/// @brief The clean format's documents: a header naming the kind of document, its schema revision and the firmware
/// that wrote it, then its body, written and read through the model's schemas.

#include "project/controller_mapping.h"
#include "project/document.h"
#include "project/preset_document.h"
#include "serialization/schema/report.h"
#include "serialization/value.h"

#include <cstdint>
#include <expected>
#include <string_view>

namespace deluge::project {

/// @brief The schema revision this firmware's documents record. Every change to the schema bumps it; the schema-lock
/// gate (tests/golden/schema/schema.lock.json) holds the two together.
inline constexpr uint32_t kSchemaRevision = 1;

/// @brief Whether @p root is a clean-format document (it has the `deluge` header).
/// @param root A parsed document.
/// @return True for a clean document; false for a legacy one.
[[nodiscard]] bool is_clean_document(const serialization::Value& root);

/// @brief @p song as a clean document.
/// @param song     The song.
/// @param firmware The firmware writing it, recorded in the header.
/// @param plugins  The codecs of the instrument and chain nodes.
/// @return The document.
[[nodiscard]] serialization::Value encode_clean(const SongDocument& song, std::string_view firmware,
                                                const CodecPlugins& plugins);

/// @brief The song a clean document holds.
/// @param root    A parsed clean document.
/// @param plugins The codecs of the instrument and chain nodes.
/// @return The song and what its load did that the document did not say; or why it could not be read.
[[nodiscard]] std::expected<serialization::schema::Loaded<SongDocument>, serialization::schema::LoadError>
decode_clean(const serialization::Value& root, const CodecPlugins& plugins);

/// @brief The output @p output_id of @p song as a preset: its entry and record, a kit's drums, and the mappings whose
/// targets are the output or its drums.
/// @param song      The song.
/// @param output_id An output of @p song.
/// @return The preset.
[[nodiscard]] PresetDocument extract_preset(const SongDocument& song, uint32_t output_id);

/// @brief @p preset as a clean document.
/// @param preset         The preset.
/// @param tick_magnitude The input tick magnitude its chain's sync values are written relative to.
/// @param firmware       The firmware writing it.
/// @param plugins        The codecs of the instrument and chain nodes.
/// @return The document.
[[nodiscard]] serialization::Value encode_preset_clean(const PresetDocument& preset, int32_t tick_magnitude,
                                                       std::string_view firmware, const CodecPlugins& plugins);

/// @brief The preset a clean document holds.
[[nodiscard]] std::expected<serialization::schema::Loaded<PresetDocument>, serialization::schema::LoadError>
decode_preset_clean(const serialization::Value& root, const CodecPlugins& plugins);

} // namespace deluge::project
