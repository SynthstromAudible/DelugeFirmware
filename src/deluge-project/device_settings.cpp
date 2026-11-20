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

#include "deluge-project/device_settings.h"

#include "music-theory/preset_scales.h"
#include "plugin-api/types/display_mapping.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

namespace deluge::project {

namespace {
/// The menu-time tables: read when a menu looks a setting up, never by the audio tier or an interrupt. The device
/// image places this namespace's data in SDRAM (linker/sdram_sections.x matches its name).
inline namespace menu_tables {

using plugin::SettingInfo;
using plugin::SettingKind;
using plugin::SettingUnits;

/// How many preset scales a "disabled by default" row is kept for.
constexpr size_t kScaleCount = music::NUM_PRESET_SCALES;
static_assert(kScaleCount <= 100, "a scale's index is written as at most two digits");

/// One preset scale's "disabled by default" path, as text the table's views point into.
struct ScalePath {
	std::array<char, device_path::kDisabledScalePrefix.size() + 2> text{}; ///< the prefix, then the index's digits
	size_t size{0};                                                        ///< how many characters of `text` it is
};

/// Every preset scale's path, built when the image is: `defaults/scales/disabled/<i>` for each scale's index.
constexpr std::array<ScalePath, kScaleCount> kScalePaths = [] {
	std::array<ScalePath, kScaleCount> paths{};
	for (size_t scale = 0; scale < kScaleCount; ++scale) {
		ScalePath& path = paths[scale];
		for (const char c : device_path::kDisabledScalePrefix) {
			path.text[path.size++] = c;
		}
		if (scale >= 10) {
			path.text[path.size++] = static_cast<char>('0' + (scale / 10));
		}
		path.text[path.size++] = static_cast<char>('0' + (scale % 10));
	}
	return paths;
}();

/// The patch cable's polarities, as the cable's own file words name them (`instrument::to_string`).
constexpr std::array<std::string_view, 2> kPolarityWords{"unipolar", "bipolar"};
/// The shortcut layouts, in ShortcutsVersion's order.
constexpr std::array<std::string_view, 2> kShortcutsVersionWords{"v1", "v3"};
/// The playhead's forms, in FlashCursor's order.
constexpr std::array<std::string_view, 3> kPlayCursorWords{"fast", "off", "slow"};
/// The favourites a browser may show, in FavouritesLayout's order.
constexpr std::array<std::string_view, 3> kFavouritesLayoutWords{"favourites", "favouritesAndBanks", "off"};
static_assert(std::to_underlying(ShortcutsVersion::V3) == 1 && std::to_underlying(FlashCursor::SLOW) == 2
                  && std::to_underlying(FavouritesLayout::OFF) == 2,
              "an enumerator's value is its choice's index");
/// The text-entry keyboard's letter layouts.
constexpr std::array<std::string_view, 3> kTextKeyboardWords{"qwerty", "azerty", "qwertz"};
/// The nine colours a pad setting may take, in the order the pad colours number them.
constexpr std::array<std::string_view, 9> kPadColourWords{"red",     "green", "blue",  "yellow", "cyan",
                                                          "magenta", "amber", "white", "pink"};
/// How a menu marks its selected row.
constexpr std::array<std::string_view, 3> kHighlightingWords{"full", "partial", "none"};
/// How a new song's session view lays its clips out.
constexpr std::array<std::string_view, 2> kSessionLayoutWords{"rows", "grid"};
/// The modes the grid view may start in.
constexpr std::array<std::string_view, 3> kGridModeWords{"selection", "green", "blue"};
/// The layouts a new keyboard view's pads may take: the three a menu offers of the device's seven. A layout past them
/// reads clamped to the last.
constexpr std::array<std::string_view, 3> kKeyboardLayoutWords{"isomorphic", "inKey", "piano"};
/// What the screensaver may show.
constexpr std::array<std::string_view, 4> kScreensaverWords{"off", "blank", "starscape", "deluge"};
/// When the sample browser may preview a sample.
constexpr std::array<std::string_view, 3> kSamplePreviewWords{"off", "conditional", "on"};

/// The pad colours' indices among kPadColourWords that the colour rows reset to.
constexpr int32_t kRed = 0;
constexpr int32_t kGreen = 1;
constexpr int32_t kBlue = 2;
constexpr int32_t kYellow = 3;
constexpr int32_t kMagenta = 5;
constexpr int32_t kAmber = 6;
/// The scales a new song may start in: the preset scales in their own order, then a random one, then none. A user
/// scale is no default, so it has no choice.
constexpr std::array<std::string_view, kScaleCount + 2> kScaleWords{
    "major",     "minor",        "dorian",          "phrygian",       "lydian", "mixolydian",
    "locrian",   "melodicMinor", "harmonicMinor",   "hungarianMinor", "marva",  "arabian",
    "wholeTone", "blues",        "pentatonicMinor", "hirajoshi",      "random", "none"};
/// The song the device opens as it starts, in the order the flash setting numbers them.
constexpr std::array<std::string_view, 4> kStartupSongWords{"blank", "template", "lastOpened", "lastSaved"};
/// How a new slice plays its sample, in the order a sample's repeat mode numbers them.
constexpr std::array<std::string_view, 4> kSliceModeWords{"cut", "once", "loop", "stretch"};
/// How loud a sound starts a threshold recording, in the order the recording mode numbers them.
constexpr std::array<std::string_view, 4> kThresholdWords{"off", "low", "medium", "high"};
/// The kinds of clip a new clip may be, in the order an output's type numbers them.
constexpr std::array<std::string_view, 5> kNewClipTypeWords{"synth", "kit", "midi", "cv", "audio"};
/// When an input being recorded is heard, in the order the monitoring mode numbers them.
constexpr std::array<std::string_view, 3> kMonitorModeWords{"smart", "on", "off"};
/// What the device sends the USB host, in the order the USB output source numbers them.
constexpr std::array<std::string_view, 2> kUsbOutputSourceWords{"mainMix", "lineIn"};
constexpr int32_t kLeastPpqn = 1;      ///< the fewest trigger-clock pulses a quarter note a menu sets
constexpr int32_t kGreatestPpqn = 192; ///< the most
constexpr int32_t kDefaultPpqn = 24;   ///< what the device's settings reset to

/// The bounds a default range's two ends share: the least and greatest end, and the end a reset gives both.
struct EndBounds {
	int32_t least;
	int32_t greatest;
	int32_t reset;
};
/// A new song's tempo, in beats a minute.
constexpr EndBounds kTempoEnds{.least = 60, .greatest = 240, .reset = 120};
/// A new song's swing, in percent.
constexpr EndBounds kSwingEnds{.least = 1, .greatest = 99, .reset = 50};
/// A new song's key, in semitones above C.
constexpr EndBounds kKeyEnds{.least = 0, .greatest = 11, .reset = 0};
/// The magnitude a reset gives: 96 << 2 ticks a quarter note.
constexpr int32_t kResetMagnitude = 2;

constexpr SettingInfo boolean(std::string_view path, int32_t default_value) {
	return SettingInfo{.path = path, .kind = SettingKind::BOOL, .default_value = default_value};
}

constexpr SettingInfo choice(std::string_view path, std::span<const std::string_view> choices, int32_t default_value) {
	return SettingInfo{.path = path, .kind = SettingKind::CHOICE, .choices = choices, .default_value = default_value};
}

/// An INTEGER row of @p least to @p greatest, counting @p units.
constexpr SettingInfo integer(std::string_view path, int32_t least, int32_t greatest, int32_t default_value,
                              SettingUnits units = SettingUnits::NONE) {
	return SettingInfo{.path = path,
	                   .kind = SettingKind::INTEGER,
	                   .min = least,
	                   .max = greatest,
	                   .default_value = default_value,
	                   .units = units};
}

/// The rows a default range's two ends are.
constexpr std::array<SettingInfo, 2> ends(DevicePath lower, DevicePath upper, EndBounds bounds) {
	return {integer(lower, bounds.least, bounds.greatest, bounds.reset),
	        integer(upper, bounds.least, bounds.greatest, bounds.reset)};
}

/// The defaults' rows: the defaults page's settings and the recording page's three, the magnitude before the swing
/// interval it moves.
constexpr std::array kDefaultsRows = [] {
	const auto tempo = ends(device_path::kTempoLower, device_path::kTempoUpper, kTempoEnds);
	const auto swing = ends(device_path::kSwingLower, device_path::kSwingUpper, kSwingEnds);
	const auto key = ends(device_path::kKeyLower, device_path::kKeyUpper, kKeyEnds);
	return std::array{
	    tempo[0],
	    tempo[1],
	    swing[0],
	    swing[1],
	    key[0],
	    key[1],
	    integer(device_path::kMagnitude, 0, 6, kResetMagnitude),
	    // 16th notes at the reset magnitude.
	    integer(device_path::kSwingInterval, 0, plugin::kNumSwingIntervals - 1, 8 - kResetMagnitude,
	            SettingUnits::SYNC_LEVEL),
	    choice(device_path::kScale, kScaleWords, 0),
	    integer(device_path::kVelocity, 1, 127, 64),
	    integer(device_path::kBendRange, 0, 96, 2),
	    integer(device_path::kMetronomeVolume, 1, 50, 50),
	    choice(device_path::kStartupSong, kStartupSongWords, 0),
	    choice(device_path::kSliceMode, kSliceModeWords, 0),
	    boolean(device_path::kCpuIndicator, 0),
	    integer(device_path::kHoldTime, 1, 20, 2),
	    // Quantised to 16th notes; no triplet or dotted level.
	    integer(device_path::kRecordQuantize, 0, std::to_underlying(plugin::SyncType::TRIPLET) - 1, 8,
	            SettingUnits::SYNC_LEVEL),
	    boolean(device_path::kRecordMargins, 1),
	    choice(device_path::kThresholdRecording, kThresholdWords, 0),
	    choice(device_path::kNewClipType, kNewClipTypeWords, 0),
	    boolean(device_path::kUseLastClipType, 1),
	    boolean(device_path::kAutomationInterpolate, 1),
	    boolean(device_path::kAutomationClear, 1),
	    boolean(device_path::kAutomationShift, 1),
	    boolean(device_path::kAutomationNudgeNote, 1),
	    boolean(device_path::kAutomationNoAuditionShortcuts, 1),
	};
}();

/// The pads' and the screen's rows.
constexpr std::array kPadsAndScreenRows{
    choice(device_path::kShortcutsVersion, kShortcutsVersionWords, std::to_underlying(ShortcutsVersion::V3)),
    choice(device_path::kPlayCursor, kPlayCursorWords, std::to_underlying(FlashCursor::SLOW)),
    choice(device_path::kTextKeyboardLayout, kTextKeyboardWords, 0),
    choice(device_path::kPadColourActive, kPadColourWords, kGreen),
    choice(device_path::kPadColourStopped, kPadColourWords, kRed),
    choice(device_path::kPadColourMuted, kPadColourWords, kYellow),
    choice(device_path::kPadColourSoloed, kPadColourWords, kBlue),
    choice(device_path::kPadColourFill, kPadColourWords, kAmber),
    choice(device_path::kPadColourOnce, kPadColourWords, kMagenta),
    boolean(device_path::kAccessibilityShortcuts, 0),
    choice(device_path::kMenuHighlighting, kHighlightingWords, 1),
    choice(device_path::kSessionLayout, kSessionLayoutWords, 0),
    choice(device_path::kGridActiveMode, kGridModeWords, 0),
    boolean(device_path::kGridAllowGreenSelection, 1),
    boolean(device_path::kGridEmptyPadsUnarm, 0),
    boolean(device_path::kGridEmptyPadsCreateRec, 0),
    choice(device_path::kKeyboardLayout, kKeyboardLayoutWords, 0),
    boolean(device_path::kVelocityMomentary, 1),
    boolean(device_path::kModwheelMomentary, 1),
    choice(device_path::kFavouritesLayout, kFavouritesLayoutWords, std::to_underlying(FavouritesLayout::FAVOURITES)),
    // A menu shows four times the level, as the pad brightness item does.
    SettingInfo{.path = device_path::kPadBrightness,
                .kind = SettingKind::INTEGER,
                .min = 0,
                .max = kBrightestPadLevel,
                .user = {.user_min = 0,
                         .user_max = kPadLevelShown * kBrightestPadLevel,
                         .stored_min = 0,
                         .stored_max = kBrightestPadLevel},
                .default_value = kBrightestPadLevel},
    choice(device_path::kScreensaverMode, kScreensaverWords, 3),
    integer(device_path::kScreensaverTimeout, 1, 60, 5),
    choice(device_path::kSamplePreview, kSamplePreviewWords, 1),
};

/// The recording page's count-in and monitoring, USB audio's and the clocks' rows, and the stem export's options,
/// which nothing saves.
constexpr std::array kAudioAndClockRows{
    integer(device_path::kCountIn, 0, 4, 0),
    choice(device_path::kMonitorMode, kMonitorModeWords, 0),
    boolean(device_path::kUsbAudioEnabled, 1),
    boolean(device_path::kUsbDirectMonitoring, 1),
    choice(device_path::kUsbOutputSource, kUsbOutputSourceWords, 0),
    integer(device_path::kTriggerInPpqn, kLeastPpqn, kGreatestPpqn, kDefaultPpqn),
    boolean(device_path::kTriggerInAutoStart, 1),
    integer(device_path::kTriggerOutPpqn, kLeastPpqn, kGreatestPpqn, kDefaultPpqn),
    boolean(device_path::kMidiClockOut, 1),
    boolean(device_path::kMidiClockIn, 1),
    boolean(device_path::kTempoMagnitudeMatching, 0),
    boolean(device_path::kStemNormalize, 0),
    boolean(device_path::kStemNormalizeDrums, 1),
    boolean(device_path::kStemToSilence, 1),
    boolean(device_path::kStemSongFx, 0),
    boolean(device_path::kStemKitFx, 0),
    boolean(device_path::kStemOffline, 1),
    boolean(device_path::kStemMixdown, 0),
};

/// The rows before the preset scales'.
constexpr size_t kLeadingRows = 2 + kDefaultsRows.size();
/// The rows after them, before the community features'.
constexpr size_t kTrailingRows = kPadsAndScreenRows.size() + kAudioAndClockRows.size();
/// How many rows the table has.
constexpr size_t kRowCount = kLeadingRows + kScaleCount + kTrailingRows + kFeatureCount;

/// @brief Community feature @p info's row: BOOL for an on/off feature, else CHOICE over its words.
constexpr SettingInfo feature_row(const FeatureInfo& info) {
	const std::string_view path = device_path::feature(info.id);
	return is_toggle(info) ? boolean(path, info.default_value) : choice(path, info.choices, info.default_value);
}

/// The DEVICE table: each row's default is the one the device's settings reset to.
constexpr std::array<SettingInfo, kRowCount> kDeviceTable = [] {
	std::array<SettingInfo, kRowCount> rows{};
	size_t at = 0;
	rows[at++] = boolean(device_path::kUseSharps, 1);
	rows[at++] = choice(device_path::kPatchCablePolarity, kPolarityWords, 1);
	for (const SettingInfo& row : kDefaultsRows) {
		rows[at++] = row;
	}
	for (const ScalePath& path : kScalePaths) {
		rows[at++] = boolean(std::string_view{path.text.data(), path.size}, 0);
	}
	for (const SettingInfo& row : kPadsAndScreenRows) {
		rows[at++] = row;
	}
	for (const SettingInfo& row : kAudioAndClockRows) {
		rows[at++] = row;
	}
	for (const FeatureInfo& info : kFeatures) {
		rows[at++] = feature_row(info);
	}
	return rows;
}();
static_assert(plugin::conditions_are_valid(kDeviceTable));
// Every path the scenes name is a row of the table; a feature's and a scale's rows are made from the same lists.
static_assert([] {
	for (const DevicePath path : {
	         device_path::kUseSharps,
	         device_path::kPatchCablePolarity,
	         device_path::kTempoLower,
	         device_path::kTempoUpper,
	         device_path::kSwingLower,
	         device_path::kSwingUpper,
	         device_path::kKeyLower,
	         device_path::kKeyUpper,
	         device_path::kMagnitude,
	         device_path::kSwingInterval,
	         device_path::kScale,
	         device_path::kVelocity,
	         device_path::kBendRange,
	         device_path::kMetronomeVolume,
	         device_path::kStartupSong,
	         device_path::kSliceMode,
	         device_path::kCpuIndicator,
	         device_path::kHoldTime,
	         device_path::kRecordQuantize,
	         device_path::kRecordMargins,
	         device_path::kThresholdRecording,
	         device_path::kNewClipType,
	         device_path::kUseLastClipType,
	         device_path::kAutomationInterpolate,
	         device_path::kAutomationClear,
	         device_path::kAutomationShift,
	         device_path::kAutomationNudgeNote,
	         device_path::kAutomationNoAuditionShortcuts,
	         device_path::kShortcutsVersion,
	         device_path::kPlayCursor,
	         device_path::kTextKeyboardLayout,
	         device_path::kPadColourActive,
	         device_path::kPadColourStopped,
	         device_path::kPadColourMuted,
	         device_path::kPadColourSoloed,
	         device_path::kPadColourFill,
	         device_path::kPadColourOnce,
	         device_path::kAccessibilityShortcuts,
	         device_path::kMenuHighlighting,
	         device_path::kSessionLayout,
	         device_path::kGridActiveMode,
	         device_path::kGridAllowGreenSelection,
	         device_path::kGridEmptyPadsUnarm,
	         device_path::kGridEmptyPadsCreateRec,
	         device_path::kKeyboardLayout,
	         device_path::kVelocityMomentary,
	         device_path::kModwheelMomentary,
	         device_path::kFavouritesLayout,
	         device_path::kPadBrightness,
	         device_path::kScreensaverMode,
	         device_path::kScreensaverTimeout,
	         device_path::kSamplePreview,
	         device_path::kCountIn,
	         device_path::kMonitorMode,
	         device_path::kUsbAudioEnabled,
	         device_path::kUsbDirectMonitoring,
	         device_path::kUsbOutputSource,
	         device_path::kTriggerInPpqn,
	         device_path::kTriggerInAutoStart,
	         device_path::kTriggerOutPpqn,
	         device_path::kMidiClockOut,
	         device_path::kMidiClockIn,
	         device_path::kTempoMagnitudeMatching,
	         device_path::kStemNormalize,
	         device_path::kStemNormalizeDrums,
	         device_path::kStemToSilence,
	         device_path::kStemSongFx,
	         device_path::kStemKitFx,
	         device_path::kStemOffline,
	         device_path::kStemMixdown,
	     }) {
		if (plugin::find_setting(kDeviceTable, path.text()) == nullptr) {
			return false;
		}
	}
	for (const FeatureInfo& info : kFeatures) {
		if (plugin::find_setting(kDeviceTable, device_path::feature(info.id).text()) == nullptr) {
			return false;
		}
	}
	return true;
}());
static_assert(plugin::mappings_are_valid(kDeviceTable));

} // namespace menu_tables
} // namespace

std::span<const plugin::SettingInfo> device_table() {
	return kDeviceTable;
}

std::optional<DevicePath> device_path::disabled_scale(size_t scale) {
	if (scale >= kScalePaths.size()) {
		return std::nullopt;
	}
	const ScalePath& path = kScalePaths[scale];
	return DevicePath{std::string_view{path.text.data(), path.size}};
}

} // namespace deluge::project
