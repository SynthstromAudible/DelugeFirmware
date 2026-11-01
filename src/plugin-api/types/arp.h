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
/// Every row's path is `arpeggiator/<the file's attribute>`. The setting rows are every arp's, each shown where its
/// menu says (a gate row's arp has no octaves, and the whole kit's no MPE). The host rows (rate,
/// gate, rhythm, …) are params on a sound and on the whole-kit arp; they are settings only on a MIDI or CV clip and
/// on a MIDI or gate kit row, where nothing automates them. A file writes them as signed 32-bit numbers.

#include "plugin-api/types/setting.h"
#include "plugin-api/types/sync.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace deluge::plugin {

/// @name Choice words
/// Each list is in the plugin enum's order, so a word's index is the enum's value.
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
/// The menu-time tables: read when a menu or an Edit looks a setting up, never by the audio tier or an interrupt.
/// The device image places this namespace's data in SDRAM (linker/sdram_sections.x matches its name).
inline namespace menu_tables {

constexpr SettingInfo choice(std::string_view path, std::span<const std::string_view> words, int32_t default_value = 0,
                             const SettingDescriptorExtras* extras = nullptr) {
	return SettingInfo{
	    .path = path, .kind = SettingKind::CHOICE, .choices = words, .default_value = default_value, .extras = extras};
}

constexpr SettingInfo integer(std::string_view path, int32_t min, int32_t max, int32_t default_value,
                              SettingUnits units = SettingUnits::NONE,
                              const SettingDescriptorExtras* extras = nullptr) {
	return SettingInfo{.path = path,
	                   .kind = SettingKind::INTEGER,
	                   .min = min,
	                   .max = max,
	                   .default_value = default_value,
	                   .units = units,
	                   .extras = extras};
}

/// The sync menu: one value over the level and the type row.
inline constexpr SettingDescriptorExtras kSyncExtras{
    .mapping = {.kind = DisplayMappingKind::COMBINED_SYNC, .partner = "arpeggiator/syncType"}};

using Instrument = SettingContext::Instrument;
using Owner = SettingContext::Owner;

inline constexpr std::array<int32_t, 1> kKitLaneOwner{static_cast<int32_t>(Owner::KIT_LANE)};
/// A gate row plays a gate, with no pitch, octave or velocity to arpeggiate.
inline constexpr std::array<int32_t, 1> kGateDrum{static_cast<int32_t>(Instrument::GATE_DRUM)};
/// What plays an arp with no sound: a MIDI or CV track, or a MIDI or gate kit row.
inline constexpr std::array<int32_t, 4> kNonSound{
    static_cast<int32_t>(Instrument::MIDI), static_cast<int32_t>(Instrument::CV),
    static_cast<int32_t>(Instrument::MIDI_DRUM), static_cast<int32_t>(Instrument::GATE_DRUM)};
/// What plays an arp with no sound and a pitch: a MIDI or CV track, or a MIDI kit row.
inline constexpr std::array<int32_t, 3> kPitchedNonSound{static_cast<int32_t>(Instrument::MIDI),
                                                         static_cast<int32_t>(Instrument::CV),
                                                         static_cast<int32_t>(Instrument::MIDI_DRUM)};
/// A MIDI or CV track: what plays chords.
inline constexpr std::array<int32_t, 2> kMidiOrCv{static_cast<int32_t>(Instrument::MIDI),
                                                  static_cast<int32_t>(Instrument::CV)};
inline constexpr std::array<int32_t, 1> kArpOffValue{kArpModeOff};

/// As-played and pattern order the notes a whole instrument holds, so a kit lane's note mode menu stops before them.
inline constexpr std::array kOffKitLane{owner_is_not(kKitLaneOwner)};
constexpr std::size_t note_mode_index(std::string_view word) {
	return static_cast<std::size_t>(std::ranges::find(kArpNoteModeWords, word) - kArpNoteModeWords.begin());
}
inline constexpr std::size_t kAsPlayedMode = note_mode_index("asPlayed");
inline constexpr std::size_t kPatternMode = note_mode_index("pattern");
static_assert(kAsPlayedMode < kArpNoteModeWords.size() && kPatternMode < kArpNoteModeWords.size());
inline constexpr std::array kNoteModeWhen{
    ChoiceCondition{.choice = kAsPlayedMode, .when = kOffKitLane},
    ChoiceCondition{.choice = kPatternMode, .when = kOffKitLane},
};

/// A row a gate row's arp has no use for.
inline constexpr std::array kNotOnGate{instrument_is_not(kGateDrum)};
/// What plays a gate row's arp, or the whole kit's (its MPE, octave and chord rows are not the kit's).
inline constexpr std::array<int32_t, 2> kGateDrumOrWholeKit{static_cast<int32_t>(Instrument::GATE_DRUM),
                                                            static_cast<int32_t>(Instrument::KIT)};
/// A row neither a gate row's arp nor the whole kit's offers.
inline constexpr std::array kNotOnGateOrWholeKit{instrument_is_not(kGateDrumOrWholeKit)};
/// A kit row's own arp row: off a gate row.
inline constexpr std::array kOnPitchedKitLane{owner_is(kKitLaneOwner), instrument_is_not(kGateDrum)};
inline constexpr std::array kOnKitLane{owner_is(kKitLaneOwner)};

inline constexpr SettingDescriptorExtras kNoteModeExtras{.applies_when = kNotOnGate, .choice_when = kNoteModeWhen};
inline constexpr SettingDescriptorExtras kNotOnGateExtras{.applies_when = kNotOnGate};
inline constexpr SettingDescriptorExtras kNotOnGateOrWholeKitExtras{.applies_when = kNotOnGateOrWholeKit};
inline constexpr SettingDescriptorExtras kChordTypeExtras{.applies_when = kOnPitchedKitLane};
inline constexpr SettingDescriptorExtras kKitArpExtras{.applies_when = kOnKitLane};

/// A host row's menu: the gate and rate as a signed Q31, the rest as an unsigned fraction, each 0..50. A host row
/// is a setting only where nothing automates it, so its menu shows only where no sound plays the arp; a randomizer's
/// spread, ratchet, chord and probabilities only while the arp is on.
inline constexpr std::array kOffSound{instrument_is(kNonSound)};
inline constexpr std::array kOffSoundArpOn{instrument_is(kNonSound),
                                           setting_is_not("arpeggiator/arpMode", kArpOffValue)};
inline constexpr std::array kPitchedOffSound{instrument_is(kPitchedNonSound)};
inline constexpr std::array kPitchedOffSoundArpOn{instrument_is(kPitchedNonSound),
                                                  setting_is_not("arpeggiator/arpMode", kArpOffValue)};
inline constexpr std::array kChordsArpOn{instrument_is(kMidiOrCv), setting_is_not("arpeggiator/arpMode", kArpOffValue)};
inline constexpr SettingDescriptorExtras kStandardExtras{.mapping = {.kind = DisplayMappingKind::STANDARD},
                                                         .applies_when = kOffSound};
inline constexpr SettingDescriptorExtras kUnsignedExtras{.mapping = {.kind = DisplayMappingKind::UNSIGNED},
                                                         .applies_when = kOffSound};
inline constexpr SettingDescriptorExtras kUnsignedArpOnExtras{.mapping = {.kind = DisplayMappingKind::UNSIGNED},
                                                              .applies_when = kOffSoundArpOn};
inline constexpr SettingDescriptorExtras kUnsignedPitchedExtras{.mapping = {.kind = DisplayMappingKind::UNSIGNED},
                                                                .applies_when = kPitchedOffSound};
inline constexpr SettingDescriptorExtras kUnsignedPitchedArpOnExtras{.mapping = {.kind = DisplayMappingKind::UNSIGNED},
                                                                     .applies_when = kPitchedOffSoundArpOn};
inline constexpr SettingDescriptorExtras kUnsignedChordsExtras{.mapping = {.kind = DisplayMappingKind::UNSIGNED},
                                                               .applies_when = kChordsArpOn};

constexpr SettingInfo host(std::string_view path, int32_t default_value = 0,
                           const SettingDescriptorExtras* extras = &kUnsignedExtras) {
	SettingInfo row =
	    integer(path, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(), default_value);
	row.extras = extras;
	return row;
}

/// Every row: the setting rows, then from kSettingRowCount the host rows. The one copy kArpSettingRows,
/// kArpHostRows and kArpRows view.
inline constexpr std::array kRows{
    choice("arpeggiator/arpMode", kArpModeWords),
    choice("arpeggiator/octaveMode", kArpOctaveModeWords, 0, &kNotOnGateOrWholeKitExtras),
    choice("arpeggiator/noteMode", kArpNoteModeWords, 0, &kNoteModeExtras),
    choice("arpeggiator/mpeVelocity", kArpMpeVelocityWords, 0, &kNotOnGateOrWholeKitExtras),
    integer("arpeggiator/numOctaves", 1, 8, 2, SettingUnits::NONE, &kNotOnGateOrWholeKitExtras),
    integer("arpeggiator/stepRepeat", 1, 8, 1, SettingUnits::NONE, &kNotOnGateExtras),
    integer("arpeggiator/chordType", 0, kArpChordTypes - 1, 0, SettingUnits::NONE, &kChordTypeExtras),
    // The level itself (1–9, 0 = off), whatever the song's tick magnitude; a sound's element holds it relative.
    SettingInfo{.path = "arpeggiator/syncLevel",
                .min = 0,
                .max = kMaxSyncLevel,
                .units = SettingUnits::SYNC_LEVEL,
                .extras = &kSyncExtras},
    choice("arpeggiator/syncType", kArpSyncTypeWords),
    SettingInfo{.path = "arpeggiator/randomizerLock", .kind = SettingKind::BOOL},
    SettingInfo{.path = "arpeggiator/kitArp", .kind = SettingKind::BOOL, .default_value = 1, .extras = &kKitArpExtras},
    // A command: writing one writes the mode, octave mode and note mode it stands for; reading derives it from them.
    choice("arpeggiator/preset", kArpPresetWords),
    // The host rows.
    host("arpeggiator/gate", 0, &kStandardExtras),
    host("arpeggiator/rate", 0, &kStandardExtras),
    host("arpeggiator/rhythm"),
    host("arpeggiator/sequenceLength"),
    host("arpeggiator/chordPolyphony", 0, &kUnsignedChordsExtras),
    host("arpeggiator/ratchetAmount", 0, &kUnsignedArpOnExtras),
    host("arpeggiator/noteProbability", -1), // 0xFFFFFFFF: every note plays
    host("arpeggiator/bassProbability", 0, &kUnsignedPitchedArpOnExtras),
    host("arpeggiator/swapProbability", 0, &kUnsignedPitchedArpOnExtras),
    host("arpeggiator/glideProbability", 0, &kUnsignedPitchedArpOnExtras),
    host("arpeggiator/reverseProbability", 0, nullptr), // no menu shows it as a setting
    host("arpeggiator/chordProbability", 0, &kUnsignedChordsExtras),
    host("arpeggiator/ratchetProbability", 0, &kUnsignedArpOnExtras),
    host("arpeggiator/spreadVelocity", 0, &kUnsignedPitchedExtras),
    host("arpeggiator/spreadGate", 0, &kUnsignedArpOnExtras),
    host("arpeggiator/spreadOctave", 0, &kUnsignedPitchedArpOnExtras),
};

/// How many of kRows are setting rows: those before the first host row.
inline constexpr std::size_t kSettingRowCount = static_cast<std::size_t>(
    std::ranges::find(kRows, std::string_view{"arpeggiator/gate"}, &SettingInfo::path) - kRows.begin());
static_assert(kSettingRowCount < kRows.size() && kRows[kSettingRowCount - 1].path == "arpeggiator/preset");

} // namespace menu_tables
} // namespace arp_detail

/// @brief An arp's every row: kArpSettingRows, then kArpHostRows.
inline constexpr std::span<const SettingInfo> kArpRows{arp_detail::kRows};
static_assert(conditions_are_valid(kArpRows));
static_assert(mappings_are_valid(kArpRows));

/// @brief The settings every arp has.
inline constexpr std::span<const SettingInfo> kArpSettingRows = kArpRows.first(arp_detail::kSettingRowCount);
static_assert(conditions_are_valid(kArpSettingRows));
static_assert(mappings_are_valid(kArpSettingRows));

/// @brief The values that are settings only where nothing automates them: a MIDI or CV clip's arp and a MIDI or gate
/// kit row's. Elsewhere they are params. Their conditions read the arp's setting rows (kArpRows).
inline constexpr std::span<const SettingInfo> kArpHostRows = kArpRows.subspan(arp_detail::kSettingRowCount);

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

} // namespace deluge::plugin
