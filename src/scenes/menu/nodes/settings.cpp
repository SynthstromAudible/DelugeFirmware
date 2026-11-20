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
///        in `nodes/settings.h`, which also lists what stayed behind and why.
///
/// Every hook is a transcription of the legacy `readCurrentValue`/`writeCurrentValue` cited above
/// it. None of them reads `MenuContext` except the two that edit the song (`defaults::Velocity`'s
/// tail): a setting is device-wide, which is exactly why the settings root's `MenuContext` has a
/// null `params` and a null `sound`.

#include "scenes/menu/nodes/settings.h"

#include "scenes/l10n_bridge.h"
#include "scenes/menu/bind_host.h" // the settings setters' host calls
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h" // MenuState, kMenuDepth -- for the CustomItems below
#include "scenes/menu/shortcuts.h"  // `shortcuts_version` selects a layer here
#include "scenes/profile.h"         // deluge_profile() -- ditto
#include "scenes/registry.h"        // menu_scene()

#include "definitions_cxx.hpp" // the flash-setting enums, MIDI_CHANNEL_NONE, NUM_CHANNELS, IS_A_CC, BEND_RANGE_MAIN, kSampleRate, OutputType, ThresholdRecordingMode, ScreensaverMode
#include "deluge-project/midi_channels.h"
#include "gfx/oled/glyphs.h"
#include "io/midi/device_registry.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_transpose.h"
#include "io/usb/usb_audio.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "music-theory/note_names.h"
#include "music-theory/preset_scales.h"
#include "notify/notice_queue.h" // the USB enable's "restart to apply"
#include "playback/playback_handler.h"
#include "plugin-api/types/display_mapping.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"   // cvEngine -- the CV/gate output pages
#include "processing/metronome/metronome.h" // AudioEngine::metronome's definition
#include "storage/flash_storage.h"

#include "shell/scene.h"                  // UiEvent/UiContext/Result -- CustomItem::handle's signature
#include "ui/core/metrics.h"              // kReadoutValueY
#include "ui/oled/detail/int_to_string.h" // format_range_integer -- the port of intToString
#include "ui/oled/frame.h"
#include "ui/oled/scroll_text.h"
#include "ui/oled/text.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>

namespace deluge::scenes::menu::nodes {

namespace {

/// @brief Read a plain `bool` global; @p invert is the `InvertedToggleBool` spelling
///        (`origin/main:src/deluge/gui/menu_item/toggle.h`'s two pairs).
constexpr int32_t from_bool(bool value, bool invert = false) {
	return (invert ? !value : value) ? 1 : 0;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Trigger clock

int32_t get_trigger_in_ppqn(const MenuContext&) {
	// `trigger::in::PPQN::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/trigger/in/ppqn.h`)
	return static_cast<int32_t>(playbackHandler.analogInTicksPPQN);
}
void set_trigger_in_ppqn(const MenuContext& ctx, int32_t value) {
	// `trigger::in::PPQN::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/trigger/in/ppqn.h`)
	playbackHandler.analogInTicksPPQN = value;
	if (playbackHandler.isExternalClockActive() && playbackHandler.usingAnalogClockInput) {
		playbackHandler.resyncInternalTicksToInputTicks(ctx.song);
	}
}

int32_t get_trigger_in_auto_start(const MenuContext&) {
	// `triggerInAutoStartMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`)
	return from_bool(playbackHandler.analogClockInputAutoStart);
}
void set_trigger_in_auto_start(const MenuContext&, int32_t value) {
	playbackHandler.analogClockInputAutoStart = value != 0;
}

int32_t get_trigger_out_ppqn(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/trigger/out/ppqn.h` `readCurrentValue`
	return static_cast<int32_t>(playbackHandler.analogOutTicksPPQN);
}
void set_trigger_out_ppqn(const MenuContext&, int32_t value) {
	// `trigger::out::PPQN::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/trigger/out/ppqn.h`)
	playbackHandler.analogOutTicksPPQN = value;
	playbackHandler.resyncAnalogOutTicksToInternalTicks();
}

// ---------------------------------------------------------------------------------------------
// MIDI clock

int32_t get_midi_clock_out(const MenuContext&) {
	// `send_clock_menu` (`origin/main:src/deluge/gui/ui/menus.cpp`), a `ToggleBool` over it
	return from_bool(playbackHandler.midiOutClockEnabled);
}
void set_midi_clock_out(const MenuContext&, int32_t value) {
	// `SendClock::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/send_clock.h`)
	playbackHandler.setMidiOutClockMode(value != 0);
}

int32_t get_midi_clock_in(const MenuContext&) {
	// `receive_clock_menu` (`origin/main:src/deluge/gui/ui/menus.cpp`)
	return from_bool(playbackHandler.midiInClockEnabled);
}
void set_midi_clock_in(const MenuContext&, int32_t value) {
	// `ReceiveClock::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/receive_clock.h`)
	playbackHandler.setMidiInClockEnabled(value != 0);
}

int32_t get_tempo_magnitude_matching(const MenuContext&) {
	// `tempoMagnitudeMatchingMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`)
	return from_bool(playbackHandler.tempoMagnitudeMatchingEnabled);
}
void set_tempo_magnitude_matching(const MenuContext&, int32_t value) {
	playbackHandler.tempoMagnitudeMatchingEnabled = value != 0;
}

// ---------------------------------------------------------------------------------------------
// MIDI follow

int32_t follow_channel_get(int32_t which) {
	// `FollowChannel::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/follow/follow_channel.h`)
	return midiEngine.midiFollowChannelType[which].channelOrZone;
}
void follow_channel_set(int32_t which, int32_t value) {
	// `FollowChannel::selectEncoderAction`'s unassigned arm forgets the learned input, device and all; any other value
	// is `writeCurrentValue`.
	if (value == MIDI_CHANNEL_NONE) {
		midiEngine.midiFollowChannelType[which].clear();
		return;
	}
	midiEngine.midiFollowChannelType[which].channelOrZone = value;
}

bool follow_channel_learn(int32_t which, const deluge::ui::shell::UiEvent& event) {
	using Kind = deluge::ui::shell::UiEvent::Kind;
	::MIDICable* const cable = deluge::midi::cable_of(event.device);
	if ((event.kind != Kind::MidiNoteOn && event.kind != Kind::MidiCc) || cable == nullptr) {
		return false;
	}
	LearnedMIDI& input = midiEngine.midiFollowChannelType[which];
	input.cable = cable;
	input.channelOrZone = event.channel;
	return true;
}

bool follow_channel_unlearn(int32_t which) {
	midiEngine.midiFollowChannelType[which].clear();
	return true;
}

std::optional<int32_t> turn_follow_channel(const MenuContext&, int32_t value, int32_t offset) {
	if (value == MIDI_CHANNEL_NONE) {
		if (offset > 0) {
			return 0;
		}
		if (offset < 0) {
			return project::kMidiChannelUpperZone;
		}
		return value;
	}
	const int32_t next = value + offset;
	return next >= NUM_CHANNELS || next < 0 ? MIDI_CHANNEL_NONE : next;
}

void follow_channel_draw(int32_t which, const MenuContext& ctx, deluge::ui::oled::OledFrame& f, int32_t value,
                         deluge::ui::ScrollText& scroll) {
	using deluge::l10n::String;
	// Legacy's canvas rows 20 / 29 / 38 at the Apple face's height 7, laid out as `MidiCommandItem` lays the same rows
	// (`ui/core/metrics.h`).
	const bool tall = f.style.menu_base_y == deluge::ui::kMenuBaseY64;
	const int32_t row0_y =
	    (tall ? deluge::ui::kMidiCommandRowY64 : deluge::ui::kMidiCommandRowY48) + deluge::ui::kApple7Height7YAdjust;
	const int32_t row_h = f.style.row_height;
	const auto row = [&](int32_t i, int32_t x) {
		return deluge::gfx::oled::Rect{x, row0_y + i * row_h, f.bounds().w - x, row_h};
	};
	const deluge::ui::oled::TextStyle ts{.font = f.style.row_font};

	deluge::ui::oled::text(f, row(0, 0),
	                       deluge::scenes::text_lookup(sid(MIDIDeviceManager::differentiatingInputsByDevice
	                                                           ? String::STRING_FOR_INPUT_DIFFERENTIATION_ON
	                                                           : String::STRING_FOR_INPUT_DIFFERENTIATION_OFF)),
	                       ts);

	const LearnedMIDI& input = midiEngine.midiFollowChannelType[which];
	// A learned cable with no MIDI devices port to name it (a spec's fakes) shows no name.
	std::string_view device = deluge::scenes::text_lookup(sid(String::STRING_FOR_FOLLOW_DEVICE_UNASSIGNED));
	if (input.cable != nullptr) {
		device =
		    ctx.ports.midi != nullptr ? ctx.ports.midi->name(deluge::midi::id_of(*input.cable)) : std::string_view{};
	}
	deluge::ui::oled::scroll_text(f, row(1, 0), device, f.style.row_font, scroll);

	std::string_view channel_text;
	if (value == project::kMidiChannelLowerZone) {
		channel_text = deluge::scenes::text_lookup(sid(String::STRING_FOR_MPE_LOWER_ZONE));
	}
	else if (value == project::kMidiChannelUpperZone) {
		channel_text = deluge::scenes::text_lookup(sid(String::STRING_FOR_MPE_UPPER_ZONE));
	}
	else if (value == MIDI_CHANNEL_NONE) {
		channel_text = deluge::scenes::text_lookup(sid(String::STRING_FOR_FOLLOW_CHANNEL_UNASSIGNED));
	}
	else {
		channel_text = deluge::scenes::text_lookup(sid(String::STRING_FOR_CHANNEL));
		char buf[12];
		const int32_t shown = value + 1 - (value >= IS_A_CC ? IS_A_CC : 0);
		const std::to_chars_result r = std::to_chars(buf, buf + sizeof(buf), shown);
		deluge::ui::oled::text(f, row(2, deluge::ui::oled::spacing_x_for(f.style.row_font) * 8),
		                       std::string_view{buf, static_cast<size_t>(r.ptr - buf)}, ts);
	}
	deluge::ui::oled::text(f, row(2, 0), channel_text, ts);
}

bool learn_follow_kit_root_note(const MenuContext&, const deluge::ui::shell::UiEvent& event) {
	if (event.kind != deluge::ui::shell::UiEvent::Kind::MidiNoteOn) {
		return false;
	}
	midiEngine.midiFollowKitRootNote = event.data1;
	return true;
}

int32_t get_follow_kit_root_note(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/midi/follow/follow_kit_root_note.h` `readCurrentValue`
	return midiEngine.midiFollowKitRootNote;
}
void set_follow_kit_root_note(const MenuContext&, int32_t value) {
	// `origin/main:src/deluge/gui/menu_item/midi/follow/follow_kit_root_note.h` `writeCurrentValue`
	midiEngine.midiFollowKitRootNote = static_cast<uint8_t>(value);
}

int32_t get_follow_feedback_channel(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/midi/follow/follow_feedback_channel_type.h` `readCurrentValue`
	return static_cast<int32_t>(midiEngine.midiFollowFeedbackChannelType);
}
void set_follow_feedback_channel(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/follow/follow_feedback_channel_type.h`).
	midiEngine.midiFollowFeedbackChannelType = static_cast<MIDIFollowFeedbackChannelType>(value);
}

size_t follow_feedback_channel_names(const MenuContext&, std::span<std::string_view> out) {
	// `FollowFeedbackChannelType::getOptions`
	// (`origin/main:src/deluge/gui/menu_item/midi/follow/follow_feedback_channel_type.h`): four l10n strings, then the
	// Track label with its "**" index placeholder trimmed, then that label joined to each of A/B/C. The three joined
	// labels are built into this translation unit's own buffers, exactly as legacy builds them into the menu item's
	// (`FollowFeedbackChannelType::getTrackAndChannelOption`).
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
	// `origin/main:src/deluge/gui/menu_item/midi/follow/follow_feedback_automation.h` `readCurrentValue`
	return static_cast<int32_t>(midiEngine.midiFollowFeedbackAutomation);
}
void set_follow_feedback_automation(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/follow/follow_feedback_automation.h`).
	midiEngine.midiFollowFeedbackAutomation = static_cast<MIDIFollowFeedbackAutomationMode>(value);
}

int32_t get_follow_feedback_filter(const MenuContext&) {
	// `midiFollowFeedbackFilterMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`)
	return from_bool(midiEngine.midiFollowFeedbackFilter);
}
void set_follow_feedback_filter(const MenuContext&, int32_t value) {
	midiEngine.midiFollowFeedbackFilter = value != 0;
}

int32_t get_follow_display_param(const MenuContext&) {
	// `midiFollowDisplayParamMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`)
	return from_bool(midiEngine.midiFollowDisplayParam);
}
void set_follow_display_param(const MenuContext&, int32_t value) {
	midiEngine.midiFollowDisplayParam = value != 0;
}

// ---------------------------------------------------------------------------------------------
// The rest of the MIDI page

int32_t get_midi_select_kit_row(const MenuContext&) {
	return from_bool(midiEngine.midiSelectKitRow); // `midiSelectKitRowMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`)
}
void set_midi_select_kit_row(const MenuContext&, int32_t value) {
	midiEngine.midiSelectKitRow = value != 0;
}

int32_t get_midi_thru(const MenuContext&) {
	return from_bool(midiEngine.midiThru); // `midiThruMenu`
}
void set_midi_thru(const MenuContext&, int32_t value) {
	midiEngine.midiThru = value != 0;
}

int32_t get_midi_transpose_method(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/midi/transpose.h` `readCurrentValue`
	return static_cast<int32_t>(MIDITranspose::controlMethod);
}
void set_midi_transpose_method(const MenuContext&, int32_t value) {
	// `origin/main:src/deluge/gui/menu_item/midi/transpose.h` `writeCurrentValue`
	MIDITranspose::controlMethod = static_cast<MIDITransposeControlMethod>(value);
}

int32_t get_midi_takeover(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/midi/takeover.h` `readCurrentValue`
	return static_cast<int32_t>(midiEngine.midiTakeover);
}
void set_midi_takeover(const MenuContext&, int32_t value) {
	// `origin/main:src/deluge/gui/menu_item/midi/takeover.h` `writeCurrentValue`
	midiEngine.midiTakeover = static_cast<MIDITakeoverMode>(value);
}

int32_t get_midi_input_differentiation(const MenuContext&) {
	return from_bool(MIDIDeviceManager::differentiatingInputsByDevice); // `midiInputDifferentiationMenu`
	                                                                    // (`origin/main:src/deluge/gui/ui/menus.cpp`)
}
void set_midi_input_differentiation(const MenuContext&, int32_t value) {
	MIDIDeviceManager::differentiatingInputsByDevice = value != 0;
}

// ---------------------------------------------------------------------------------------------
// Defaults: UI

int32_t get_default_accessibility_shortcuts(const MenuContext&) {
	return from_bool(FlashStorage::accessibilityShortcuts); // `defaultAccessibilityShortcuts`
	                                                        // (`origin/main:src/deluge/gui/ui/menus.cpp`)
}
void set_default_accessibility_shortcuts(const MenuContext&, int32_t value) {
	FlashStorage::accessibilityShortcuts = value != 0;
}

int32_t get_default_menu_highlighting(const MenuContext&) {
	// `AccessibilityMenuHighlighting::readCurrentValue`
	// (`origin/main:src/deluge/gui/menu_item/defaults/accessibility_menu_highlighting.h`)
	return static_cast<int32_t>(FlashStorage::accessibilityMenuHighlighting);
}
void set_default_menu_highlighting(const MenuContext&, int32_t value) {
	// `AccessibilityMenuHighlighting::writeCurrentValue`: the setting, then main's canvas flag and the guest's panels.
	FlashStorage::accessibilityMenuHighlighting = static_cast<MenuHighlighting>(value);
	host_highlighting_changed();
}

int32_t get_default_session_layout(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/defaults/session_layout.h` `readCurrentValue`
	return static_cast<int32_t>(FlashStorage::defaultSessionLayout);
}
void set_default_session_layout(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/session_layout.h`).
	FlashStorage::defaultSessionLayout = static_cast<SessionLayoutType>(value);
}

int32_t get_default_grid_active_mode(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/defaults/grid_default_active_mode.h` `readCurrentValue`
	return static_cast<int32_t>(FlashStorage::defaultGridActiveMode);
}
void set_default_grid_active_mode(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/grid_default_active_mode.h`).
	FlashStorage::defaultGridActiveMode = static_cast<GridDefaultActiveMode>(value);
}

int32_t get_default_grid_green_selection(const MenuContext&) {
	return from_bool(FlashStorage::gridAllowGreenSelection); // `defaultGridAllowGreenSelection`
	                                                         // (`origin/main:src/deluge/gui/ui/menus.cpp`)
}
void set_default_grid_green_selection(const MenuContext&, int32_t value) {
	FlashStorage::gridAllowGreenSelection = value != 0;
}

int32_t get_default_grid_pads_unarm(const MenuContext&) {
	// `defaultgridEmptyPadsUnarm` (`origin/main:src/deluge/gui/ui/menus.cpp`)
	return from_bool(FlashStorage::gridEmptyPadsUnarm);
}
void set_default_grid_pads_unarm(const MenuContext&, int32_t value) {
	FlashStorage::gridEmptyPadsUnarm = value != 0;
}

int32_t get_default_grid_pads_create_rec(const MenuContext&) {
	return from_bool(FlashStorage::gridEmptyPadsCreateRec); // `defaultGridEmptyPadsCreateRec`
	                                                        // (`origin/main:src/deluge/gui/ui/menus.cpp`)
}
void set_default_grid_pads_create_rec(const MenuContext&, int32_t value) {
	FlashStorage::gridEmptyPadsCreateRec = value != 0;
}

int32_t get_default_keyboard_layout(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/defaults/keyboard_layout.h` `readCurrentValue`
	return static_cast<int32_t>(FlashStorage::defaultKeyboardLayout);
}
void set_default_keyboard_layout(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/keyboard_layout.h`).
	FlashStorage::defaultKeyboardLayout = static_cast<KeyboardLayoutType>(value);
}

int32_t get_default_kb_velocity_glide(const MenuContext&) {
	return from_bool(FlashStorage::keyboardFunctionsVelocityGlide,
	                 /*invert=*/true); // `defaultUIKeyboardFunctionsVelocityGlide`
}
void set_default_kb_velocity_glide(const MenuContext&, int32_t value) {
	FlashStorage::keyboardFunctionsVelocityGlide = value == 0;
}

int32_t get_default_kb_modwheel_glide(const MenuContext&) {
	return from_bool(FlashStorage::keyboardFunctionsModwheelGlide,
	                 /*invert=*/true); // `defaultUIKeyboardFunctionsModwheelGlide`
}
void set_default_kb_modwheel_glide(const MenuContext&, int32_t value) {
	FlashStorage::keyboardFunctionsModwheelGlide = value == 0;
}

int32_t get_default_favourites_layout(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/defaults/favourites_layout.h` `readCurrentValue`
	return static_cast<int32_t>(FlashStorage::defaultFavouritesLayout);
}
void set_default_favourites_layout(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/favourites_layout.h`).
	FlashStorage::defaultFavouritesLayout = static_cast<FavouritesDefaultLayout>(value);
}

int32_t get_default_new_clip_type(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/defaults/ui/clip_type/default_new_clip_type.h` `readCurrentValue`
	return static_cast<int32_t>(FlashStorage::defaultNewClipType);
}
void set_default_new_clip_type(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/ui/clip_type/default_new_clip_type.h`).
	FlashStorage::defaultNewClipType = static_cast<OutputType>(value);
}

int32_t get_default_use_last_clip_type(const MenuContext&) {
	return from_bool(FlashStorage::defaultUseLastClipType); // `defaultUseLastClipTypeMenu`
	                                                        // (`origin/main:src/deluge/gui/ui/menus.cpp`)
}
void set_default_use_last_clip_type(const MenuContext&, int32_t value) {
	FlashStorage::defaultUseLastClipType = value != 0;
}

int32_t get_default_use_sharps(const MenuContext&) {
	return from_bool(FlashStorage::defaultUseSharps); // `defaultUseSharps` (`origin/main:src/deluge/gui/ui/menus.cpp`)
}
void set_default_use_sharps(const MenuContext&, int32_t value) {
	FlashStorage::defaultUseSharps = value != 0;
}

// ---------------------------------------------------------------------------------------------
// Defaults: automation

int32_t get_default_automation_interpolate(const MenuContext&) {
	return from_bool(FlashStorage::automationInterpolate); // `defaultAutomationInterpolateMenu`
}
void set_default_automation_interpolate(const MenuContext&, int32_t value) {
	FlashStorage::automationInterpolate = value != 0;
}
int32_t get_default_automation_clear(const MenuContext&) {
	return from_bool(FlashStorage::automationClear); // `defaultAutomationClearMenu`
}
void set_default_automation_clear(const MenuContext&, int32_t value) {
	FlashStorage::automationClear = value != 0;
}
int32_t get_default_automation_shift(const MenuContext&) {
	return from_bool(FlashStorage::automationShift); // `defaultAutomationShiftMenu`
}
void set_default_automation_shift(const MenuContext&, int32_t value) {
	FlashStorage::automationShift = value != 0;
}
int32_t get_default_automation_nudge(const MenuContext&) {
	return from_bool(FlashStorage::automationNudgeNote); // `defaultAutomationNudgeNoteMenu`
	                                                     // (`origin/main:src/deluge/gui/ui/menus.cpp`)
}
void set_default_automation_nudge(const MenuContext&, int32_t value) {
	FlashStorage::automationNudgeNote = value != 0;
}
int32_t get_default_automation_no_audition(const MenuContext&) {
	// `defaultAutomationDisableAuditionPadShortcutsMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`)
	return from_bool(FlashStorage::automationDisableAuditionPadShortcuts);
}
void set_default_automation_no_audition(const MenuContext&, int32_t value) {
	FlashStorage::automationDisableAuditionPadShortcuts = value != 0;
}

// ---------------------------------------------------------------------------------------------
// Defaults: the rest

int32_t default_resolution_magnitude(const MenuContext&) {
	return FlashStorage::defaultMagnitude;
}
int32_t relative_to_song_magnitude(const MenuContext&) {
	return 1;
}
int32_t get_default_swing_interval(const MenuContext&) {
	return FlashStorage::defaultSwingInterval; // `origin/main:src/deluge/gui/menu_item/defaults/swing_interval.h`
	                                           // `readCurrentValue`
}
void set_default_swing_interval(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/swing_interval.h`).
	FlashStorage::defaultSwingInterval = static_cast<uint8_t>(value);
}

int32_t get_default_scale(const MenuContext&) {
	// `defaults::DefaultScale::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/scale.h`) with its own
	// `scaleToOptionIndex`: USER_SCALE is not offerable as a default, so the option index
	// is the scale index with that one slot removed.
	const deluge::music::Scale scale = deluge::music::flashStorageCodeToScale(FlashStorage::defaultScale);
	return scale >= deluge::music::USER_SCALE ? static_cast<int32_t>(scale) - 1 : static_cast<int32_t>(scale);
}
void set_default_scale(const MenuContext&, int32_t value) {
	// `origin/main:src/deluge/gui/menu_item/defaults/scale.h` `writeCurrentValue` over `optionIndexToScale`
	int32_t index = value;
	if (index >= deluge::music::USER_SCALE) {
		index += 1;
	}
	FlashStorage::defaultScale = deluge::music::scaleToFlashStorageCode(static_cast<deluge::music::Scale>(index));
}
size_t default_scale_names(const MenuContext&, std::span<std::string_view> out) {
	size_t n = 0; // `DefaultScale::getOptions` (`origin/main:src/deluge/gui/menu_item/defaults/scale.h`)
	for (uint8_t i = 0; i < deluge::music::NUM_SCALELIKE && n < out.size(); i++) {
		if (i != deluge::music::USER_SCALE) {
			out[n++] = deluge::music::scalelikeNames[i];
		}
	}
	return n;
}

int32_t get_default_velocity(const MenuContext&) {
	return FlashStorage::defaultVelocity; // `origin/main:src/deluge/gui/menu_item/defaults/velocity.h`
	                                      // `readCurrentValue`
}
void set_default_velocity(const MenuContext& ctx, int32_t value) {
	// `origin/main:src/deluge/gui/menu_item/defaults/velocity.h` `writeCurrentValue`
	FlashStorage::defaultVelocity = static_cast<uint8_t>(value);
	if (ctx.song != nullptr) {
		ctx.song->setDefaultVelocityForAllInstruments(FlashStorage::defaultVelocity);
	}
}

int32_t get_default_magnitude(const MenuContext&) {
	return FlashStorage::defaultMagnitude; // `origin/main:src/deluge/gui/menu_item/defaults/magnitude.h`
	                                       // `readCurrentValue`
}
void set_default_magnitude(const MenuContext&, int32_t value) {
	// `origin/main:src/deluge/gui/menu_item/defaults/magnitude.h` `writeCurrentValue`: the swing interval is nudged by
	// the same delta so its musical meaning survives the resolution change.
	const int32_t old_magnitude = FlashStorage::defaultMagnitude;
	FlashStorage::defaultMagnitude = static_cast<int8_t>(value);
	const int32_t delta = FlashStorage::defaultMagnitude - old_magnitude;
	FlashStorage::defaultSwingInterval =
	    deluge::plugin::clamp_swing_interval(FlashStorage::defaultSwingInterval - delta);
}

int32_t get_default_bend_range(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/defaults/bend_range.h` `readCurrentValue`
	return FlashStorage::defaultBendRange[BEND_RANGE_MAIN];
}
void set_default_bend_range(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/bend_range.h`).
	FlashStorage::defaultBendRange[BEND_RANGE_MAIN] = static_cast<uint8_t>(value);
}

int32_t get_default_metronome_volume(const MenuContext&) {
	return FlashStorage::defaultMetronomeVolume; // `origin/main:src/deluge/gui/menu_item/defaults/metronome_volume.h`
	                                             // `readCurrentValue`
}
void set_default_metronome_volume(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/metronome_volume.h`).
	FlashStorage::defaultMetronomeVolume = static_cast<uint8_t>(value);
	AudioEngine::metronome.setVolume(value);
}

int32_t get_default_startup_song(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/defaults/startup_song_mode.h` `readCurrentValue`
	return static_cast<int32_t>(FlashStorage::defaultStartupSongMode);
}
void set_default_startup_song(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/startup_song_mode.h`).
	FlashStorage::defaultStartupSongMode = static_cast<StartupSongMode>(value);
}

int32_t get_default_pad_brightness(const MenuContext&) {
	// `PadBrightness::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/pad_brightness.h`), `to_ui`
	return FlashStorage::defaultPadBrightness << 2;
}
void set_default_pad_brightness(const MenuContext&, int32_t value) {
	// `PadBrightness::writeCurrentValue`: the setting, then the pads' dimmer.
	FlashStorage::defaultPadBrightness = static_cast<uint8_t>(value >> 2);
	host_pad_brightness_changed();
}

int32_t get_default_slice_mode(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/defaults/slice_mode.h` `readCurrentValue`
	return static_cast<int32_t>(FlashStorage::defaultSliceMode);
}
void set_default_slice_mode(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/slice_mode.h`).
	FlashStorage::defaultSliceMode = static_cast<SampleRepeatMode>(value);
}

int32_t get_default_cpu_indicator(const MenuContext&) {
	return from_bool(FlashStorage::highCPUUsageIndicator); // `defaultHighCPUUsageIndicatorMode`
	                                                       // (`origin/main:src/deluge/gui/ui/menus.cpp`)
}
void set_default_cpu_indicator(const MenuContext&, int32_t value) {
	FlashStorage::highCPUUsageIndicator = value != 0;
}

int32_t get_default_hold_time(const MenuContext&) {
	return FlashStorage::defaultHoldTime; // `HoldTime::readCurrentValue`
	                                      // (`origin/main:src/deluge/gui/menu_item/defaults/hold_time.h`)
}
void set_default_hold_time(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/defaults/hold_time.h`).
	FlashStorage::defaultHoldTime = static_cast<uint8_t>(value);
	FlashStorage::holdTime = (FlashStorage::defaultHoldTime * kSampleRate) / 20;
}

int32_t get_screensaver_mode(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/defaults/screensaver.h` `readCurrentValue`
	return static_cast<int32_t>(FlashStorage::screensaverMode);
}
void set_screensaver_mode(const MenuContext&, int32_t value) {
	// `ScreensaverModeMenu::writeCurrentValue`: the setting, then both screensavers re-read it.
	FlashStorage::screensaverMode = static_cast<ScreensaverMode>(value);
	host_screensaver_settings_changed();
}

int32_t get_screensaver_timeout(const MenuContext&) {
	return FlashStorage::screensaverTimeoutMinutes; // `origin/main:src/deluge/gui/menu_item/defaults/screensaver.h`
	                                                // `readCurrentValue`
}
void set_screensaver_timeout(const MenuContext&, int32_t value) {
	// `ScreensaverTimeout::writeCurrentValue`: the setting, then both screensavers re-read it.
	FlashStorage::screensaverTimeoutMinutes = static_cast<uint8_t>(value);
	host_screensaver_settings_changed();
}

// ---------------------------------------------------------------------------------------------
// Pads

int32_t get_pads_keyboard_layout(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/keyboard/layout.h` `Layout::readCurrentValue`
	return static_cast<int32_t>(FlashStorage::keyboardLayout);
}
void set_pads_keyboard_layout(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/keyboard/layout.h`).
	FlashStorage::keyboardLayout = static_cast<KeyboardLayout>(value);
}
size_t pads_keyboard_layout_names(const MenuContext&, std::span<std::string_view> out) {
	// `origin/main:src/deluge/gui/menu_item/keyboard/layout.h` `Layout::getOptions`
	static constexpr std::string_view kNames[] = {"QWERTY", "AZERTY", "QWERTZ"};
	size_t n = 0;
	for (std::string_view name : kNames) {
		if (n < out.size()) {
			out[n++] = name;
		}
	}
	return n;
}

int32_t get_sample_browser_preview(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/sample/browser_preview/mode.h` `readCurrentValue`
	return FlashStorage::sampleBrowserPreviewMode;
}
void set_sample_browser_preview(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/sample/browser_preview/mode.h`).
	FlashStorage::sampleBrowserPreviewMode = static_cast<uint8_t>(value);
}

// ---------------------------------------------------------------------------------------------
// Recording

int32_t get_record_count_in(const MenuContext&) {
	return playbackHandler.countInBars; // `origin/main:src/deluge/gui/menu_item/record/countin.h` `readCurrentValue`
}
void set_record_count_in(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/record/countin.h`).
	playbackHandler.countInBars = static_cast<uint8_t>(value);
}

int32_t get_record_quantize(const MenuContext&) {
	return FlashStorage::recordQuantizeLevel; // `Quantize::readCurrentValue`
	                                          // (`origin/main:src/deluge/gui/menu_item/record/quantize.h`)
}
void set_record_quantize(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/record/quantize.h`).
	FlashStorage::recordQuantizeLevel = static_cast<uint8_t>(value);
}

int32_t get_record_margins(const MenuContext&) {
	// `recordMarginsMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`)
	return from_bool(FlashStorage::audioClipRecordMargins);
}
void set_record_margins(const MenuContext&, int32_t value) {
	FlashStorage::audioClipRecordMargins = value != 0;
}

int32_t get_monitor_mode(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/monitor/mode.h` `readCurrentValue`
	return static_cast<int32_t>(AudioEngine::inputMonitoringMode);
}
void set_monitor_mode(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/monitor/mode.h`).
	AudioEngine::inputMonitoringMode = static_cast<InputMonitoringMode>(value);
}

int32_t get_default_threshold_recording(const MenuContext&) {
	// `ThresholdMode::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/record/threshold_mode.h`)
	return static_cast<int32_t>(FlashStorage::defaultThresholdRecordingMode);
}
void set_default_threshold_recording(const MenuContext& ctx, int32_t value) {
	// `ThresholdMode::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/record/threshold_mode.h`): the DEFAULT
	// kind writes the flash setting **and then**, if there is a song, the song's own mode too -- both arms run, which
	// is why this is not an either/or.
	FlashStorage::defaultThresholdRecordingMode = static_cast<ThresholdRecordingMode>(value);
	if (ctx.song != nullptr) {
		ctx.song->thresholdRecordingMode = static_cast<ThresholdRecordingMode>(value);
	}
}

// ---------------------------------------------------------------------------------------------
// Community features

int32_t runtime_feature_get(int32_t type) {
	// `Setting::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/runtime_feature/setting.cpp`) scans the option
	// list for the stored value and reports its index; every option list here is `{0, 1}` in index order
	// (`SetupOnOffSetting` and `SetupSyncScalingActionSetting` in `runtime_feature_settings.cpp`), so the index IS the
	// value.
	return static_cast<int32_t>(runtimeFeatureSettings.get(static_cast<RuntimeFeatureSettingType>(type)));
}
void runtime_feature_set(int32_t type, int32_t value) {
	// `Setting::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/runtime_feature/setting.cpp`)
	runtimeFeatureSettings.set(static_cast<RuntimeFeatureSettingType>(type), static_cast<uint32_t>(value));
}

void set_rounded_corners(const MenuContext&, int32_t value) {
	// `RoundedCornersSettingToggle::writeCurrentValue`: the setting, then main's canvas flag and the guest's panels.
	runtime_feature_set(RuntimeFeatureSettingType::RoundedCorners, value);
	host_rounded_corners_changed();
}

void set_horizontal_menus(const MenuContext&, int32_t value) {
	// `HorizontalMenu::renderingStyle()` (`origin/main:src/deluge/gui/menu_item/horizontal_menu.cpp`) reads the setting
	// live; the open menu's context holds the copy it lays out by.
	runtime_feature_set(RuntimeFeatureSettingType::HorizontalMenus, value);
	menu_scene().set_horizontal_menus(value != 0);
}

void set_shift_is_sticky(const MenuContext&, int32_t value) {
	// `ShiftIsSticky::writeCurrentValue`: the setting and, on, the shift LED; then the guest's shift machine and
	// legacy's latch.
	runtime_feature_set(RuntimeFeatureSettingType::ShiftIsSticky, value);
	if (value != 0) {
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::LightShiftLed, RuntimeFeatureStateToggle::On);
	}
	host_shift_sticky_changed();
}

size_t new_clip_type_names(const MenuContext&, std::span<std::string_view> out) {
	// `DefaultNewClipType::getOptions`
	// (`origin/main:src/deluge/gui/menu_item/defaults/ui/clip_type/default_new_clip_type.h`).
	static constexpr std::string_view kNames[] = {"SYNTH", "KIT", "MIDI", "CV", "AUDIO"};
	size_t n = 0;
	for (std::string_view name : kNames) {
		if (n < out.size()) {
			out[n++] = name;
		}
	}
	return n;
}

size_t sync_scaling_action_names(const MenuContext&, std::span<std::string_view> out) {
	// `SetupSyncScalingActionSetting` (`runtime_feature_settings.cpp`): two literal option
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
// 8-9. Sample preview and play cursor

int32_t get_usb_audio_enabled(const MenuContext&) {
	return from_bool(FlashStorage::usbAudioEnabled);
}
void set_usb_audio_enabled(const MenuContext&, int32_t value) {
	FlashStorage::usbAudioEnabled = value != 0;
	if (FlashStorage::usbAudioEnabled != usb_audio::present()) {
		deluge::notify::notice_string(sid(String::STRING_FOR_RESTART_TO_APPLY));
	}
}
int32_t get_usb_direct_monitoring(const MenuContext&) {
	return from_bool(usb_audio::directMonitoring);
}
void set_usb_direct_monitoring(const MenuContext&, int32_t value) {
	usb_audio::directMonitoring = value != 0;
}
int32_t get_usb_output_source(const MenuContext&) {
	return static_cast<int32_t>(usb_audio::outputSource);
}
void set_usb_output_source(const MenuContext&, int32_t value) {
	usb_audio::outputSource = static_cast<usb_audio::OutputSource>(value);
}

// ---------------------------------------------------------------------------------------------
// 7. Pads

// Both hooks qualify their `menu::` callee: `nodes` already has a `shortcuts_version` (the `Node`)
// and a `set_shortcuts_version` (this hook), and either name would hide the enclosing namespace's.
int32_t get_shortcuts_version(const MenuContext&) {
	// `Version::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/shortcuts/version.h`)
	return static_cast<int32_t>(menu::shortcuts_version());
}
void set_shortcuts_version(const MenuContext&, int32_t value) {
	// `shortcuts::Version::writeCurrentValue`: the write that persists, flash byte 60
	// (`FlashStorage::shortcutsVersion`), which the shortcut lookups read and `FlashStorage::writeSettings` stores.
	menu::set_shortcuts_version(value == static_cast<int32_t>(project::ShortcutsVersion::V1)
	                                ? project::ShortcutsVersion::V1
	                                : project::ShortcutsVersion::V3);
}

// The six `pad_colour_*` nodes are six instantiations of `nodes/settings.h`'s `get_pad_colour` /
// `set_pad_colour` over a plain `int`, so that header need not include `storage/flash_storage.h`.
// Those literals are only correct while `PadColourSlot` keeps its current numbering, so the
// numbering is pinned here -- the same guard, for the same reason, as the `DescriptorId` asserts
// below.
static_assert(static_cast<int>(FlashStorage::PAD_COLOUR_ACTIVE) == 0);
static_assert(static_cast<int>(FlashStorage::PAD_COLOUR_STOPPED) == 1);
static_assert(static_cast<int>(FlashStorage::PAD_COLOUR_MUTED) == 2);
static_assert(static_cast<int>(FlashStorage::PAD_COLOUR_SOLOED) == 3);
static_assert(static_cast<int>(FlashStorage::PAD_COLOUR_FILL) == 4);
static_assert(static_cast<int>(FlashStorage::PAD_COLOUR_ONCE) == 5);
static_assert(static_cast<int>(FlashStorage::NUM_PAD_COLOUR_SLOTS) == 6);

int32_t pad_colour_get(int32_t slot) {
	// `menu_item::Colour::readCurrentValue`, `origin/main:src/deluge/gui/menu_item/colour.h`
	return FlashStorage::padColours[slot];
}
void pad_colour_set(int32_t slot, int32_t value) {
	// `Colour::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/colour.h`): the setting, then every pad.
	FlashStorage::padColours[slot] = static_cast<uint8_t>(value);
	host_pad_colours_changed();
}

// ---------------------------------------------------------------------------------------------
// 2-3b. The CV and gate output pages (`cvSubmenu`, `gateModeMenu`, `gateOffTimeMenu`) -- the three
// pages legacy reaches only through a chain, see `nodes/settings.h`'s note.
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
	// `cv::Volts::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/cv/volts.h`).
	return cvEngine.cvChannels[output_index(ctx, NUM_PHYSICAL_CV_CHANNELS)].voltsPerOctave;
}
void set_cv_volts(const MenuContext& ctx, int32_t value) {
	// `cv::Volts::writeCurrentValue`.
	cvEngine.setCVVoltsPerOctave(static_cast<uint8_t>(output_index(ctx, NUM_PHYSICAL_CV_CHANNELS)),
	                             static_cast<uint8_t>(value));
}

int32_t get_cv_transpose(const MenuContext& ctx) {
	// `cv::Transpose::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/cv/transpose.h`), over
	// `computeCurrentValueForTranspose` (`util/value_scaling.cpp`) -- the same
	// packing `sound_osc.cpp` and `roots.cpp` transcribe for the other transposes.
	const CVChannel& channel = cvEngine.cvChannels[output_index(ctx, NUM_PHYSICAL_CV_CHANNELS)];
	return channel.transpose * 100 + channel.cents;
}
void set_cv_transpose(const MenuContext& ctx, int32_t value) {
	// `computeFinalValuesForTranspose` (`util/value_scaling.cpp`) then `cv::Transpose::writeCurrentValue`.
	const int32_t current = value + 25600;
	const int32_t semitones = (current + 50) / 100;
	cvEngine.setCVTranspose(static_cast<uint8_t>(output_index(ctx, NUM_PHYSICAL_CV_CHANNELS)), semitones - 256,
	                        current - semitones * 100);
}

size_t gate_mode_names(const MenuContext& ctx, std::span<std::string_view> out) {
	// `gate::Mode`'s `options_` as `updateOptions(whichOutput)` leaves it
	// (`origin/main:src/deluge/gui/menu_item/gate/mode.h`): the two trigger polarities always, plus one extra
	// on the outputs that carry the clock or the run signal.
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
	// `gate::Mode::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/gate/mode.h`).
	return static_cast<int32_t>(cvEngine.gateChannels[output_index(ctx, NUM_GATE_CHANNELS)].mode);
}
void set_gate_mode(const MenuContext& ctx, int32_t value) {
	// `gate::Mode::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/gate/mode.h`).
	cvEngine.setGateType(static_cast<uint8_t>(output_index(ctx, NUM_GATE_CHANNELS)), static_cast<GateType>(value));
}

int32_t get_gate_off_time(const MenuContext&) {
	// `gate::OffTime::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/gate/off_time.h`)
	return cvEngine.minGateOffTime;
}
void set_gate_off_time(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/gate/off_time.h`).
	cvEngine.minGateOffTime = static_cast<uint8_t>(value);
}

const Node* gate_selection_chain(const MenuContext& ctx, const MenuState&) {
	// `gate::Selection::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/gate/selection.h`): the option one
	// past the last physical output is the minimum-off-time page; every other option is that output's mode page.
	// Unclamped on purpose -- the equality is against the exact sentinel legacy tests, and `output_index` clamps at the
	// point of use inside the mode hooks.
	//
	// Reads `ctx.source_index`, not `state`'s own cursor: `menu_scene.h`'s mailbox note names this
	// the *load-bearing* drain-order case, and `tables_3_spec.cpp`'s "drains the selector's output
	// index BEFORE the navigation arm, not after" pins exactly this read. `GateSelectionItem`'s
	// backward arm (`nodes/dynamic.cpp`) needs a DIFFERENT value on the way back -- "the last real
	// output", which `source_index` is not once this branch has been taken -- and gets it from its
	// own `MenuState::real_output_choice` rather than by disturbing this read.
	return ctx.source_index == NUM_GATE_CHANNELS ? &gate_off_time : &gate_mode;
}

// ---------------------------------------------------------------------------------------------
// 12-13. Battery and firmware (`CustomItem`s -- see `nodes/settings.h`'s note on
// why these are not a shared `Kind`)

namespace {
/// @brief The node currently on screen, so a `CustomItem` -- which is handed no `Node` of its own,
///        only `MenuState` -- can still draw its own title. Mirrors `PendingCustom::render`
///        (`nodes/sound_osc.cpp`).
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

void FirmwareVersionItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	draw_title_for(f, state);
	// `firmware::Version::drawPixelsForOled` (`origin/main:src/deluge/gui/menu_item/firmware/version.h`): the build's
	// own version string, a build-time constant with nothing to format.
	// A context with no UI facts has no version to show: the readout is empty.
	draw_readout(f, ctx.ports.ui != nullptr ? ctx.ports.ui->firmware_version() : std::string_view{});
	// The select press leaves the item, same as every other leaf's arm (`menu_scene.cpp`'s
	// `render_leaf_sync_level`, for one) -- `Kind::Custom`'s own dispatch draws nothing else, so
	// this item claims it itself.
	if (f.select_pressed()) {
		state.tick.render_resp.entered = true;
	}
}

deluge::ui::shell::Result FirmwareVersionItem::handle(const deluge::ui::shell::UiEvent&,
                                                      const deluge::ui::shell::UiContext&, const MenuContext&,
                                                      MenuState&) const {
	return {}; // `firmware::Version` has no `buttonAction` override; nothing to consume here either
}

void BatteryLevelItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	draw_title_for(f, state);

	// A context with no UI facts has no reading: 0 mV, which draws as an empty battery.
	const int32_t battery_mv = ctx.ports.ui != nullptr ? ctx.ports.ui->battery_millivolts() : 0;

	// Charging heuristic, ported from `battery::Level::timerCallback`
	// (`origin/main:src/deluge/gui/menu_item/battery/level.h`): every 4 timer ticks at 500 ms (= 2000 ms), check
	// whether the voltage rose by more than 5 mV since the last check. This port has no separate
	// timer callback to count ticks on -- only a clock reading each time `render()` runs -- so the
	// same 2000 ms window is measured directly off `now_ms()` instead.
	constexpr uint32_t kChargeCheckMs = 2000;
	const uint32_t now = f.now_ms();
	if (!checked_once_) {
		last_check_mv_ = static_cast<uint16_t>(battery_mv);
		last_check_ms_ = now;
		checked_once_ = true;
	}
	else if (now - last_check_ms_ >= kChargeCheckMs) {
		const int32_t diff = battery_mv - static_cast<int32_t>(last_check_mv_);
		// `Level::timerCallback` (`origin/main:src/deluge/gui/menu_item/battery/level.h`)'s own threshold
		charging_ = diff > 5;
		last_check_mv_ = static_cast<uint16_t>(battery_mv);
		last_check_ms_ = now;
	}

	// `battery::Level::getBatteryString` (`origin/main:src/deluge/gui/menu_item/battery/level.h`). 2600 mV / 4200 mV
	// are its own empty/full endpoints.
	constexpr int32_t kMinMv = 2600;
	constexpr int32_t kMaxMv = 4200;
	int32_t percentage = 0;
	if (battery_mv > kMinMv) {
		percentage = (battery_mv - kMinMv) * 100 / (kMaxMv - kMinMv);
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
	append_int(battery_mv);
	append("mV)");
	// `Level::getBatteryString` (`origin/main:src/deluge/gui/menu_item/battery/level.h`): FULL takes precedence over
	// CHG -- a battery reading 99%+ is reported full even if the charging heuristic above still thinks it is rising.
	if (percentage >= 99) {
		append(" FULL");
	}
	else if (charging_) {
		append(" CHG");
	}

	draw_readout(f, std::string_view{buf, static_cast<size_t>(out - buf)});

	// `battery::Level::beginSession`/`timerCallback` re-render every 500 ms for as long as the item is on screen
	// (`origin/main:src/deluge/gui/menu_item/battery/level.h`); this port's equivalent asks the frame's own repaint
	// gate to wake it up on the same cadence -- the same shape every other animated widget here uses
	// (`menu_scene.cpp`'s `render_grid`, on the shortcut blink).
	f.gate().request_repaint_after(500, now);

	if (f.select_pressed()) {
		state.tick.render_resp.entered = true;
	}
}

deluge::ui::shell::Result BatteryLevelItem::handle(const deluge::ui::shell::UiEvent&,
                                                   const deluge::ui::shell::UiContext&, const MenuContext&,
                                                   MenuState&) const {
	return {}; // `battery::Level` has no `buttonAction` override either
}

void BatteryLevelItem::on_focus(const MenuContext&, MenuState&) const {
	// `battery::Level::beginSession` (`origin/main:src/deluge/gui/menu_item/battery/level.h`): reset the baseline
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
// The three `Kind::Range` items (see `nodes/settings.h`)
//
// `FlashStorage::defaultRanges` (`storage/flash_storage.h`) holds the six values main's
// `IntegerRange::lower`/`upper` and `KeyRange::lower`/`upper` hold
// (`origin/main:src/deluge/gui/menu_item/integer_range.h`, `origin/main:src/deluge/gui/menu_item/key_range.h`),
// and these twelve hooks address them.

namespace {

/// The slot each node's hooks address. Pinned against the enum here, in a translation unit that
/// sees both, so a reordering of `DefaultRangeSlot` fires instead of silently repointing an item at
/// the wrong setting -- the same guard `nodes/settings.cpp`'s pad-colour slots and `ResetItem`'s
/// descriptor ids already carry.
static_assert(FlashStorage::DEFAULT_RANGE_TEMPO == 0);
static_assert(FlashStorage::DEFAULT_RANGE_SWING_AMOUNT == 1);
static_assert(FlashStorage::DEFAULT_RANGE_KEY == 2);
static_assert(FlashStorage::NUM_DEFAULT_RANGE_SLOTS == 3);

} // namespace

std::string_view format_range_integer(int32_t value, std::span<char> scratch) {
	// `IntegerRange::getText` (`origin/main:src/deluge/gui/menu_item/integer_range.cpp`) spells each end with
	// `intToString(value, buffer)`, i.e. `intToString(value, buffer, 1)` -- at least one digit, and a
	// leading '-' for a negative one. `int_to_string` is the toolkit's port of exactly that call
	// (`ui/oled/detail/int_to_string.h`), which `int_value` uses for the same job.
	return deluge::ui::oled::detail::int_to_string(value, 1, scratch);
}

std::string_view format_range_key(int32_t value, std::span<char> scratch) {
	// `KeyRange::getText` (`origin/main:src/deluge/gui/menu_item/key_range.cpp`), one end at a time: the note
	// letter, then an accidental on a black key. The device's `defaultUseSharps` picks both the letter and the sign.
	const deluge::music::NoteName name =
	    deluge::music::note_name(std::clamp<int32_t>(value, 0, 11), FlashStorage::defaultUseSharps);
	size_t len = 0;
	if (len < scratch.size()) {
		scratch[len++] = name.letter;
	}
	if (name.accidental != deluge::music::Accidental::NONE && len < scratch.size()) {
		scratch[len++] = name.accidental == deluge::music::Accidental::SHARP ? '#' : deluge::gfx::oled::kFlatGlyph;
	}
	return std::string_view{scratch.data(), len};
}

int32_t get_default_tempo_lower(const MenuContext&) {
	return FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].lower;
}
void set_default_tempo_lower(const MenuContext&, int32_t value) {
	FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].lower = value;
}
int32_t get_default_tempo_upper(const MenuContext&) {
	return FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].upper;
}
void set_default_tempo_upper(const MenuContext&, int32_t value) {
	FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].upper = value;
}

int32_t get_default_swing_lower(const MenuContext&) {
	return FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_SWING_AMOUNT].lower;
}
void set_default_swing_lower(const MenuContext&, int32_t value) {
	FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_SWING_AMOUNT].lower = value;
}
int32_t get_default_swing_upper(const MenuContext&) {
	return FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_SWING_AMOUNT].upper;
}
void set_default_swing_upper(const MenuContext&, int32_t value) {
	FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_SWING_AMOUNT].upper = value;
}

int32_t get_default_key_lower(const MenuContext&) {
	return FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY].lower;
}
void set_default_key_lower(const MenuContext&, int32_t value) {
	FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY].lower = value;
}
int32_t get_default_key_upper(const MenuContext&) {
	return FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY].upper;
}
void set_default_key_upper(const MenuContext&, int32_t value) {
	FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY].upper = value;
}

// ---------------------------------------------------------------------------------------------

namespace {
constexpr const Node* kRoots[] = {&root_settings};
} // namespace

const std::span<const Node* const> kSettingsRoots{kRoots};

// --------------------------------------------------------------------------------------------- Voice usage
// (`origin/main:src/deluge/gui/menu_item/cpu/voice_usage.h`): the scene keeps the counters, sampled while the page is
// up.

int32_t get_voice_usage_live(const MenuContext&) {
	return menu_scene().voice_usage().live();
}
int32_t get_voice_usage_average(const MenuContext&) {
	return menu_scene().voice_usage().average();
}
int32_t get_voice_usage_maximum(const MenuContext&) {
	return menu_scene().voice_usage().maximum();
}
void set_voice_usage_count(const MenuContext&, int32_t) {
	// `VoiceCount::selectEncoderAction` does nothing (`origin/main:src/deluge/gui/menu_item/cpu/voice_usage.h`).
}
bool voice_usage_permitted(const MenuContext&) {
	// `VoiceCount::selectEncoderActionIsPermitted` (`origin/main:src/deluge/gui/menu_item/cpu/voice_usage.h`).
	return false;
}

} // namespace deluge::scenes::menu::nodes
