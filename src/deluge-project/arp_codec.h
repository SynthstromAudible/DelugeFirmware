#pragma once

/// @file
/// @brief An arp's element: its record's fields as a file writes them.
///
/// A clip's element and a MIDI or gate kit row's hold the sync level itself. A sound's element (a sound kit row's,
/// a synth preset's) holds it relative to the song's input tick magnitude. The randomizer's locked values and the note
/// pattern are the engine's; neither function reads or writes them.

#include "deluge-project/arp_record.h"
#include "serialization/value.h"

#include <cstdint>
#include <optional>

namespace deluge::project {

/// @brief @p record's settings as an arp element's attributes, in the order the firmware writes them.
/// @param record         The arp's record.
/// @param tick_magnitude As encode's.
/// @return The settings' attributes.
[[nodiscard]] serialization::Value encode_settings(const ArpRecord& record, std::optional<int32_t> tick_magnitude);

/// @brief The host values @p record holds as an arp element's attributes, in the order the firmware writes them.
/// @param record The arp's record.
/// @return The attributes; none where the record holds no host value.
[[nodiscard]] serialization::Value encode_host(const ArpRecord& record);

/// @brief @p record as an arp element's attributes, in the order the firmware writes them.
/// @param record         The arp's record.
/// @param tick_magnitude The song's input tick magnitude for a sound's element; nullopt for a clip's or a non-audio
///                       kit row's.
/// @return The element: the settings, then each host value the record holds.
[[nodiscard]] serialization::Value encode(const ArpRecord& record, std::optional<int32_t> tick_magnitude);

/// @brief Read an arp element's settings and host values into @p record. Attributes the element lacks keep
///        @p record's values; an older file's single `mode` word sets the mode, octave mode and note mode when the
///        element has no `arpMode`.
/// @param arpElement     The arp element.
/// @param record         The record to fill.
/// @param tick_magnitude As encode's.
void decode(const serialization::Value& arpElement, ArpRecord& record, std::optional<int32_t> tick_magnitude);

} // namespace deluge::project
