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
	ENABLE_DX7_ENGINE,
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
	Feature id;                                ///< the feature, which is also its index in `kFeatures`
	std::string_view path;                     ///< the name the feature file writes
	std::span<const std::string_view> choices; ///< the words for its values, in value order
	uint8_t default_value{0};                  ///< the value a factory reset gives it
};

/// @brief How many features there are.
inline constexpr size_t kFeatureCount = std::to_underlying(Feature::SHORTCUT_OVERLAY) + 1;

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
inline constexpr std::array<FeatureInfo, kFeatureCount> kFeatures{{
    {Feature::DRUM_RANDOMIZER, "drumRandomizer", kFeatureToggleWords, 1},
    {Feature::QUANTIZE, "quantize", kFeatureToggleWords, 1},
    {Feature::FINE_TEMPO_KNOB, "fineTempoKnob", kFeatureToggleWords, 1},
    {Feature::CATCH_NOTES, "catchNotes", kFeatureToggleWords, 1},
    {Feature::DELETE_UNUSED_KIT_ROWS, "deleteUnusedKitRows", kFeatureToggleWords, 1},
    {Feature::ALT_GOLDEN_KNOB_DELAY_PARAMS, "altGoldenKnobDelayParams", kFeatureToggleWords, 0},
    {Feature::SYNC_SCALING_ACTION, "syncScalingAction", kSyncScalingActionWords,
     std::to_underlying(SyncScalingAction::SYNC_SCALING)},
    {Feature::HIGHLIGHT_INCOMING_NOTES, "highlightIncomingNotes", kFeatureToggleWords, 1},
    {Feature::DISPLAY_NORNS_LAYOUT, "displayNornsLayout", kFeatureToggleWords, 0},
    {Feature::SHIFT_IS_STICKY, "stickyShift", kFeatureToggleWords, 0},
    {Feature::LIGHT_SHIFT_LED, "lightShift", kFeatureToggleWords, 0},
    {Feature::ENABLE_DX7_ENGINE, "EnableDX7Engine", kFeatureToggleWords, 0},
    {Feature::ENABLE_KEYBOARD_VIEW_SIDEBAR_MENU_EXIT, "enableKeyboardViewSidebarMenuExit", kFeatureToggleWords, 0},
    {Feature::ENABLE_LAUNCH_EVENT_PLAYHEAD, "enableLaunchEventPlayhead", kFeatureToggleWords, 1},
    {Feature::DISPLAY_CHORD_KEYBOARD, "displayChordKeyboard", kFeatureToggleWords, 0},
    {Feature::ALTERNATIVE_PLAYBACK_START_BEHAVIOUR, "alternativePlaybackStartBehaviour", kFeatureToggleWords, 0},
    {Feature::ENABLE_GRID_VIEW_LOOP_PADS, "enableGridViewLoopPads", kFeatureToggleWords, 0},
    {Feature::ALTERNATIVE_TAP_TEMPO_BEHAVIOUR, "alternativeTapTempoBehaviour", kFeatureToggleWords, 0},
    {Feature::HORIZONTAL_MENUS, "enableHorizontalMenus", kFeatureToggleWords, 1},
    {Feature::TRIM_FROM_START_OF_AUDIO_CLIP, "trimFromStartOfAudioClip", kFeatureToggleWords, 1},
    {Feature::SHOW_BATTERY_LEVEL, "showBatteryLevel", kFeatureToggleWords, 1},
    {Feature::ROUNDED_CORNERS, "roundedCorners", kFeatureToggleWords, 1},
    {Feature::SHORTCUT_OVERLAY, "shortcutOverlay", kShortcutOverlayWords,
     std::to_underlying(ShortcutOverlay::MENU_ONLY)},
}};

} // namespace menu_tables

/// @brief The table's entry for @p f.
/// @param f A feature.
/// @return Its entry in `kFeatures`.
[[nodiscard]] constexpr const FeatureInfo& feature_info(Feature f) {
	return kFeatures[std::to_underlying(f)];
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
