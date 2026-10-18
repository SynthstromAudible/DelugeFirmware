/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

/// @file
/// @brief An arpeggiator's settings: the table a menu reads, the words a file writes, and the presets.
///
/// Every row's path is `arpeggiator/<the file's attribute>`. The setting rows apply to every arp. The host rows (rate,
/// gate, rhythm, …) are params on a sound and on the whole-kit arp; they are settings only on a MIDI or CV clip and
/// on a MIDI or gate kit row, where nothing automates them. A file writes them as signed 32-bit numbers.

#include "engine-api/types/setting.h"
#include "engine-api/types/sync.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace deluge::engine {

/// @name Choice words
/// Each list is in the engine enum's order, so a word's index is the enum's value.
/// @{
inline constexpr std::array<std::string_view, 2> kArpModeWords{"off", "arp"};
inline constexpr std::array<std::string_view, 5> kArpOctaveModeWords{"up", "down", "upDown", "alt", "random"};
inline constexpr std::array<std::string_view, 9> kArpNoteModeWords{"up",    "down",  "upDown",   "random", "walk1",
                                                                   "walk2", "walk3", "asPlayed", "pattern"};
inline constexpr std::array<std::string_view, 3> kArpMpeVelocityWords{"off", "z", "y"};
/// A synced rate's type, as its stored number (even, triplet, dotted).
inline constexpr std::array<std::string_view, 3> kArpSyncTypeWords{"0", "10", "19"};
/// The preset menu's choices. A file holds no preset: it is derived from the mode, octave mode and note mode.
inline constexpr std::array<std::string_view, 7> kArpPresetWords{"off",    "up",   "down",  "both",
                                                                 "random", "walk", "custom"};
/// @}

/// @name Enum values the presets name
/// @{
inline constexpr uint8_t kArpModeOff = 0;
inline constexpr uint8_t kArpModeArp = 1;
inline constexpr uint8_t kArpPresetOff = 0;
inline constexpr uint8_t kArpPresetUp = 1;
inline constexpr uint8_t kArpPresetDown = 2;
inline constexpr uint8_t kArpPresetBoth = 3;
inline constexpr uint8_t kArpPresetRandom = 4;
inline constexpr uint8_t kArpPresetWalk = 5;
inline constexpr uint8_t kArpPresetCustom = 6;
inline constexpr uint8_t kArpOctaveUp = 0;
inline constexpr uint8_t kArpOctaveDown = 1;
inline constexpr uint8_t kArpOctaveAlternate = 3;
inline constexpr uint8_t kArpOctaveRandom = 4;
inline constexpr uint8_t kArpNoteUp = 0;
inline constexpr uint8_t kArpNoteDown = 1;
inline constexpr uint8_t kArpNoteRandom = 3;
inline constexpr uint8_t kArpNoteWalk2 = 5;
/// @}

/// @brief The chord types a kit arp offers.
inline constexpr int32_t kArpChordTypes = 9;

namespace arp_detail {

constexpr SettingInfo choice(std::string_view path, std::span<const std::string_view> words,
                             int32_t default_value = 0) {
	return SettingInfo{.path = path, .kind = SettingKind::CHOICE, .choices = words, .default_value = default_value};
}

constexpr SettingInfo integer(std::string_view path, int32_t min, int32_t max, int32_t default_value,
                              SettingUnits units = SettingUnits::NONE) {
	return SettingInfo{.path = path,
	                   .kind = SettingKind::INTEGER,
	                   .min = min,
	                   .max = max,
	                   .default_value = default_value,
	                   .units = units};
}

/// The sync menu: one value over the level and the type row.
inline constexpr SettingDescriptorExtras kSyncExtras{
    .mapping = {.kind = DisplayMappingKind::COMBINED_SYNC, .partner = "arpeggiator/syncType"}};

/// The owners a kit lane stands for.
inline constexpr std::array<int32_t, 1> kKitLaneOwner{static_cast<int32_t>(SettingContext::Owner::KIT_LANE)};
/// As-played and pattern order the notes a whole instrument holds, so a kit lane's note mode menu stops before them.
inline constexpr std::array kOffKitLane{
    SettingCondition{
        .subject = SettingCondition::Subject::OWNER, .test = SettingCondition::Test::IS_NOT, .values = kKitLaneOwner},
};
inline constexpr std::size_t kAsPlayedMode = 7;
inline constexpr std::size_t kPatternMode = 8;
static_assert(kArpNoteModeWords[kAsPlayedMode] == "asPlayed" && kArpNoteModeWords[kPatternMode] == "pattern");
inline constexpr std::array kNoteModeWhen{
    ChoiceCondition{.choice = kAsPlayedMode, .when = kOffKitLane},
    ChoiceCondition{.choice = kPatternMode, .when = kOffKitLane},
};
inline constexpr SettingDescriptorExtras kNoteModeExtras{.choice_when = kNoteModeWhen};

/// A host row's menu: the gate and rate as a signed Q31, the rest as an unsigned fraction, each 0..50.
inline constexpr SettingDescriptorExtras kStandardExtras{.mapping = {.kind = DisplayMappingKind::STANDARD}};
inline constexpr SettingDescriptorExtras kUnsignedExtras{.mapping = {.kind = DisplayMappingKind::UNSIGNED}};

constexpr SettingInfo host(std::string_view path, int32_t default_value = 0,
                           const SettingDescriptorExtras* extras = &kUnsignedExtras) {
	SettingInfo row =
	    integer(path, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(), default_value);
	row.extras = extras;
	return row;
}

inline constexpr std::array kSettingRows{
    choice("arpeggiator/arpMode", kArpModeWords),
    choice("arpeggiator/octaveMode", kArpOctaveModeWords),
    SettingInfo{.path = "arpeggiator/noteMode",
                .kind = SettingKind::CHOICE,
                .choices = kArpNoteModeWords,
                .extras = &kNoteModeExtras},
    choice("arpeggiator/mpeVelocity", kArpMpeVelocityWords),
    integer("arpeggiator/numOctaves", 1, 8, 2),
    integer("arpeggiator/stepRepeat", 1, 8, 1),
    integer("arpeggiator/chordType", 0, kArpChordTypes - 1, 0),
    // The level itself (1–9, 0 = off), whatever the song's tick magnitude; a sound's element holds it relative.
    SettingInfo{.path = "arpeggiator/syncLevel",
                .min = 0,
                .max = kMaxSyncLevel,
                .units = SettingUnits::SYNC_LEVEL,
                .extras = &kSyncExtras},
    choice("arpeggiator/syncType", kArpSyncTypeWords),
    SettingInfo{.path = "arpeggiator/randomizerLock", .kind = SettingKind::BOOL},
    SettingInfo{.path = "arpeggiator/kitArp", .kind = SettingKind::BOOL, .default_value = 1},
    // A command: writing one writes the mode, octave mode and note mode it stands for; reading derives it from them.
    choice("arpeggiator/preset", kArpPresetWords),
};

inline constexpr std::array kHostRows{
    host("arpeggiator/gate", 0, &kStandardExtras),
    host("arpeggiator/rate", 0, &kStandardExtras),
    host("arpeggiator/rhythm"),
    host("arpeggiator/sequenceLength"),
    host("arpeggiator/chordPolyphony"),
    host("arpeggiator/ratchetAmount"),
    host("arpeggiator/noteProbability", -1), // 0xFFFFFFFF: every note plays
    host("arpeggiator/bassProbability"),
    host("arpeggiator/swapProbability"),
    host("arpeggiator/glideProbability"),
    host("arpeggiator/reverseProbability", 0, nullptr), // no menu shows it as a setting
    host("arpeggiator/chordProbability"),
    host("arpeggiator/ratchetProbability"),
    host("arpeggiator/spreadVelocity"),
    host("arpeggiator/spreadGate"),
    host("arpeggiator/spreadOctave"),
};

} // namespace arp_detail

/// @brief The settings every arp has.
inline constexpr std::span<const SettingInfo> kArpSettingRows{arp_detail::kSettingRows};
static_assert(conditions_name_rows(kArpSettingRows));

/// @brief The values that are settings only where nothing automates them: a MIDI or CV clip's arp and a MIDI or gate
/// kit row's. Elsewhere they are params.
inline constexpr std::span<const SettingInfo> kArpHostRows{arp_detail::kHostRows};

/// @brief What a preset sets: the mode, and for every preset but off, the octave and note modes.
struct ArpPresetFields {
	uint8_t mode{kArpModeOff};
	std::optional<uint8_t> octave_mode;
	std::optional<uint8_t> note_mode;
	bool operator==(const ArpPresetFields&) const = default;
};

/// @brief The fields @p preset sets. Custom starts an arp going up, which its octave and note menus then change.
/// @param preset A preset's value (an index of kArpPresetWords).
/// @return The fields, or nullopt for a value no preset has.
[[nodiscard]] constexpr std::optional<ArpPresetFields> arp_preset_fields(uint8_t preset) {
	switch (preset) {
	case kArpPresetOff:
		return ArpPresetFields{.mode = kArpModeOff};
	case kArpPresetUp:
	case kArpPresetCustom:
		return ArpPresetFields{.mode = kArpModeArp, .octave_mode = kArpOctaveUp, .note_mode = kArpNoteUp};
	case kArpPresetDown:
		return ArpPresetFields{.mode = kArpModeArp, .octave_mode = kArpOctaveDown, .note_mode = kArpNoteDown};
	case kArpPresetBoth:
		return ArpPresetFields{.mode = kArpModeArp, .octave_mode = kArpOctaveAlternate, .note_mode = kArpNoteUp};
	case kArpPresetRandom:
		return ArpPresetFields{.mode = kArpModeArp, .octave_mode = kArpOctaveRandom, .note_mode = kArpNoteRandom};
	case kArpPresetWalk:
		return ArpPresetFields{.mode = kArpModeArp, .octave_mode = kArpOctaveAlternate, .note_mode = kArpNoteWalk2};
	default:
		return std::nullopt;
	}
}

/// @brief The preset an arp's fields make: off when the arp is, the preset whose octave and note modes they are, or
/// custom.
/// @param mode        The arp mode.
/// @param octave_mode The octave mode.
/// @param note_mode   The note mode.
/// @return The preset's value.
[[nodiscard]] constexpr uint8_t arp_preset_of(uint8_t mode, uint8_t octave_mode, uint8_t note_mode) {
	if (mode == kArpModeOff) {
		return kArpPresetOff;
	}
	for (uint8_t preset = kArpPresetUp; preset < kArpPresetCustom; ++preset) {
		const std::optional<ArpPresetFields> fields = arp_preset_fields(preset);
		if (fields->octave_mode == octave_mode && fields->note_mode == note_mode) {
			return preset;
		}
	}
	return kArpPresetCustom;
}

} // namespace deluge::engine
