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

/// @file
/// @brief The settings root's `get`/`set` hooks and its exported root span. The `Node` literals are
///        in `settings.h`, which also lists what stayed behind and why.
///
/// Every hook is a transcription of the legacy `readCurrentValue`/`writeCurrentValue` cited above
/// it. None of them reads `MenuContext` except the two that edit the song (`defaults::Velocity`'s
/// tail): a setting is device-wide, which is exactly why the settings root's `MenuContext` has a
/// null `params` and a null `sound`.

#include "scenes/menu/nodes/settings.h"

#include "scenes/l10n_bridge.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_transpose.h"
#include "model/scale/preset_scales.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "model/sync.h"
#include "modulation/patch/patch_cable.h" // polarityToString
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/metronome/metronome.h" // AudioEngine::metronome's definition
#include "storage/flash_storage.h"

#include <version.h> // kFirmwareVersionString

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>

// `battery::Level::isRelevant` reads `runtimeFeatureSettings` (`relevance::battery_shown`, this
// file's own relevance.cpp); the value itself is `batteryMV`, a plain global the main loop sets
// (`deluge.cpp:89,146`) -- declared bare and at global scope, exactly as `sound_editor.cpp:1403`
// and `battery/level.h:26` both do (a namespaced `extern` here would name a *different*,
// unlinked variable rather than referring to the one `deluge.cpp` defines).
extern uint16_t batteryMV;

namespace deluge::scenes::menu::nodes {

namespace {

/// @brief Read a plain `bool` global; @p invert is the `InvertedToggleBool` spelling
///        (`gui/menu_item/toggle.h`'s two pairs).
constexpr int32_t from_bool(bool value, bool invert = false) {
	return (invert ? !value : value) ? 1 : 0;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Trigger clock

int32_t get_trigger_in_ppqn(const MenuContext&) {
	return static_cast<int32_t>(playbackHandler.analogInTicksPPQN); // `trigger/in/ppqn.h:22`
}
void set_trigger_in_ppqn(const MenuContext& ctx, int32_t value) {
	playbackHandler.analogInTicksPPQN = value; // `trigger/in/ppqn.h:23-27`
	if (playbackHandler.isExternalClockActive() && playbackHandler.usingAnalogClockInput) {
		playbackHandler.resyncInternalTicksToInputTicks(ctx.song);
	}
}

int32_t get_trigger_in_auto_start(const MenuContext&) {
	return from_bool(playbackHandler.analogClockInputAutoStart); // `menus.cpp:1500`
}
void set_trigger_in_auto_start(const MenuContext&, int32_t value) {
	playbackHandler.analogClockInputAutoStart = value != 0;
}

int32_t get_trigger_out_ppqn(const MenuContext&) {
	return static_cast<int32_t>(playbackHandler.analogOutTicksPPQN); // `trigger/out/ppqn.h:22`
}
void set_trigger_out_ppqn(const MenuContext&, int32_t value) {
	playbackHandler.analogOutTicksPPQN = value; // `trigger/out/ppqn.h:23-25`
	playbackHandler.resyncAnalogOutTicksToInternalTicks();
}

// ---------------------------------------------------------------------------------------------
// MIDI clock

int32_t get_midi_clock_out(const MenuContext&) {
	return from_bool(playbackHandler.midiOutClockEnabled); // `menus.cpp:1459`, a `ToggleBool` over it
}
void set_midi_clock_out(const MenuContext&, int32_t value) {
	playbackHandler.setMidiOutClockMode(value != 0); // `midi/send_clock.h:22`
}

int32_t get_midi_clock_in(const MenuContext&) {
	return from_bool(playbackHandler.midiInClockEnabled); // `menus.cpp:1461`
}
void set_midi_clock_in(const MenuContext&, int32_t value) {
	playbackHandler.setMidiInClockEnabled(value != 0); // `midi/receive_clock.h:22`
}

int32_t get_tempo_magnitude_matching(const MenuContext&) {
	return from_bool(playbackHandler.tempoMagnitudeMatchingEnabled); // `menus.cpp:1464`
}
void set_tempo_magnitude_matching(const MenuContext&, int32_t value) {
	playbackHandler.tempoMagnitudeMatchingEnabled = value != 0;
}

// ---------------------------------------------------------------------------------------------
// MIDI follow

int32_t follow_channel_get(int32_t which) {
	return midiEngine.midiFollowChannelType[which].channelOrZone; // `midi/follow/follow_channel.h:29`
}
void follow_channel_set(int32_t which, int32_t value) {
	midiEngine.midiFollowChannelType[which].channelOrZone = value; // `follow_channel.h:30`
}

int32_t get_follow_kit_root_note(const MenuContext&) {
	return midiEngine.midiFollowKitRootNote; // `midi/follow/follow_kit_root_note.h:23`
}
void set_follow_kit_root_note(const MenuContext&, int32_t value) {
	midiEngine.midiFollowKitRootNote = static_cast<uint8_t>(value); // `follow_kit_root_note.h:24`
}

int32_t get_follow_feedback_channel(const MenuContext&) {
	return static_cast<int32_t>(midiEngine.midiFollowFeedbackChannelType); // `follow_feedback_channel_type.h:22`
}
void set_follow_feedback_channel(const MenuContext&, int32_t value) {
	midiEngine.midiFollowFeedbackChannelType = static_cast<MIDIFollowFeedbackChannelType>(value); // `:23-25`
}

size_t follow_feedback_channel_names(const MenuContext&, std::span<std::string_view> out) {
	// `FollowFeedbackChannelType::getOptions` (`follow_feedback_channel_type.h:24-42`): four l10n
	// strings, then the Track label with its "**" index placeholder trimmed, then that label joined
	// to each of A/B/C. The three joined labels are built into this translation unit's own buffers,
	// exactly as legacy builds them into the menu item's (`:76-88`).
	using deluge::l10n::String;
	static constexpr String kFixed[] = {String::STRING_FOR_NONE, String::STRING_FOR_FOLLOW_CHANNEL_A,
	                                    String::STRING_FOR_FOLLOW_CHANNEL_B, String::STRING_FOR_FOLLOW_CHANNEL_C};
	static constexpr String kJoined[] = {String::STRING_FOR_FOLLOW_CHANNEL_A, String::STRING_FOR_FOLLOW_CHANNEL_B,
	                                     String::STRING_FOR_FOLLOW_CHANNEL_C};
	static char buffers[3][80];

	std::string_view track = deluge::scenes::text_lookup(sid(String::STRING_FOR_FOLLOW_CHANNEL_TRACK));
	if (track.size() >= 2 && track.substr(track.size() - 2) == "**") {
		track.remove_suffix(2);
	}

	size_t n = 0;
	for (String s : kFixed) {
		if (n < out.size()) {
			out[n++] = deluge::scenes::text_lookup(sid(s));
		}
	}
	if (n < out.size()) {
		out[n++] = track;
	}
	for (size_t i = 0; i < 3 && n < out.size(); i++) {
		const std::string_view channel = deluge::scenes::text_lookup(sid(kJoined[i]));
		size_t len = 0;
		const auto append = [&](std::string_view text) {
			for (char c : text) {
				if (len + 1 < sizeof(buffers[i])) {
					buffers[i][len++] = c;
				}
			}
		};
		append(track);
		append(" + ");
		append(channel);
		out[n++] = std::string_view{buffers[i], len};
	}
	return n;
}

int32_t get_follow_feedback_automation(const MenuContext&) {
	return static_cast<int32_t>(midiEngine.midiFollowFeedbackAutomation); // `follow_feedback_automation.h:21`
}
void set_follow_feedback_automation(const MenuContext&, int32_t value) {
	midiEngine.midiFollowFeedbackAutomation = static_cast<MIDIFollowFeedbackAutomationMode>(value); // `:22-24`
}

int32_t get_follow_feedback_filter(const MenuContext&) {
	return from_bool(midiEngine.midiFollowFeedbackFilter); // `menus.cpp:1362`
}
void set_follow_feedback_filter(const MenuContext&, int32_t value) {
	midiEngine.midiFollowFeedbackFilter = value != 0;
}

int32_t get_follow_display_param(const MenuContext&) {
	return from_bool(midiEngine.midiFollowDisplayParam); // `menus.cpp:1357`
}
void set_follow_display_param(const MenuContext&, int32_t value) {
	midiEngine.midiFollowDisplayParam = value != 0;
}

// ---------------------------------------------------------------------------------------------
// The rest of the MIDI page

int32_t get_midi_select_kit_row(const MenuContext&) {
	return from_bool(midiEngine.midiSelectKitRow); // `menus.cpp:1400`
}
void set_midi_select_kit_row(const MenuContext&, int32_t value) {
	midiEngine.midiSelectKitRow = value != 0;
}

int32_t get_midi_thru(const MenuContext&) {
	return from_bool(midiEngine.midiThru); // `menus.cpp:1312`
}
void set_midi_thru(const MenuContext&, int32_t value) {
	midiEngine.midiThru = value != 0;
}

int32_t get_midi_transpose_method(const MenuContext&) {
	return static_cast<int32_t>(MIDITranspose::controlMethod); // `midi/transpose.h:21`
}
void set_midi_transpose_method(const MenuContext&, int32_t value) {
	MIDITranspose::controlMethod = static_cast<MIDITransposeControlMethod>(value); // `midi/transpose.h:22`
}

int32_t get_midi_takeover(const MenuContext&) {
	return static_cast<int32_t>(midiEngine.midiTakeover); // `midi/takeover.h:21`
}
void set_midi_takeover(const MenuContext&, int32_t value) {
	midiEngine.midiTakeover = static_cast<MIDITakeoverMode>(value); // `midi/takeover.h:22`
}

int32_t get_midi_input_differentiation(const MenuContext&) {
	return from_bool(MIDIDeviceManager::differentiatingInputsByDevice); // `menus.cpp:1456`
}
void set_midi_input_differentiation(const MenuContext&, int32_t value) {
	MIDIDeviceManager::differentiatingInputsByDevice = value != 0;
}

// ---------------------------------------------------------------------------------------------
// Defaults: UI

int32_t get_default_accessibility_shortcuts(const MenuContext&) {
	return from_bool(FlashStorage::accessibilityShortcuts); // `menus.cpp:1585`
}
void set_default_accessibility_shortcuts(const MenuContext&, int32_t value) {
	FlashStorage::accessibilityShortcuts = value != 0;
}

int32_t get_default_menu_highlighting(const MenuContext&) {
	return static_cast<int32_t>(FlashStorage::accessibilityMenuHighlighting); // `accessibility_menu_highlighting.h:11`
}
void set_default_menu_highlighting(const MenuContext&, int32_t value) {
	// `:12-16`. The `Canvas::menuHighlightingInverts` poke is `hid/` -- see `settings.h`'s note.
	FlashStorage::accessibilityMenuHighlighting = static_cast<MenuHighlighting>(value);
}

int32_t get_default_session_layout(const MenuContext&) {
	return static_cast<int32_t>(FlashStorage::defaultSessionLayout); // `defaults/session_layout.h:11`
}
void set_default_session_layout(const MenuContext&, int32_t value) {
	FlashStorage::defaultSessionLayout = static_cast<SessionLayoutType>(value); // `:12`
}

int32_t get_default_grid_active_mode(const MenuContext&) {
	return static_cast<int32_t>(FlashStorage::defaultGridActiveMode); // `defaults/grid_default_active_mode.h:11`
}
void set_default_grid_active_mode(const MenuContext&, int32_t value) {
	FlashStorage::defaultGridActiveMode = static_cast<GridDefaultActiveMode>(value); // `:12`
}

int32_t get_default_grid_green_selection(const MenuContext&) {
	return from_bool(FlashStorage::gridAllowGreenSelection); // `menus.cpp:1570`
}
void set_default_grid_green_selection(const MenuContext&, int32_t value) {
	FlashStorage::gridAllowGreenSelection = value != 0;
}

int32_t get_default_grid_pads_unarm(const MenuContext&) {
	return from_bool(FlashStorage::gridEmptyPadsUnarm); // `menus.cpp:1557`
}
void set_default_grid_pads_unarm(const MenuContext&, int32_t value) {
	FlashStorage::gridEmptyPadsUnarm = value != 0;
}

int32_t get_default_grid_pads_create_rec(const MenuContext&) {
	return from_bool(FlashStorage::gridEmptyPadsCreateRec); // `menus.cpp:1560`
}
void set_default_grid_pads_create_rec(const MenuContext&, int32_t value) {
	FlashStorage::gridEmptyPadsCreateRec = value != 0;
}

int32_t get_default_keyboard_layout(const MenuContext&) {
	return static_cast<int32_t>(FlashStorage::defaultKeyboardLayout); // `defaults/keyboard_layout.h:11`
}
void set_default_keyboard_layout(const MenuContext&, int32_t value) {
	FlashStorage::defaultKeyboardLayout = static_cast<KeyboardLayoutType>(value); // `:12`
}

int32_t get_default_kb_velocity_glide(const MenuContext&) {
	return from_bool(FlashStorage::keyboardFunctionsVelocityGlide, /*invert=*/true); // `menus.cpp:1537`
}
void set_default_kb_velocity_glide(const MenuContext&, int32_t value) {
	FlashStorage::keyboardFunctionsVelocityGlide = value == 0;
}

int32_t get_default_kb_modwheel_glide(const MenuContext&) {
	return from_bool(FlashStorage::keyboardFunctionsModwheelGlide, /*invert=*/true); // `menus.cpp:1542`
}
void set_default_kb_modwheel_glide(const MenuContext&, int32_t value) {
	FlashStorage::keyboardFunctionsModwheelGlide = value == 0;
}

int32_t get_default_favourites_layout(const MenuContext&) {
	return static_cast<int32_t>(FlashStorage::defaultFavouritesLayout); // `defaults/favourites_layout.h:11`
}
void set_default_favourites_layout(const MenuContext&, int32_t value) {
	FlashStorage::defaultFavouritesLayout = static_cast<FavouritesDefaultLayout>(value); // `:12-14`
}

int32_t get_default_new_clip_type(const MenuContext&) {
	return static_cast<int32_t>(FlashStorage::defaultNewClipType); // `defaults/ui/clip_type/default_new_clip_type.h:12`
}
void set_default_new_clip_type(const MenuContext&, int32_t value) {
	FlashStorage::defaultNewClipType = static_cast<OutputType>(value); // `:13`
}

int32_t get_default_use_last_clip_type(const MenuContext&) {
	return from_bool(FlashStorage::defaultUseLastClipType); // `menus.cpp:1599`
}
void set_default_use_last_clip_type(const MenuContext&, int32_t value) {
	FlashStorage::defaultUseLastClipType = value != 0;
}

int32_t get_default_use_sharps(const MenuContext&) {
	return from_bool(FlashStorage::defaultUseSharps); // `menus.cpp:1608`
}
void set_default_use_sharps(const MenuContext&, int32_t value) {
	FlashStorage::defaultUseSharps = value != 0;
}

// ---------------------------------------------------------------------------------------------
// Defaults: automation

int32_t get_default_automation_interpolate(const MenuContext&) {
	return from_bool(FlashStorage::automationInterpolate); // `menus.cpp:1616`
}
void set_default_automation_interpolate(const MenuContext&, int32_t value) {
	FlashStorage::automationInterpolate = value != 0;
}
int32_t get_default_automation_clear(const MenuContext&) {
	return from_bool(FlashStorage::automationClear); // `menus.cpp:1620`
}
void set_default_automation_clear(const MenuContext&, int32_t value) {
	FlashStorage::automationClear = value != 0;
}
int32_t get_default_automation_shift(const MenuContext&) {
	return from_bool(FlashStorage::automationShift); // `menus.cpp:1623`
}
void set_default_automation_shift(const MenuContext&, int32_t value) {
	FlashStorage::automationShift = value != 0;
}
int32_t get_default_automation_nudge(const MenuContext&) {
	return from_bool(FlashStorage::automationNudgeNote); // `menus.cpp:1626`
}
void set_default_automation_nudge(const MenuContext&, int32_t value) {
	FlashStorage::automationNudgeNote = value != 0;
}
int32_t get_default_automation_no_audition(const MenuContext&) {
	return from_bool(FlashStorage::automationDisableAuditionPadShortcuts); // `menus.cpp:1630`
}
void set_default_automation_no_audition(const MenuContext&, int32_t value) {
	FlashStorage::automationDisableAuditionPadShortcuts = value != 0;
}

// ---------------------------------------------------------------------------------------------
// Defaults: the rest

int32_t get_default_swing_interval(const MenuContext&) {
	return FlashStorage::defaultSwingInterval; // `defaults/swing_interval.h:12`
}
void set_default_swing_interval(const MenuContext&, int32_t value) {
	FlashStorage::defaultSwingInterval = static_cast<uint8_t>(value); // `:13-18`
}

int32_t get_default_scale(const MenuContext&) {
	// `defaults::DefaultScale::readCurrentValue` (`defaults/scale.h:13-15`) with its own
	// `scaleToOptionIndex` (`:41-47`): USER_SCALE is not offerable as a default, so the option index
	// is the scale index with that one slot removed.
	const Scale scale = flashStorageCodeToScale(FlashStorage::defaultScale);
	return scale >= USER_SCALE ? static_cast<int32_t>(scale) - 1 : static_cast<int32_t>(scale);
}
void set_default_scale(const MenuContext&, int32_t value) {
	int32_t index = value; // `scale.h:17-19` over `optionIndexToScale` (`:35-40`)
	if (index >= USER_SCALE) {
		index += 1;
	}
	FlashStorage::defaultScale = scaleToFlashStorageCode(static_cast<Scale>(index));
}
size_t default_scale_names(const MenuContext&, std::span<std::string_view> out) {
	size_t n = 0; // `scale.h:23-32`
	for (uint8_t i = 0; i < NUM_SCALELIKE && n < out.size(); i++) {
		if (i != USER_SCALE) {
			out[n++] = scalelikeNames[i];
		}
	}
	return n;
}

int32_t get_default_velocity(const MenuContext&) {
	return FlashStorage::defaultVelocity; // `defaults/velocity.h:12`
}
void set_default_velocity(const MenuContext& ctx, int32_t value) {
	FlashStorage::defaultVelocity = static_cast<uint8_t>(value); // `defaults/velocity.h:13-16`
	if (ctx.song != nullptr) {
		ctx.song->setDefaultVelocityForAllInstruments(FlashStorage::defaultVelocity);
	}
}

int32_t get_default_magnitude(const MenuContext&) {
	return FlashStorage::defaultMagnitude; // `defaults/magnitude.h:12`
}
void set_default_magnitude(const MenuContext&, int32_t value) {
	// `defaults/magnitude.h:13-20`: the swing interval is nudged by the same delta so its musical
	// meaning survives the resolution change.
	const int32_t old_magnitude = FlashStorage::defaultMagnitude;
	FlashStorage::defaultMagnitude = static_cast<int8_t>(value);
	const int32_t delta = FlashStorage::defaultMagnitude - old_magnitude;
	FlashStorage::defaultSwingInterval = clampSwingIntervalSyncLevel(FlashStorage::defaultSwingInterval - delta);
}
size_t default_magnitude_names(const MenuContext&, std::span<std::string_view> out) {
	// `defaults::Magnitude` has no option *names*: it draws `96 << value`
	// (`defaults/magnitude.h:22-29`) over a `size()` of 7 (`:31`). The seven numbers are built here
	// so the node still has a list to page through.
	static char buffers[7][8];
	size_t n = 0;
	for (int32_t i = 0; i < 7 && n < out.size(); i++) {
		int32_t value = 96 << i;
		size_t len = 0;
		char digits[8];
		while (value > 0 && len < sizeof(digits)) {
			digits[len++] = static_cast<char>('0' + value % 10);
			value /= 10;
		}
		for (size_t d = 0; d < len; d++) {
			buffers[i][d] = digits[len - 1 - d];
		}
		out[n++] = std::string_view{buffers[i], len};
	}
	return n;
}

int32_t get_default_bend_range(const MenuContext&) {
	return FlashStorage::defaultBendRange[BEND_RANGE_MAIN]; // `defaults/bend_range.h:8`
}
void set_default_bend_range(const MenuContext&, int32_t value) {
	FlashStorage::defaultBendRange[BEND_RANGE_MAIN] = static_cast<uint8_t>(value); // `:9`
}

int32_t get_default_metronome_volume(const MenuContext&) {
	return FlashStorage::defaultMetronomeVolume; // `defaults/metronome_volume.h:12`
}
void set_default_metronome_volume(const MenuContext&, int32_t value) {
	FlashStorage::defaultMetronomeVolume = static_cast<uint8_t>(value); // `:13-16`
	AudioEngine::metronome.setVolume(value);
}

int32_t get_default_patch_cable_polarity(const MenuContext&) {
	return static_cast<int32_t>(FlashStorage::defaultPatchCablePolarity); // `defaults/patch_cable_polarity.h:11`
}
void set_default_patch_cable_polarity(const MenuContext&, int32_t value) {
	FlashStorage::defaultPatchCablePolarity = static_cast<Polarity>(value); // `:12`
}

int32_t get_default_startup_song(const MenuContext&) {
	return static_cast<int32_t>(FlashStorage::defaultStartupSongMode); // `defaults/startup_song_mode.h:11`
}
void set_default_startup_song(const MenuContext&, int32_t value) {
	FlashStorage::defaultStartupSongMode = static_cast<StartupSongMode>(value); // `:12`
}

int32_t get_default_pad_brightness(const MenuContext&) {
	return FlashStorage::defaultPadBrightness << 2; // `defaults/pad_brightness.h:24`, `to_ui`
}
void set_default_pad_brightness(const MenuContext&, int32_t value) {
	// `:25-29`. The `PadLEDs::setBrightnessLevel` apply is `hid/` -- see `settings.h`'s note.
	FlashStorage::defaultPadBrightness = static_cast<uint8_t>(value >> 2);
}

int32_t get_default_slice_mode(const MenuContext&) {
	return static_cast<int32_t>(FlashStorage::defaultSliceMode); // `defaults/slice_mode.h:11`
}
void set_default_slice_mode(const MenuContext&, int32_t value) {
	FlashStorage::defaultSliceMode = static_cast<SampleRepeatMode>(value); // `:12`
}

int32_t get_default_cpu_indicator(const MenuContext&) {
	return from_bool(FlashStorage::highCPUUsageIndicator); // `menus.cpp:1663`
}
void set_default_cpu_indicator(const MenuContext&, int32_t value) {
	FlashStorage::highCPUUsageIndicator = value != 0;
}

int32_t get_default_hold_time(const MenuContext&) {
	return FlashStorage::defaultHoldTime; // `defaults/hold_time.h:12`
}
void set_default_hold_time(const MenuContext&, int32_t value) {
	FlashStorage::defaultHoldTime = static_cast<uint8_t>(value); // `:13-16`
	FlashStorage::holdTime = (FlashStorage::defaultHoldTime * kSampleRate) / 20;
}

int32_t get_screensaver_mode(const MenuContext&) {
	return static_cast<int32_t>(FlashStorage::screensaverMode); // `defaults/screensaver.h:24`
}
void set_screensaver_mode(const MenuContext&, int32_t value) {
	// `:25-28`. `Screensaver::settingsChanged()` is `hid/` -- see `settings.h`'s note.
	FlashStorage::screensaverMode = static_cast<ScreensaverMode>(value);
}

int32_t get_screensaver_timeout(const MenuContext&) {
	return FlashStorage::screensaverTimeoutMinutes; // `defaults/screensaver.h:55`
}
void set_screensaver_timeout(const MenuContext&, int32_t value) {
	FlashStorage::screensaverTimeoutMinutes = static_cast<uint8_t>(value); // `:56-59`
}

// ---------------------------------------------------------------------------------------------
// Pads

int32_t get_pads_keyboard_layout(const MenuContext&) {
	return static_cast<int32_t>(FlashStorage::keyboardLayout); // `keyboard/layout.h:11`
}
void set_pads_keyboard_layout(const MenuContext&, int32_t value) {
	FlashStorage::keyboardLayout = static_cast<KeyboardLayout>(value); // `:12`
}
size_t pads_keyboard_layout_names(const MenuContext&, std::span<std::string_view> out) {
	static constexpr std::string_view kNames[] = {"QWERTY", "AZERTY", "QWERTZ"}; // `keyboard/layout.h:13-20`
	size_t n = 0;
	for (std::string_view name : kNames) {
		if (n < out.size()) {
			out[n++] = name;
		}
	}
	return n;
}

int32_t get_sample_browser_preview(const MenuContext&) {
	return FlashStorage::sampleBrowserPreviewMode; // `sample/browser_preview/mode.h:11`
}
void set_sample_browser_preview(const MenuContext&, int32_t value) {
	FlashStorage::sampleBrowserPreviewMode = static_cast<uint8_t>(value); // `:12`
}

// ---------------------------------------------------------------------------------------------
// Recording

int32_t get_record_count_in(const MenuContext&) {
	return playbackHandler.countInBars; // `record/countin.h:11`
}
void set_record_count_in(const MenuContext&, int32_t value) {
	playbackHandler.countInBars = static_cast<uint8_t>(value); // `:12`
}

int32_t get_record_quantize(const MenuContext&) {
	return FlashStorage::recordQuantizeLevel; // `record/quantize.h:24`
}
void set_record_quantize(const MenuContext&, int32_t value) {
	FlashStorage::recordQuantizeLevel = static_cast<uint8_t>(value); // `:25`
}

int32_t get_record_margins(const MenuContext&) {
	return from_bool(FlashStorage::audioClipRecordMargins); // `menus.cpp:1268`
}
void set_record_margins(const MenuContext&, int32_t value) {
	FlashStorage::audioClipRecordMargins = value != 0;
}

int32_t get_monitor_mode(const MenuContext&) {
	return static_cast<int32_t>(AudioEngine::inputMonitoringMode); // `monitor/mode.h:12`
}
void set_monitor_mode(const MenuContext&, int32_t value) {
	AudioEngine::inputMonitoringMode = static_cast<InputMonitoringMode>(value); // `:13`
}

int32_t get_default_threshold_recording(const MenuContext&) {
	return static_cast<int32_t>(FlashStorage::defaultThresholdRecordingMode); // `record/threshold_mode.h:27-28`
}
void set_default_threshold_recording(const MenuContext& ctx, int32_t value) {
	// `threshold_mode.h:36-43`: the DEFAULT kind writes the flash setting **and then**, if there is
	// a song, the song's own mode too -- both arms run, which is why this is not an either/or.
	FlashStorage::defaultThresholdRecordingMode = static_cast<ThresholdRecordingMode>(value);
	if (ctx.song != nullptr) {
		ctx.song->thresholdRecordingMode = static_cast<ThresholdRecordingMode>(value);
	}
}

int32_t get_loop_command(const MenuContext&) {
	// `record::LoopCommand::readCurrentValue` (`record/loop_command.h:21-26`): the stored global MIDI
	// command becomes one of two overdub types.
	return FlashStorage::defaultLoopRecordingCommand == GlobalMIDICommand::LOOP
	           ? static_cast<int32_t>(OverDubType::Normal)
	           : static_cast<int32_t>(OverDubType::ContinuousLayering);
}
void set_loop_command(const MenuContext&, int32_t value) {
	FlashStorage::defaultLoopRecordingCommand = static_cast<OverDubType>(value) == OverDubType::Normal
	                                                ? GlobalMIDICommand::LOOP
	                                                : GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING; // `:27-31`
}

// ---------------------------------------------------------------------------------------------
// Community features

int32_t runtime_feature_get(int32_t type) {
	// `Setting::readCurrentValue` (`runtime_feature/setting.cpp:22-33`) scans the option list for the
	// stored value and reports its index; every option list here is `{0, 1}` in index order
	// (`runtime_feature_settings.cpp:41-77`), so the index IS the value.
	return static_cast<int32_t>(runtimeFeatureSettings.get(static_cast<RuntimeFeatureSettingType>(type)));
}
void runtime_feature_set(int32_t type, int32_t value) {
	runtimeFeatureSettings.set(static_cast<RuntimeFeatureSettingType>(type),
	                           static_cast<uint32_t>(value)); // `setting.cpp:35-38`
}

void set_shift_is_sticky(const MenuContext&, int32_t value) {
	// `ShiftIsSticky::writeCurrentValue` (`runtime_feature/shift_is_sticky.cpp:20-33`), minus its
	// `Buttons::clearShiftSticky()` arm (`hid/`; see `settings.h`'s note). The LightShiftLed poke is
	// model state and is transcribed.
	runtime_feature_set(RuntimeFeatureSettingType::ShiftIsSticky, value);
	if (value != 0) {
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::LightShiftLed, RuntimeFeatureStateToggle::On);
	}
}

size_t new_clip_type_names(const MenuContext&, std::span<std::string_view> out) {
	// `DefaultNewClipType::getOptions` (`defaults/ui/clip_type/default_new_clip_type.h:19-25`).
	static constexpr std::string_view kNames[] = {"SYNTH", "KIT", "MIDI", "CV", "AUDIO"};
	size_t n = 0;
	for (std::string_view name : kNames) {
		if (n < out.size()) {
			out[n++] = name;
		}
	}
	return n;
}

size_t polarity_names(const MenuContext&, std::span<std::string_view> out) {
	// `PatchCablePolarity::getOptions` (`defaults/patch_cable_polarity.h:19-22`).
	size_t n = 0;
	if (n < out.size()) {
		out[n++] = polarityToString(Polarity::UNIPOLAR);
	}
	if (n < out.size()) {
		out[n++] = polarityToString(Polarity::BIPOLAR);
	}
	return n;
}

size_t sync_scaling_action_names(const MenuContext&, std::span<std::string_view> out) {
	// `SetupSyncScalingActionSetting` (`runtime_feature_settings.cpp:59-77`): two literal option
	// names, not l10n ids -- exactly as legacy stores them.
	static constexpr std::string_view kNames[] = {"Sync Scaling", "Fill mode"};
	size_t n = 0;
	for (std::string_view name : kNames) {
		if (n < out.size()) {
			out[n++] = name;
		}
	}
	return n;
}

// ---------------------------------------------------------------------------------------------
// 12-13. Battery and firmware (`Kind::Info`, E2 task 2)

std::string_view get_battery_info(const MenuContext&, InfoScratch& scratch) {
	// `battery::Level::getBatteryString` (`gui/menu_item/battery/level.h:81-107`), minus the
	// charging suffix -- see this node's own declaration comment in `settings.h` for why. 2600 mV /
	// 4200 mV are legacy's own empty/full endpoints (`level.h:84-85`).
	constexpr int32_t kMinMv = 2600;
	constexpr int32_t kMaxMv = 4200;
	int32_t percentage = 0;
	if (::batteryMV > kMinMv) {
		percentage = (static_cast<int32_t>(::batteryMV) - kMinMv) * 100 / (kMaxMv - kMinMv);
		percentage = std::clamp(percentage, 0, 100);
	}

	char* out = scratch.chars;
	char* const end = scratch.chars + sizeof(scratch.chars);
	const auto append = [&](std::string_view s) {
		const size_t n = std::min(s.size(), static_cast<size_t>(end - out));
		std::copy_n(s.data(), n, out);
		out += n;
	};
	const auto append_int = [&](int32_t v) {
		const std::to_chars_result r = std::to_chars(out, end, v);
		out = r.ptr;
	};

	append_int(percentage);
	append("% (");
	append_int(static_cast<int32_t>(::batteryMV));
	append("mV)");
	if (percentage >= 99) {
		append(" FULL"); // the one stateless half of legacy's status suffix -- see the doc comment
	}
	return std::string_view{scratch.chars, static_cast<size_t>(out - scratch.chars)};
}

std::string_view get_firmware_version_info(const MenuContext&, InfoScratch&) {
	// `firmware::Version::drawPixelsForOled` (`gui/menu_item/firmware/version.h:29-32`): the build's
	// own version string, no formatting and no scratch buffer needed.
	return kFirmwareVersionString;
}

// ---------------------------------------------------------------------------------------------

namespace {
constexpr const Node* kRoots[] = {&root_settings};
} // namespace

const std::span<const Node* const> kSettingsRoots{kRoots};

} // namespace deluge::scenes::menu::nodes
