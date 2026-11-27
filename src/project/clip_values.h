#pragma once

/// @file
/// @brief A clip's value layer: the values a clip holds for settings a track also has, resolved clip first.
///
/// Each value is optional. A clip that holds one plays it; one that does not plays its track's. Today the track
/// holds none of these, so a caller passes the setting's default as the track's value.

#include "project/arp_record.h"
#include "serialization/schema/extras.h"

#include <cstdint>
#include <optional>

namespace deluge::project {

/// The MIDI program, bank and sub-bank a clip's values hold: 0..127, and this for none (no message sent).
inline constexpr int32_t kNoMidiProgram = 128;
/// The most semitones a bend range has, as its menus offer them.
inline constexpr int32_t kMaxBendRange = 96;

/// @brief A clip's own values, by setting path.
struct ClipValues {
	std::optional<int32_t> midi_bank;      ///< `midiBank`, 0..127, 128 = none
	std::optional<int32_t> midi_sub;       ///< `midiSub`, 0..127, 128 = none
	std::optional<int32_t> midi_pgm;       ///< `midiPGM`, 0..127, 128 = none
	std::optional<int32_t> bend_range;     ///< `bendRange`, semitones, the whole clip's bend
	std::optional<int32_t> bend_range_mpe; ///< `bendRangeMPE`, semitones, a finger's bend
	std::optional<ArpRecord> arp;          ///< the clip's arp; absent until the clip has one recorded
	/// Keys a document held here that this firmware does not know, written back unchanged.
	serialization::schema::Extras extras;

	bool operator==(const ClipValues&) const = default;
};

} // namespace deluge::project
