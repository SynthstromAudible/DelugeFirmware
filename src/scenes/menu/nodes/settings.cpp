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

#include "scenes/context_menu/context_menu_scene.h" // ResetItem::on_focus opens this
#include "scenes/l10n_bridge.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h" // MenuState, kMenuDepth -- for the CustomItems below
#include "scenes/profile.h"         // deluge_profile() -- ditto

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
#include "processing/engines/cv_engine.h"   // cvEngine -- the CV/gate output pages
#include "processing/metronome/metronome.h" // AudioEngine::metronome's definition
#include "storage/flash_storage.h"

#include "shell/scene.h"     // UiEvent/UiContext/Result -- CustomItem::handle's signature
#include "ui/core/metrics.h" // kReadoutValueY
#include "ui/oled/frame.h"
#include "ui/oled/text.h"

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
// 2-3b. The CV and gate output pages (`cvSubmenu`, `gateModeMenu`, `gateOffTimeMenu`) -- the three
// pages legacy reaches only through a chain, see `settings.h`'s note.
//
// `MenuContext::source_index` stands in for `soundEditor.currentSourceIndex` throughout, and is
// clamped at every use: legacy indexes the two `cvEngine` arrays with it unchecked because its own
// selectors are the only writers, whereas here it arrives on a context resolved elsewhere.

namespace {
/// @brief @p ctx's output index, clamped into `[0, count)`.
size_t output_index(const MenuContext& ctx, size_t count) {
	const auto raw = static_cast<int32_t>(ctx.source_index);
	return raw < 0 ? 0 : static_cast<size_t>(std::min<int32_t>(raw, static_cast<int32_t>(count) - 1));
}
} // namespace

int32_t get_cv_volts(const MenuContext& ctx) {
	// `cv::Volts::readCurrentValue` (`cv/volts.h:36-38`).
	return cvEngine.cvChannels[output_index(ctx, NUM_PHYSICAL_CV_CHANNELS)].voltsPerOctave;
}
void set_cv_volts(const MenuContext& ctx, int32_t value) {
	// `cv::Volts::writeCurrentValue` (`:39-41`).
	cvEngine.setCVVoltsPerOctave(static_cast<uint8_t>(output_index(ctx, NUM_PHYSICAL_CV_CHANNELS)),
	                             static_cast<uint8_t>(value));
}

int32_t get_cv_transpose(const MenuContext& ctx) {
	// `cv::Transpose::readCurrentValue` (`cv/transpose.h:34-37`), over
	// `computeCurrentValueForTranspose` (`gui/menu_item/value_scaling.cpp:64-66`) -- the same
	// packing `sound_osc.cpp` and `roots.cpp` transcribe for the other transposes.
	const CVChannel& channel = cvEngine.cvChannels[output_index(ctx, NUM_PHYSICAL_CV_CHANNELS)];
	return channel.transpose * 100 + channel.cents;
}
void set_cv_transpose(const MenuContext& ctx, int32_t value) {
	// `computeFinalValuesForTranspose` (`value_scaling.cpp:68-73`) then `transpose.h:39-43`.
	const int32_t current = value + 25600;
	const int32_t semitones = (current + 50) / 100;
	cvEngine.setCVTranspose(static_cast<uint8_t>(output_index(ctx, NUM_PHYSICAL_CV_CHANNELS)), semitones - 256,
	                        current - semitones * 100);
}

size_t gate_mode_names(const MenuContext& ctx, std::span<std::string_view> out) {
	// `gate::Mode`'s `options_` (`gate/mode.h:30-33`) as `updateOptions(whichOutput)` leaves it
	// (`:53-72`): the two trigger polarities always, plus one extra on the outputs that carry the
	// clock or the run signal.
	size_t n = 0;
	for (const String s : {String::STRING_FOR_V_TRIGGER, String::STRING_FOR_S_TRIGGER}) {
		if (n < out.size()) {
			out[n++] = deluge::scenes::text_lookup(sid(s));
		}
	}
	if (n < out.size()) {
		switch (static_cast<int32_t>(output_index(ctx, NUM_GATE_CHANNELS))) {
		case WHICH_GATE_OUTPUT_IS_CLOCK:
			out[n++] = deluge::scenes::text_lookup(sid(String::STRING_FOR_CLOCK));
			break;
		case WHICH_GATE_OUTPUT_IS_RUN:
			out[n++] = deluge::scenes::text_lookup(sid(String::STRING_FOR_RUN_SIGNAL));
			break;
		default:
			break;
		}
	}
	return n;
}
int32_t get_gate_mode(const MenuContext& ctx) {
	// `gate::Mode::readCurrentValue` (`gate/mode.h:37`).
	return static_cast<int32_t>(cvEngine.gateChannels[output_index(ctx, NUM_GATE_CHANNELS)].mode);
}
void set_gate_mode(const MenuContext& ctx, int32_t value) {
	// `gate::Mode::writeCurrentValue` (`:38-40`).
	cvEngine.setGateType(static_cast<uint8_t>(output_index(ctx, NUM_GATE_CHANNELS)), static_cast<GateType>(value));
}

int32_t get_gate_off_time(const MenuContext&) {
	return cvEngine.minGateOffTime; // `gate::OffTime::readCurrentValue` (`gate/off_time.h:28`)
}
void set_gate_off_time(const MenuContext&, int32_t value) {
	cvEngine.minGateOffTime = static_cast<uint8_t>(value); // `:29`
}

const Node* gate_selection_chain(const MenuContext& ctx, const MenuState&) {
	// `gate::Selection::selectButtonPress` (`gate/selection.h:43-54`): the option one past the last
	// physical output is the minimum-off-time page; every other option is that output's mode page.
	// Unclamped on purpose -- the equality is against the exact sentinel legacy tests, and
	// `output_index` clamps at the point of use inside the mode hooks.
	return ctx.source_index == NUM_GATE_CHANNELS ? &gate_off_time : &gate_mode;
}

// ---------------------------------------------------------------------------------------------
// 12-13. Battery and firmware (`CustomItem`s, E2 task 2 fix round 1 -- see `settings.h`'s note on
// why these are not a shared `Kind`)

namespace {
/// @brief The node currently on screen, so a `CustomItem` -- which is handed no `Node` of its own,
///        only `MenuState` -- can still draw its own title. Mirrors `PendingCustom::render`
///        (`nodes/sound_osc.cpp:52`).
const Node* focused_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
}

void draw_title_for(deluge::ui::oled::OledFrame& f, const MenuState& state) {
	const Node* node = focused_of(state);
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                           : std::string_view{});
}

/// @brief Draw @p text centred at the shared battery/firmware baseline (`kReadoutValueY`), in the
///        huge font -- `firmware::Version`/`battery::Level::drawPixelsForOled`'s shared
///        `drawStringCentredShrinkIfNecessary(str, 22, 18, 20)` (`ui/core/metrics.h`'s
///        `kReadoutValueY` doc comment has the full derivation).
void draw_readout(deluge::ui::oled::OledFrame& f, std::string_view text) {
	const deluge::gfx::oled::Rect band{f.bounds().x, f.bounds().y + deluge::ui::kReadoutValueY, f.bounds().w,
	                                   deluge::ui::oled::text_height_for(f.style.big_font)};
	deluge::ui::oled::text(f, band, text,
	                       {.font = f.style.big_font, .align = deluge::ui::oled::Align::Centre, .shrink = true});
}
} // namespace

void FirmwareVersionItem::render(deluge::ui::oled::OledFrame& f, const MenuContext&, MenuState& state) const {
	draw_title_for(f, state);
	// `firmware::Version::drawPixelsForOled` (`gui/menu_item/firmware/version.h:29-32`): the build's
	// own version string, a build-time constant with nothing to format.
	draw_readout(f, kFirmwareVersionString);
	// The select press leaves the item, same as every other leaf's arm (`menu_scene.cpp`'s
	// `render_leaf_sync_level`, for one) -- `Kind::Custom`'s own dispatch draws nothing else, so
	// this item claims it itself.
	if (f.select_pressed()) {
		state.render_resp.entered = true;
	}
}

deluge::ui::shell::Result FirmwareVersionItem::handle(const deluge::ui::shell::UiEvent&,
                                                      const deluge::ui::shell::UiContext&, const MenuContext&,
                                                      MenuState&) const {
	return {}; // `firmware::Version` has no `buttonAction` override; nothing to consume here either
}

void BatteryLevelItem::render(deluge::ui::oled::OledFrame& f, const MenuContext&, MenuState& state) const {
	draw_title_for(f, state);

	// Charging heuristic, ported from `battery::Level::timerCallback`
	// (`gui/menu_item/battery/level.h:53-68`): every 4 timer ticks at 500 ms (= 2000 ms), check
	// whether the voltage rose by more than 5 mV since the last check. This port has no separate
	// timer callback to count ticks on -- only a clock reading each time `render()` runs -- so the
	// same 2000 ms window is measured directly off `now_ms()` instead.
	constexpr uint32_t kChargeCheckMs = 2000;
	const uint32_t now = f.now_ms();
	if (!checked_once_) {
		last_check_mv_ = ::batteryMV;
		last_check_ms_ = now;
		checked_once_ = true;
	}
	else if (now - last_check_ms_ >= kChargeCheckMs) {
		const int32_t diff = static_cast<int32_t>(::batteryMV) - static_cast<int32_t>(last_check_mv_);
		charging_ = diff > 5; // `level.h:59-60`'s own threshold
		last_check_mv_ = ::batteryMV;
		last_check_ms_ = now;
	}

	// `battery::Level::getBatteryString` (`gui/menu_item/battery/level.h:81-107`). 2600 mV / 4200 mV
	// are legacy's own empty/full endpoints (`level.h:84-85`).
	constexpr int32_t kMinMv = 2600;
	constexpr int32_t kMaxMv = 4200;
	int32_t percentage = 0;
	if (::batteryMV > kMinMv) {
		percentage = (static_cast<int32_t>(::batteryMV) - kMinMv) * 100 / (kMaxMv - kMinMv);
		percentage = std::clamp(percentage, 0, 100);
	}

	char buf[48];
	char* out = buf;
	char* const end = buf + sizeof(buf);
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
	// `level.h:97-102`: FULL takes precedence over CHG -- a battery reading 99%+ is reported full
	// even if the charging heuristic above still thinks it is rising.
	if (percentage >= 99) {
		append(" FULL");
	}
	else if (charging_) {
		append(" CHG");
	}

	draw_readout(f, std::string_view{buf, static_cast<size_t>(out - buf)});

	// `battery::Level::beginSession`/`timerCallback` re-render every 500 ms for as long as the item
	// is on screen (`battery/level.h:50,65-67`); this port's equivalent asks the frame's own repaint
	// gate to wake it up on the same cadence -- the same shape every other animated widget here uses
	// (`menu_scene.cpp`'s `render_grid`, on the shortcut blink).
	f.gate().request_repaint_after(500, now);

	if (f.select_pressed()) {
		state.render_resp.entered = true;
	}
}

deluge::ui::shell::Result BatteryLevelItem::handle(const deluge::ui::shell::UiEvent&,
                                                   const deluge::ui::shell::UiContext&, const MenuContext&,
                                                   MenuState&) const {
	return {}; // `battery::Level` has no `buttonAction` override either
}

void BatteryLevelItem::on_focus(const MenuContext&, MenuState&) const {
	// `battery::Level::beginSession` (`gui/menu_item/battery/level.h:43-51`): reset the baseline
	// every time the item becomes focused, not just once for the whole process -- otherwise
	// leaving the screen and coming back takes the `now - last_check_ms_ >= kChargeCheckMs` branch
	// in `render()` immediately against a stale baseline from the previous visit, computing a
	// delta over an arbitrary real-world gap (a spurious `CHG`, or a missed real one).
	//
	// `checked_once_ = false` is the whole reset: `on_focus` has no `OledFrame` and so no `now_ms()`
	// to stamp a fresh baseline with; the next `render()` call (the first of this new visit) takes
	// the `!checked_once_` branch and captures `last_check_mv_`/`last_check_ms_` off *that* call's
	// clock instead, deferring the next real check a full `kChargeCheckMs` from the moment the item
	// actually became visible again -- exactly what legacy's `voltageCheckCounter = 0` guarantees.
	//
	// `charging_` is reset too, unlike legacy's `isCharging` (never touched in `beginSession`,
	// because legacy's `timerCallback` recomputes it before the ~2 s window is up regardless of
	// visits). Showing a stale charging indicator left over from a previous, unrelated visit would
	// be actively misleading on the very first render of this one; nothing here is charging until
	// the reset baseline says so.
	checked_once_ = false;
	charging_ = false;
}

// ---------------------------------------------------------------------------------------------
// 1. Actions (`ResetItem`, see `settings.h`)

// `settings.h` constructs the five `ResetItem`s from raw `uint16_t` literals because including
// `context_menu_scene.h` there would pull the scene into every consumer of this header. Those
// literals are only correct while `DescriptorId` keeps its current numbering, so the numbering is
// pinned here, in the one translation unit that already sees both. Reorder the enum and this
// fires, instead of five menu items silently resetting the wrong subsystem.
namespace {
namespace cm = deluge::scenes::context_menu;
static_assert(static_cast<uint16_t>(cm::DescriptorId::ResetFlash) == 0);
static_assert(static_cast<uint16_t>(cm::DescriptorId::ResetCommunityFeatures) == 1);
static_assert(static_cast<uint16_t>(cm::DescriptorId::ResetMidiFollow) == 2);
static_assert(static_cast<uint16_t>(cm::DescriptorId::ResetMidiDevices) == 3);
static_assert(static_cast<uint16_t>(cm::DescriptorId::ResetAll) == 4);
} // namespace

void ResetItem::render(deluge::ui::oled::OledFrame& f, const MenuContext&, MenuState& state) const {
	// Drawn for at most the one tick between `on_focus` asking for the push and the router
	// applying it -- `EditNameItem::render`'s own reasoning (`nodes/edit_name.cpp`). Legacy never
	// draws this item at all: `Reset::selectButtonPress` opens the context menu directly and
	// returns `NO_NAVIGATION` before `SoundEditor` gets to render anything.
	draw_title_for(f, state);
}

deluge::ui::shell::Result ResetItem::handle(const deluge::ui::shell::UiEvent&, const deluge::ui::shell::UiContext&,
                                            const MenuContext&, MenuState&) const {
	// The context menu owns every input while it is up, and this item is only ever focused for the
	// tick before it appears. Nothing to consume.
	return {};
}

void ResetItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `Reset::selectButtonPress` (`gui/menu_item/reset_settings/reset.h:29-33`):
	// `context_menu::resetSettings.setAction(action_)` then `openUI(&context_menu::resetSettings)`.
	// `open()` points the ported context menu at this visit's `MenuContext` (unused by any of the
	// five accept hooks, but required by `on_exit`'s fail-closed contract -- `context_menu_scene.h`);
	// `descriptor_id_` is what used to be the `ResetSettingsAction` argument to `setAction`.
	deluge::scenes::context_menu::context_menu_scene().open(ctx);
	state.pending_scene =
	    deluge::ui::shell::SceneId{deluge::scenes::context_menu::kContextMenuSceneKind, descriptor_id_};
}

// ---------------------------------------------------------------------------------------------

namespace {
constexpr const Node* kRoots[] = {&root_settings};
} // namespace

const std::span<const Node* const> kSettingsRoots{kRoots};

} // namespace deluge::scenes::menu::nodes
