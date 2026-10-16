#pragma once

/// @file
/// @brief The record of one arpeggiator's settings: a clip's (in its value layer) or a kit row's (on its lane).
///
/// It holds what a menu sets. What the arp generates while it plays (the randomizer's locked values, the note pattern)
/// is the engine's, saved beside these fields. The host values are held only where they are settings (a MIDI or CV
/// clip, a MIDI or gate kit row); on a sound they are params, and the record leaves them absent.

#include "engine-api/types/arp.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::project {

/// @brief One arp's settings, each as its engine value.
struct ArpRecord {
	/// @name Settings every arp has
	/// @{
	uint8_t mode{engine::kArpModeOff}; ///< `arpMode`, an index of kArpModeWords
	uint8_t octave_mode{0};            ///< `octaveMode`, an index of kArpOctaveModeWords
	uint8_t note_mode{0};              ///< `noteMode`, an index of kArpNoteModeWords
	uint8_t mpe_velocity{0};           ///< `mpeVelocity`, an index of kArpMpeVelocityWords
	uint8_t num_octaves{2};            ///< `numOctaves`, 1..8
	uint8_t step_repeat{1};            ///< `stepRepeat`, 1..8
	uint8_t chord_type{0};             ///< `chordType`, 0..kArpChordTypes-1
	uint8_t sync_level{0};             ///< `syncLevel`, the level itself (1..9, 0 = off)
	uint8_t sync_type{0};              ///< `syncType`, the stored number (0, 10, 19)
	bool randomizer_lock{false};       ///< `randomizerLock`
	bool kit_arp{true};                ///< `kitArp`: a kit row plays in its kit's arp
	/// @}

	/// @name Host values, held where they are settings
	/// File form: a signed 32-bit number.
	/// @{
	std::optional<int32_t> gate;
	std::optional<int32_t> rate;
	std::optional<int32_t> rhythm;
	std::optional<int32_t> sequence_length;
	std::optional<int32_t> chord_polyphony;
	std::optional<int32_t> ratchet_amount;
	std::optional<int32_t> note_probability;
	std::optional<int32_t> bass_probability;
	std::optional<int32_t> swap_probability;
	std::optional<int32_t> glide_probability;
	std::optional<int32_t> reverse_probability;
	std::optional<int32_t> chord_probability;
	std::optional<int32_t> ratchet_probability;
	std::optional<int32_t> spread_velocity;
	std::optional<int32_t> spread_gate;
	std::optional<int32_t> spread_octave;
	/// @}

	bool operator==(const ArpRecord&) const = default;
};

/// @brief The slot of the host value at @p path.
/// @param record The record (const or not).
/// @param path   A path of kArpHostRows.
/// @return Its slot, or nullptr for a path that is not a host row.
template <typename Record>
[[nodiscard]] auto* host_value_at(Record& record, std::string_view path) {
	using Slot = decltype(&record.gate);
	constexpr std::string_view kPrefix = "arpeggiator/";
	if (!path.starts_with(kPrefix)) {
		return Slot{nullptr};
	}
	const std::string_view name = path.substr(kPrefix.size());
	if (name == "gate") {
		return &record.gate;
	}
	if (name == "rate") {
		return &record.rate;
	}
	if (name == "rhythm") {
		return &record.rhythm;
	}
	if (name == "sequenceLength") {
		return &record.sequence_length;
	}
	if (name == "chordPolyphony") {
		return &record.chord_polyphony;
	}
	if (name == "ratchetAmount") {
		return &record.ratchet_amount;
	}
	if (name == "noteProbability") {
		return &record.note_probability;
	}
	if (name == "bassProbability") {
		return &record.bass_probability;
	}
	if (name == "swapProbability") {
		return &record.swap_probability;
	}
	if (name == "glideProbability") {
		return &record.glide_probability;
	}
	if (name == "reverseProbability") {
		return &record.reverse_probability;
	}
	if (name == "chordProbability") {
		return &record.chord_probability;
	}
	if (name == "ratchetProbability") {
		return &record.ratchet_probability;
	}
	if (name == "spreadVelocity") {
		return &record.spread_velocity;
	}
	if (name == "spreadGate") {
		return &record.spread_gate;
	}
	if (name == "spreadOctave") {
		return &record.spread_octave;
	}
	return Slot{nullptr};
}

/// @brief The slot of the setting at @p path, other than the preset (which the record derives).
/// @param record The record (const or not).
/// @param path   A path of kArpSettingRows.
/// @return Its slot, or nullptr for the preset or a path that is not a setting row. Bools are not here: see
///         arp_flag_at.
template <typename Record>
[[nodiscard]] auto* arp_field_at(Record& record, std::string_view path) {
	using Slot = decltype(&record.mode);
	if (path == "arpeggiator/arpMode") {
		return &record.mode;
	}
	if (path == "arpeggiator/octaveMode") {
		return &record.octave_mode;
	}
	if (path == "arpeggiator/noteMode") {
		return &record.note_mode;
	}
	if (path == "arpeggiator/mpeVelocity") {
		return &record.mpe_velocity;
	}
	if (path == "arpeggiator/numOctaves") {
		return &record.num_octaves;
	}
	if (path == "arpeggiator/stepRepeat") {
		return &record.step_repeat;
	}
	if (path == "arpeggiator/chordType") {
		return &record.chord_type;
	}
	if (path == "arpeggiator/syncLevel") {
		return &record.sync_level;
	}
	if (path == "arpeggiator/syncType") {
		return &record.sync_type;
	}
	return Slot{nullptr};
}

/// @brief The slot of the bool setting at @p path.
/// @param record The record (const or not).
/// @param path   `arpeggiator/randomizerLock` or `arpeggiator/kitArp`.
/// @return Its slot, or nullptr for any other path.
template <typename Record>
[[nodiscard]] auto* arp_flag_at(Record& record, std::string_view path) {
	using Slot = decltype(&record.kit_arp);
	if (path == "arpeggiator/randomizerLock") {
		return &record.randomizer_lock;
	}
	if (path == "arpeggiator/kitArp") {
		return &record.kit_arp;
	}
	return Slot{nullptr};
}

/// @brief The preset @p record's fields make.
/// @param record The arp's record.
/// @return The preset's value, an index of kArpPresetWords.
[[nodiscard]] constexpr uint8_t preset_of(const ArpRecord& record) {
	return engine::arp_preset_of(record.mode, record.octave_mode, record.note_mode);
}

} // namespace deluge::project
