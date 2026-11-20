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
/// @brief The DEVICE setting group: the device's own settings, which it keeps whatever song is loaded. Its rows are
///        the table `table_of(SettingGroupId::DEVICE)` gives; the host stores their values where the device keeps them
///        (the flash settings, the pad controller), and a menu reads and writes them through the scenes' device port.
///
/// A CHOICE row's value is its choice's index, so the scenes need no legacy type for it. The enums here exist only for
/// the rows whose value a scene branches on; each enumerator's value is its choice's index in the row.

#include "deluge-project/features.h"
#include "plugin-api/types/setting.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace deluge::project {

/// @brief The DEVICE group's rows.
/// @return Every row, in a table that lives for the program's life.
[[nodiscard]] std::span<const plugin::SettingInfo> device_table();

/// @brief Which of the two shortcut layouts the grid's parameter shortcuts resolve: the `pads/shortcutsVersion` row.
enum class ShortcutsVersion : uint8_t {
	V1 = 0, ///< The pre-3.0 layout.
	V3 = 1, ///< The current one, and the default.
};

/// @brief How a playhead is shown on the pads: the `pads/playCursor` row.
enum class FlashCursor : uint8_t {
	FAST = 0, ///< the pad controller flashes the pad; no pixel of the rendered image changes
	OFF = 1,  ///< no playhead
	SLOW = 2, ///< the factory default: the playhead's pixel is substituted in the rendered image
};

struct device_path;

/// @brief The path of a DEVICE row the table holds. Only `device_path` makes one, from its own constants, so a path a
///        caller types cannot be one: a read that takes a DevicePath reads a row that exists.
class DevicePath {
public:
	/// @brief The row's path.
	[[nodiscard]] constexpr std::string_view text() const { return text_; }
	/// @brief The row's path, for the checked front (`DeviceSettings::read`, `write`) that takes any path.
	constexpr operator std::string_view() const { return text_; } // NOLINT(google-explicit-constructor)
	/// @brief Whether two handles name the same row.
	constexpr bool operator==(const DevicePath&) const = default;

private:
	friend struct device_path;
	constexpr explicit DevicePath(std::string_view text) : text_{text} {}
	std::string_view text_;
};

/// @brief The paths of the DEVICE rows the scenes name in code. `device_settings.cpp` checks, when the image is
///        built, that the table holds a row at each.
struct device_path {
	/// Whether a note's name uses a sharp or a flat: BOOL, 1 for sharps.
	static constexpr DevicePath kUseSharps{"defaults/useSharps"};
	/// The polarity a new patch cable takes: CHOICE over the patch cable's two polarities, unipolar then bipolar.
	static constexpr DevicePath kPatchCablePolarity{"defaults/patchCablePolarity"};
	/// The least tempo a new song draws its tempo from: INTEGER, beats a minute.
	static constexpr DevicePath kTempoLower{"defaults/tempo/lower"};
	/// The greatest tempo a new song draws its tempo from: INTEGER, beats a minute.
	static constexpr DevicePath kTempoUpper{"defaults/tempo/upper"};
	/// The least swing a new song draws its swing from: INTEGER, percent.
	static constexpr DevicePath kSwingLower{"defaults/swing/lower"};
	/// The greatest swing a new song draws its swing from: INTEGER, percent.
	static constexpr DevicePath kSwingUpper{"defaults/swing/upper"};
	/// The first key a new song draws its key from: INTEGER, semitones above C.
	static constexpr DevicePath kKeyLower{"defaults/key/lower"};
	/// The last key a new song draws its key from: INTEGER, semitones above C.
	static constexpr DevicePath kKeyUpper{"defaults/key/upper"};
	/// A new song's tick resolution: INTEGER, the power of two that multiplies 96 ticks a quarter note.
	static constexpr DevicePath kMagnitude{"defaults/magnitude"};
	/// A new song's swing interval: INTEGER, a sync level, named against the magnitude row.
	static constexpr DevicePath kSwingInterval{"defaults/swingInterval"};
	/// A new song's scale: CHOICE over the preset scales, then random, then none.
	static constexpr DevicePath kScale{"defaults/scale"};
	/// The velocity a new instrument plays at: INTEGER, 1 to 127.
	static constexpr DevicePath kVelocity{"defaults/velocity"};
	/// A new MIDI or CV instrument's pitch-bend range: INTEGER, semitones.
	static constexpr DevicePath kBendRange{"defaults/bendRange"};
	/// The metronome's volume: INTEGER, in the menu's steps.
	static constexpr DevicePath kMetronomeVolume{"defaults/metronomeVolume"};
	/// The song the device opens as it starts: CHOICE, blank, template, last opened, last saved.
	static constexpr DevicePath kStartupSong{"defaults/startupSong"};
	/// How a new slice plays its sample: CHOICE, cut, once, loop, stretch.
	static constexpr DevicePath kSliceMode{"defaults/sliceMode"};
	/// Whether a pad lights while the CPU is near its limit: BOOL.
	static constexpr DevicePath kCpuIndicator{"defaults/cpuIndicator"};
	/// How long a press is held to count as a hold: INTEGER, twentieths of a second.
	static constexpr DevicePath kHoldTime{"defaults/holdTime"};
	/// What a recording is quantised to: INTEGER, a sync level named against a magnitude of one.
	static constexpr DevicePath kRecordQuantize{"defaults/recordQuantize"};
	/// Whether an audio clip recorded from an outside input keeps margins past its loop's ends: BOOL.
	static constexpr DevicePath kRecordMargins{"defaults/recordMargins"};
	/// How loud a sound starts a threshold recording: CHOICE, off, low, medium, high.
	static constexpr DevicePath kThresholdRecording{"defaults/thresholdRecording"};
	/// The kind of clip a new clip is: CHOICE, synth, kit, MIDI, CV, audio.
	static constexpr DevicePath kNewClipType{"defaults/newClipType"};
	/// Whether a new clip takes the kind the last one had instead: BOOL.
	static constexpr DevicePath kUseLastClipType{"defaults/useLastClipType"};
	/// Whether the automation editor interpolates between its steps: BOOL.
	static constexpr DevicePath kAutomationInterpolate{"defaults/automation/interpolate"};
	/// Whether a clear outside the automation editor leaves a clip's automation, clearing its notes alone: BOOL.
	static constexpr DevicePath kAutomationClear{"defaults/automation/clear"};
	/// Whether a shift outside the automation editor leaves a clip's automation where it is: BOOL.
	static constexpr DevicePath kAutomationShift{"defaults/automation/shift"};
	/// Whether a note's nudge leaves the automation where it is: BOOL.
	static constexpr DevicePath kAutomationNudgeNote{"defaults/automation/nudgeNote"};
	/// Whether the automation editor's shortcuts are off while a pad auditions: BOOL.
	static constexpr DevicePath kAutomationNoAuditionShortcuts{"defaults/automation/noAuditionShortcuts"};
	/// The grid's shortcut layout: CHOICE over ShortcutsVersion.
	static constexpr DevicePath kShortcutsVersion{"pads/shortcutsVersion"};
	/// How a playhead is shown on the pads: CHOICE over FlashCursor.
	static constexpr DevicePath kPlayCursor{"pads/playCursor"};
	/// The first part of each preset scale's "disabled by default" path; the scale's index follows it.
	static constexpr std::string_view kDisabledScalePrefix = "defaults/scales/disabled/";
	/// The first part of each community feature's path; the name the feature file writes it under follows it.
	static constexpr std::string_view kFeaturePrefix = "features/";

	/// @brief The BOOL row that says whether preset scale @p scale is disabled by default.
	/// @param scale A preset scale's index.
	/// @return The row's path, `defaults/scales/disabled/<scale>`, valid for the program's life; nullopt for an index
	///         past the preset scales.
	[[nodiscard]] static std::optional<DevicePath> disabled_scale(size_t scale);

	/// @brief The DEVICE row of community feature @p f: BOOL for an on/off feature, else CHOICE over its words.
	/// @param f A feature.
	/// @return The row's path, `features/` then the name the feature file writes it under.
	[[nodiscard]] static constexpr DevicePath feature(Feature f);
};

namespace feature_paths {
/// The menu-time tables: read when a menu looks a setting up, never by the audio tier or an interrupt. The device
/// image places this namespace's data in SDRAM (linker/sdram_sections.x matches its name).
inline namespace menu_tables {

/// @brief How long the longest feature's path is.
inline constexpr size_t kLongest = [] {
	size_t longest = 0;
	for (const FeatureInfo& info : kFeatures) {
		longest = std::max(longest, device_path::kFeaturePrefix.size() + info.path.size());
	}
	return longest;
}();

/// @brief One feature's path, as text the table's views point into.
struct FeaturePath {
	std::array<char, kLongest> text{}; ///< the prefix, then the feature's file name
	size_t size{0};                    ///< how many characters of `text` it is
};

/// @brief Every feature's path, `features/<name>`, indexed by its id.
inline constexpr std::array<FeaturePath, kFeatureCount> kPaths = [] {
	std::array<FeaturePath, kFeatureCount> paths{};
	for (const FeatureInfo& info : kFeatures) {
		FeaturePath& path = paths[std::to_underlying(info.id)];
		for (const std::string_view part : {device_path::kFeaturePrefix, info.path}) {
			for (const char c : part) {
				path.text[path.size++] = c;
			}
		}
	}
	return paths;
}();

} // namespace menu_tables
} // namespace feature_paths

constexpr DevicePath device_path::feature(Feature f) {
	// Indexed through `feature_info`, so an id with no entry stops there rather than reading past the paths.
	const feature_paths::FeaturePath& path = feature_paths::kPaths[std::to_underlying(feature_info(f).id)];
	return DevicePath{std::string_view{path.text.data(), path.size}};
}

/// @brief The community feature whose DEVICE row is at @p path.
/// @param path A DEVICE row's path.
/// @return The feature's entry, or null for a path no feature's row has.
[[nodiscard]] constexpr const FeatureInfo* feature_at(std::string_view path) {
	if (!path.starts_with(device_path::kFeaturePrefix)) {
		return nullptr;
	}
	return find_feature(path.substr(device_path::kFeaturePrefix.size()));
}

} // namespace deluge::project
