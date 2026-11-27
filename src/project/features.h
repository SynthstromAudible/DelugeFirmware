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
/// @brief The community features: the device's switchable behaviours, each with the name the feature file
///        (`SETTINGS/CommunityFeatures.XML`) writes it under, the words for its values and its factory value.
///
/// A feature's value is the index of its word in `choices`. The file stores that index as a number; a menu shows the
/// feature as the DEVICE row `features/<path>` (`device_settings.h`).

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <utility>

namespace deluge::project {

/// @brief A community feature the device can turn on, in the order the feature file has always listed them.
enum class Feature : uint8_t {
	DRUM_RANDOMIZER,
	QUANTIZE,
	FINE_TEMPO_KNOB,
	CATCH_NOTES,
	DELETE_UNUSED_KIT_ROWS,
	ALT_GOLDEN_KNOB_DELAY_PARAMS,
	SYNC_SCALING_ACTION,
	HIGHLIGHT_INCOMING_NOTES,
	DISPLAY_NORNS_LAYOUT,
	SHIFT_IS_STICKY,
	LIGHT_SHIFT_LED,
	ENABLE_KEYBOARD_VIEW_SIDEBAR_MENU_EXIT,
	ENABLE_LAUNCH_EVENT_PLAYHEAD,
	DISPLAY_CHORD_KEYBOARD,
	ALTERNATIVE_PLAYBACK_START_BEHAVIOUR,
	ENABLE_GRID_VIEW_LOOP_PADS,
	ALTERNATIVE_TAP_TEMPO_BEHAVIOUR,
	HORIZONTAL_MENUS,
	TRIM_FROM_START_OF_AUDIO_CLIP,
	SHOW_BATTERY_LEVEL,
	ROUNDED_CORNERS,
	SHORTCUT_OVERLAY,
};

/// @brief What the SHIFT shortcut overlay covers: the `SHORTCUT_OVERLAY` feature's values.
enum class ShortcutOverlay : uint8_t {
	OFF,           ///< no overlay
	MENU_ONLY,     ///< the overlay shows over a menu; the factory value
	MENU_AND_CLIP, ///< it shows over a menu and over a clip
};

/// @brief What the sync-scaling button does: the `SYNC_SCALING_ACTION` feature's values.
enum class SyncScalingAction : uint8_t {
	SYNC_SCALING, ///< it sets the clip that the others' lengths scale to; the factory value
	FILL,         ///< it holds fill mode on while pressed
};

/// @brief One feature: its file name, its choices and its default.
struct FeatureInfo {
	Feature id;            ///< the feature, which is also its index in `kFeatures`
	std::string_view path; ///< the name the feature file writes: a string literal, so its text ends in a NUL
	std::span<const std::string_view> choices; ///< the words for its values, in value order
	uint8_t default_value{0};                  ///< the value a factory reset gives it
};

/// The menu-time tables: read by a menu and by the feature file's read and write, never by the audio tier or an
/// interrupt. The device image places this namespace's data in SDRAM (linker/sdram_sections.x matches its name).
inline namespace menu_tables {

/// @brief The words of an on/off feature's two values.
inline constexpr std::array<std::string_view, 2> kFeatureToggleWords{"off", "on"};
/// @brief The words of the sync-scaling action's values, in SyncScalingAction's order.
inline constexpr std::array<std::string_view, 2> kSyncScalingActionWords{"syncScaling", "fill"};
/// @brief The words of the shortcut overlay's values, in ShortcutOverlay's order.
inline constexpr std::array<std::string_view, 3> kShortcutOverlayWords{"off", "menuOnly", "menuAndClip"};

/// @brief Every feature, indexed by its id.
inline constexpr auto kFeatures = std::to_array<FeatureInfo>({
    {.id = Feature::DRUM_RANDOMIZER, .path = "drumRandomizer", .choices = kFeatureToggleWords, .default_value = 1},
    {.id = Feature::QUANTIZE, .path = "quantize", .choices = kFeatureToggleWords, .default_value = 1},
    {.id = Feature::FINE_TEMPO_KNOB, .path = "fineTempoKnob", .choices = kFeatureToggleWords, .default_value = 1},
    {.id = Feature::CATCH_NOTES, .path = "catchNotes", .choices = kFeatureToggleWords, .default_value = 1},
    {.id = Feature::DELETE_UNUSED_KIT_ROWS,
     .path = "deleteUnusedKitRows",
     .choices = kFeatureToggleWords,
     .default_value = 1},
    {.id = Feature::ALT_GOLDEN_KNOB_DELAY_PARAMS,
     .path = "altGoldenKnobDelayParams",
     .choices = kFeatureToggleWords,
     .default_value = 0},
    {.id = Feature::SYNC_SCALING_ACTION,
     .path = "syncScalingAction",
     .choices = kSyncScalingActionWords,
     .default_value = std::to_underlying(SyncScalingAction::SYNC_SCALING)},
    {.id = Feature::HIGHLIGHT_INCOMING_NOTES,
     .path = "highlightIncomingNotes",
     .choices = kFeatureToggleWords,
     .default_value = 1},
    {.id = Feature::DISPLAY_NORNS_LAYOUT,
     .path = "displayNornsLayout",
     .choices = kFeatureToggleWords,
     .default_value = 0},
    {.id = Feature::SHIFT_IS_STICKY, .path = "stickyShift", .choices = kFeatureToggleWords, .default_value = 0},
    {.id = Feature::LIGHT_SHIFT_LED, .path = "lightShift", .choices = kFeatureToggleWords, .default_value = 0},
    {.id = Feature::ENABLE_KEYBOARD_VIEW_SIDEBAR_MENU_EXIT,
     .path = "enableKeyboardViewSidebarMenuExit",
     .choices = kFeatureToggleWords,
     .default_value = 0},
    {.id = Feature::ENABLE_LAUNCH_EVENT_PLAYHEAD,
     .path = "enableLaunchEventPlayhead",
     .choices = kFeatureToggleWords,
     .default_value = 1},
    {.id = Feature::DISPLAY_CHORD_KEYBOARD,
     .path = "displayChordKeyboard",
     .choices = kFeatureToggleWords,
     .default_value = 0},
    {.id = Feature::ALTERNATIVE_PLAYBACK_START_BEHAVIOUR,
     .path = "alternativePlaybackStartBehaviour",
     .choices = kFeatureToggleWords,
     .default_value = 0},
    {.id = Feature::ENABLE_GRID_VIEW_LOOP_PADS,
     .path = "enableGridViewLoopPads",
     .choices = kFeatureToggleWords,
     .default_value = 0},
    {.id = Feature::ALTERNATIVE_TAP_TEMPO_BEHAVIOUR,
     .path = "alternativeTapTempoBehaviour",
     .choices = kFeatureToggleWords,
     .default_value = 0},
    {.id = Feature::HORIZONTAL_MENUS,
     .path = "enableHorizontalMenus",
     .choices = kFeatureToggleWords,
     .default_value = 1},
    {.id = Feature::TRIM_FROM_START_OF_AUDIO_CLIP,
     .path = "trimFromStartOfAudioClip",
     .choices = kFeatureToggleWords,
     .default_value = 1},
    {.id = Feature::SHOW_BATTERY_LEVEL, .path = "showBatteryLevel", .choices = kFeatureToggleWords, .default_value = 1},
    {.id = Feature::ROUNDED_CORNERS, .path = "roundedCorners", .choices = kFeatureToggleWords, .default_value = 1},
    {.id = Feature::SHORTCUT_OVERLAY,
     .path = "shortcutOverlay",
     .choices = kShortcutOverlayWords,
     .default_value = std::to_underlying(ShortcutOverlay::MENU_ONLY)},
});

} // namespace menu_tables

/// @brief How many features there are: the table's size.
inline constexpr size_t kFeatureCount = kFeatures.size();
static_assert(std::to_underlying(Feature::SHORTCUT_OVERLAY) + 1 == kFeatureCount,
              "every Feature has an entry: point this at the last enumerator when adding one");

/// @brief The table's entry for @p f.
///
/// An id with no entry stops the program: a constant-evaluated call with one does not compile, because `std::abort`
/// is no constant expression, and a run-time one aborts rather than read past the table.
/// @param f A feature.
/// @return Its entry in `kFeatures`.
[[nodiscard]] constexpr const FeatureInfo& feature_info(Feature f) {
	const size_t index = std::to_underlying(f);
	if (index >= kFeatures.size()) {
		std::abort();
	}
	return kFeatures[index];
}

/// @brief Whether @p info is an on/off feature: its values are off and on.
/// @param info A feature's entry.
/// @return True for an on/off feature, false for one with choices of its own.
[[nodiscard]] constexpr bool is_toggle(const FeatureInfo& info) {
	return info.choices.data() == kFeatureToggleWords.data();
}

/// @brief The feature the file names @p path.
/// @param path A name from the feature file.
/// @return The feature's entry, or null for a name no feature has.
[[nodiscard]] constexpr const FeatureInfo* find_feature(std::string_view path) {
	for (const FeatureInfo& info : kFeatures) {
		if (info.path == path) {
			return &info;
		}
	}
	return nullptr;
}

} // namespace deluge::project
