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

#include "scenes/bind/device_reader.h"
#include "scenes/l10n_bridge.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h" // MenuState, kMenuDepth -- for the CustomItems below
#include "scenes/profile.h"         // deluge_profile() -- ditto
#include "scenes/registry.h"        // menu_scene()

#include "definitions_cxx.hpp" // the flash-setting enums, MIDI_CHANNEL_NONE, NUM_CHANNELS, IS_A_CC
#include "deluge-project/midi_channels.h"
#include "gfx/oled/glyphs.h"
#include "io/midi/device_registry.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_transpose.h"
#include "io/usb/usb_audio.h"
#include "music-theory/note_names.h"
#include "notify/notice_queue.h" // the USB enable's "restart to apply"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h" // cvEngine -- the CV/gate output pages
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

/// @brief Read a plain `bool` global as a toggle's value.
constexpr int32_t from_bool(bool value) {
	return value ? 1 : 0;
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
// Defaults: the rest

int32_t default_resolution_magnitude(const MenuContext& ctx) {
	return bind::read_device_value(ctx.ports.device, project::device_path::kMagnitude);
}
int32_t relative_to_song_magnitude(const MenuContext&) {
	return 1;
}

std::optional<int32_t> pad_brightness_turn(const MenuContext&, int32_t value, int32_t offset) {
	// A level is four on the screen; the dimmest a turn reaches is level 1, the brightest level 25.
	constexpr int32_t kStep = 4;
	constexpr int32_t kLeast = 1 * kStep;
	constexpr int32_t kGreatest = 25 * kStep;
	return std::clamp(value + (offset * kStep), kLeast, kGreatest);
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

int32_t get_monitor_mode(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/monitor/mode.h` `readCurrentValue`
	return static_cast<int32_t>(AudioEngine::inputMonitoringMode);
}
void set_monitor_mode(const MenuContext&, int32_t value) {
	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/monitor/mode.h`).
	AudioEngine::inputMonitoringMode = static_cast<InputMonitoringMode>(value);
}

// ---------------------------------------------------------------------------------------------
// Community features

void apply_horizontal_menus(const MenuContext&, int32_t value) {
	menu_scene().set_horizontal_menus(value != 0);
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
// Defaults: the range ends' spellings

std::string_view format_range_integer(int32_t value, const void* /*hook_ctx*/, std::span<char> scratch) {
	// `IntegerRange::getText` (`origin/main:src/deluge/gui/menu_item/integer_range.cpp`) spells each end with
	// `intToString(value, buffer)`, i.e. `intToString(value, buffer, 1)` -- at least one digit, and a
	// leading '-' for a negative one. `int_to_string` is the toolkit's port of exactly that call
	// (`ui/oled/detail/int_to_string.h`), which `int_value` uses for the same job.
	return deluge::ui::oled::detail::int_to_string(value, 1, scratch);
}

std::string_view format_range_key(int32_t value, const void* hook_ctx, std::span<char> scratch) {
	// `KeyRange::getText` (`origin/main:src/deluge/gui/menu_item/key_range.cpp`), one end at a time: the note
	// letter, then an accidental on a black key. The device's `defaults/useSharps` row picks both the letter and the
	// sign.
	const auto* ctx = static_cast<const MenuContext*>(hook_ctx);
	const bool sharps =
	    bind::read_device_flag(ctx != nullptr ? ctx->ports.device : nullptr, project::device_path::kUseSharps);
	const deluge::music::NoteName name = deluge::music::note_name(std::clamp<int32_t>(value, 0, 11), sharps);
	size_t len = 0;
	if (len < scratch.size()) {
		scratch[len++] = name.letter;
	}
	if (name.accidental != deluge::music::Accidental::NONE && len < scratch.size()) {
		scratch[len++] = name.accidental == deluge::music::Accidental::SHARP ? '#' : deluge::gfx::oled::kFlatGlyph;
	}
	return std::string_view{scratch.data(), len};
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
