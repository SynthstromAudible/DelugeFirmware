#pragma once

/// @file
/// @brief The record of one arpeggiator's settings: a clip's (in its value layer) or a kit row's (on its lane).
///
/// It holds what a menu sets. What the arp generates while it plays (the randomizer's locked values, the note pattern)
/// is the engine's, saved beside these fields. The host values are held only where they are settings (a MIDI or CV
/// clip, a MIDI or gate kit row); on a sound they are params, and the record leaves them absent.

#include "engine-api/types/arp.h"

#include <array>
#include <cstddef>
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
	/// File form: a signed 32-bit number, one per row of kArpHostRows in that order; read and written through
	/// host_value and set_host_value.
	/// @{
	uint16_t host_held{0};                                          ///< bit i: the record holds host value i
	std::array<int32_t, engine::kArpHostRows.size()> host_values{}; ///< 0 where not held
	/// @}

	bool operator==(const ArpRecord&) const = default;
};

static_assert(engine::kArpHostRows.size() <= 16, "ArpRecord::host_held has a bit per host value");

/// @brief The index of host value @p path among kArpHostRows.
/// @param path A path of kArpHostRows.
/// @return Its index, or nullopt for a path that is not a host row.
[[nodiscard]] constexpr std::optional<size_t> host_index(std::string_view path) {
	for (size_t i = 0; i < engine::kArpHostRows.size(); ++i) {
		if (engine::kArpHostRows[i].path == path) {
			return i;
		}
	}
	return std::nullopt;
}

/// @brief The host value @p record holds at @p path.
/// @param record The record.
/// @param path   A path of kArpHostRows.
/// @return The value, or nullopt where the record holds none (or @p path is not a host row).
[[nodiscard]] constexpr std::optional<int32_t> host_value(const ArpRecord& record, std::string_view path) {
	const std::optional<size_t> i = host_index(path);
	return i && (record.host_held & (1u << *i)) != 0 ? std::optional<int32_t>{record.host_values[*i]} : std::nullopt;
}

/// @brief Hold @p value as @p record's host value at @p path, or forget it.
/// @param record The record.
/// @param path   A path of kArpHostRows; any other path changes nothing.
/// @param value  The value, or nullopt to hold none.
constexpr void set_host_value(ArpRecord& record, std::string_view path, std::optional<int32_t> value) {
	const std::optional<size_t> i = host_index(path);
	if (!i) {
		return;
	}
	const auto bit = static_cast<uint16_t>(1u << *i);
	record.host_held = value ? record.host_held | bit : record.host_held & ~bit;
	record.host_values[*i] = value.value_or(0);
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
