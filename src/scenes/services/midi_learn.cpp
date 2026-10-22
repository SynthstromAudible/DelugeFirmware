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

#include "scenes/services/midi_learn.h"

#include "io/midi/learned_midi.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "model/clip/clip.h"
#include "model/output.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "scenes/menu/bind_host.h"
#include "storage/flash_storage.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace deluge::scenes::services {

using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

constexpr deluge::ui::LedState kOff{deluge::ui::LedState::Kind::Off, 0, 0};
constexpr deluge::ui::LedState kOn{deluge::ui::LedState::Kind::On, 0, 0};

constexpr uint32_t kFlashMs = MidiLearn::kFlashMs;

/// @brief `LedId` for a `ButtonId`, exactly as `global_controls.cpp` spells it: the Deluge profile
///        numbers its LEDs by their button (`shell/profiles/deluge.h:19`).
/// @param b The button.
/// @return Its LED id.
constexpr deluge::ui::LedId led(ButtonId b) {
	return static_cast<deluge::ui::LedId>(static_cast<uint8_t>(b));
}

/// @brief The slot @p c binds, as a live reference into the MIDI engine.
///
/// `midiEngine.globalMIDICommands[util::to_underlying(c)]` (`view.cpp:156`, `:208`, `:224`,
/// `:804`, `:808`, `:812`), spelled with a `static_cast` because `util/` is outside this layer's
/// include allowance (`scripts/check_layer_rules.py`'s `scenes-includes-outside`) --
/// `menu/nodes/midi_command.cpp`'s `slot_for` makes the identical substitution for the identical
/// reason.
/// @param c The command.
/// @return Its learned-MIDI slot.
::LearnedMIDI& slot_for(GlobalMIDICommand c) {
	return midiEngine.globalMIDICommands[static_cast<size_t>(c)];
}

/// @brief The section learn's scratch command: legacy's `View::sectionLearnScratch`.
::LearnedMIDI g_section_scratch{};

/// @brief Does @p c already carry a learned message? `containsSomething()` (`view.cpp:821`).
/// @param c The command.
/// @return Whether something is learned to it.
bool global_command_learned(GlobalMIDICommand c) {
	return slot_for(c).containsSomething();
}

} // namespace

// -- `LearnTarget` -------------------------------------------------------------------------------

LearnTarget LearnTarget::section(::Song& song, uint8_t section) {
	// `View::sectionMidiLearnPadPressed`: the song's record holds the command, so the learn edits a
	// scratch copy of it and commits each change back.
	g_section_scratch = deluge::bridge::learned_of(deluge::bridge::sectionCommand(song.project.mappings, section));
	return LearnTarget{.kind = ::MidiLearn::SECTION,
	                   .thing = &g_section_scratch,
	                   .mapping = deluge::project::SectionLaunchTarget{section},
	                   .section_index = section};
}

LearnTarget LearnTarget::clip(deluge::project::ClipAddress clip) {
	// `View::clipStatusMidiLearnPadPressed` (`view.cpp:551-553`).
	return LearnTarget{.kind = ::MidiLearn::CLIP, .mapping = deluge::project::ClipMuteTarget{clip.clip_id}};
}

LearnTarget LearnTarget::note_row_mute(std::optional<uint32_t> drum_id) {
	// `View::noteRowMuteMidiLearnPadPressed` (`view.cpp:565-567`). Legacy dereferences the row's drum unguarded;
	// every call site has already refused a drumless row (`gui/views/instrument_clip_view.cpp:2270-2272`), so the
	// guard here is the same refusal spelled where the target is built.
	if (!drum_id) {
		return LearnTarget{};
	}
	return LearnTarget{.kind = ::MidiLearn::NOTEROW_MUTE, .mapping = deluge::project::DrumMuteTarget{*drum_id}};
}

LearnTarget LearnTarget::drum_input(uint32_t drum_id) {
	// `View::drumMidiLearnPadPressed` (`view.cpp:579-584`).
	return LearnTarget{.kind = ::MidiLearn::DRUM_INPUT, .mapping = deluge::project::LaneInputTarget{drum_id}};
}

LearnTarget LearnTarget::instrument_input(uint32_t output_id) {
	// `View::instrumentMidiLearnPadPressed` (`view.cpp:598-601`).
	return LearnTarget{.kind = ::MidiLearn::INSTRUMENT_INPUT, .mapping = deluge::project::TrackInputTarget{output_id}};
}

LearnTarget LearnTarget::global_command(GlobalMIDICommand command) {
	// `View::buttonAction`'s three learn arms (`view.cpp:155-156`, `:207-208`, `:223-224`): the
	// button's `MidiLearn` enumerator and `&midiEngine.globalMIDICommands[to_underlying(command)]`.
	::MidiLearn kind = ::MidiLearn::NONE;
	switch (command) {
	case GlobalMIDICommand::PLAY:
		kind = ::MidiLearn::PLAY_BUTTON;
		break;
	case GlobalMIDICommand::RECORD:
		kind = ::MidiLearn::RECORD_BUTTON;
		break;
	case GlobalMIDICommand::TAP:
		kind = ::MidiLearn::TAP_TEMPO_BUTTON;
		break;
	default:
		// The other nine `GlobalMIDICommand`s are learned through the settings menu's
		// `midi_command_*` nodes (`scenes/menu/nodes/midi_command.cpp`), never through a button
		// held under learn -- legacy's `View::buttonAction` has arms for exactly these three.
		return LearnTarget{};
	}
	return LearnTarget{.kind = kind, .thing = &slot_for(command)};
}

// -- The mode ------------------------------------------------------------------------------------

void MidiLearn::begin() {
	// `View::startMIDILearn` (`view.cpp:489-497`). The guard at `:490` is
	// `currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_MIDI_LEARN`: a press while some
	// *other* mode is running is refused outright, and re-entering while already in learn is a
	// deliberate no-op-shaped reset (it clears the held thing).
	if (other_mode_active_ && !active_) {
		return;
	}
	pressed_ = LearnTarget{};      // `:474` `thingPressedForMidiLearn = MidiLearn::NONE`
	should_save_settings_ = false; // `:475`
	active_ = true;                // `:476`
	flash_step();                  // `:477` `midiLearnFlash()`
	// `:478` `indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1)` -- declarative here; see
	// `leds()`.
}

void MidiLearn::end() {
	// `View::endMIDILearn` (`view.cpp:499-517`).
	// `:483-487`. Legacy's own comment on the inner guard: "Rare case where we could have been
	// called during audio routine."
	if (should_save_settings_) {
		if (!AudioEngine::audioRoutineLocked) {
			FlashStorage::writeSettings();
		}
	}
	flash_remaining_ms_.reset(); // `:488` `unsetTimer(TimerName::MIDI_LEARN_FLASH)`
	flash_on_ = false;           // `:489`
	// `:490-492` `getRootUI()->midiLearnFlash()` is the root view's grid repaint. Here that is
	// `timer_repaints()`'s grid claim, which `Shell::tick` has already armed for the deadline this
	// call is cancelling -- the gate fires once more and the scenes repaint with `flash_on()`
	// false, which is exactly what legacy's call buys.
	active_ = false; // `:494-496`

	// `:498-499` `playbackHandler.setLedStates()` + `setLedState(LEARN, false)`: declarative, see
	// `leds()`. Not clearing `pressed_` here is legacy's too -- `endMIDILearn` leaves
	// `thingPressedForMidiLearn` alone and the next `startMIDILearn` resets it (`:474`).
}

// -- The pressed thing ---------------------------------------------------------------------------

void MidiLearn::end_press_session(LearnTarget new_thing) {
	// `View::endMidiLearnPressSession` (`view.cpp:614-638`).
	//
	// `:599-612`'s switch calls `playbackHandler.setLedStates()` for the three button kinds and
	// nothing for the other six. In a declarative LED model that call is not a state change at
	// all: `leds()` recomposes from scratch every tick and stops declaring the flash the moment
	// `pressed_.kind` stops naming a button. Nothing to port, and it is named rather than dropped.
	pressed_ = new_thing; // `:614` `learnedThing = nullptr` + `:617` the new kind, as one write
	unported_[static_cast<size_t>(UnportedCall::SpecificDeviceHook)]++; // `:620`
}

void MidiLearn::release_pressed_thing() {
	// The `else if (thingPressedForMidiLearn == K)` body every caller shares. The `if
	// (deleteMidiCommandOnRelease)` test is `view.cpp:161`, `:542`, `:556`, `:570`, `:588`, `:605`;
	// what varies between the six is only what runs *alongside* the `clear()`.
	if (delete_on_release_ && pressed_.kind != ::MidiLearn::NONE) {
		switch (pressed_.kind) {
		case ::MidiLearn::PLAY_BUTTON:
		case ::MidiLearn::RECORD_BUTTON:
		case ::MidiLearn::TAP_TEMPO_BUTTON:
			// `:161-162`: the three button arms, and only they, ask for the settings write that
			// `endMIDILearn` then performs. Clearing a *pad's* command is a song edit, which the
			// song save owns; clearing a global command is a settings edit.
			pressed_.thing->clear();
			should_save_settings_ = true;
			break;
		case ::MidiLearn::INSTRUMENT_INPUT:
			// `:589-591`. The `clearMelodicInstrumentMonoExpressionIfPossible()` at `:589` runs
			// **before** the clear ("in case it gets stuck") and is not ported; see the class doc.
			// The unlearn marks the instrument edited, as `:591` does.
			unported_[static_cast<size_t>(UnportedCall::ClearMonoExpression)]++;
			commit(::LearnedMIDI{});
			break;
		default:
			// `CLIP`, `NOTEROW_MUTE`, `SECTION`: the bare clear. `DRUM_INPUT`'s (`:572-573`) marks the kit edited,
			// which its unlearn does.
			commit(::LearnedMIDI{});
			break;
		}
	}
	end_press_session(LearnTarget{}); // `:164`, `:528`, `:542`, `:556`, `:575`, `:593`
}

void MidiLearn::thing_pressed(LearnTarget target, bool on) {
	// A `NONE` target is "there is nothing learnable under that pad", which two of the factories can
	// answer (`note_row_mute` for a drumless row, `global_command` for a command with no button).
	// Legacy spells the same refusal at the CALL SITE instead -- `commandLearnMuteRow` returns
	// early on `!noteRow->drum` (`gui/views/instrument_clip_view.cpp:2270-2272`) rather than calling
	// the overload -- so this is the same behaviour with the test moved next to the thing it is
	// about.
	if (target.kind == ::MidiLearn::NONE) {
		return;
	}
	if (on) {
		end_press_session(target); // `view.cpp:537` / `:155`
		delete_on_release_ = true; // `:521` / `:153`
		// `:522` `learnedThing = &…` is `end_press_session`'s own assignment of `target.thing`.
		return;
	}
	// `:524` `else if (thingPressedForMidiLearn == MidiLearn::SECTION)`. The comparison is on the
	// KIND, not on the target: legacy has one `thingPressedForMidiLearn`, and a release of a
	// different kind of thing falls through doing nothing.
	if (pressed_.kind == target.kind) {
		release_pressed_thing();
	}
}

// -- The commit ----------------------------------------------------------------------------------

deluge::scenes::bind::WriteSink* MidiLearn::sink() const {
	if (sink_ != nullptr) {
		return sink_;
	}
	return song_ != nullptr && menu::host_binds(*song_) ? &menu::host_write_sink() : nullptr;
}

void MidiLearn::commit(const ::LearnedMIDI& learned, std::optional<deluge::scenes::bind::LearnInput> input) {
	// `View::commitLearnedThing`. A section's scratch holds what was learned, for the next press to read.
	if (pressed_.kind == ::MidiLearn::SECTION) {
		g_section_scratch = learned;
	}
	if (!pressed_.mapping) {
		if (pressed_.thing != nullptr) {
			*pressed_.thing = learned; // a global command: the device's, not the song's
		}
		return;
	}
	if (deluge::scenes::bind::WriteSink* const to = sink(); to != nullptr) {
		(void)to->learn(*pressed_.mapping,
		                learned.containsSomething() ? std::optional{deluge::bridge::command_of(learned)} : std::nullopt,
		                input);
	}
}

void MidiLearn::note_on_received(::MIDICable& cable, int32_t channel_or_zone, int32_t note, int32_t /*velocity*/) {
	// `View::noteOnReceivedForMidiLearn` (`view.cpp:640-760`).
	if (pressed_.kind == ::MidiLearn::NONE) { // `:624`
		return;
	}
	delete_on_release_ = false; // `:625` -- something was learned, so the release must not wipe it.

	// A note learned on an input brings its cable's bend ranges and velocity default (`:629-660`, `:677-728`,
	// `:733-737`), which the learn takes with it.
	std::optional<deluge::scenes::bind::LearnInput> input;
	switch (pressed_.kind) { // `:627`
	case ::MidiLearn::DRUM_INPUT:
		input = deluge::scenes::bind::LearnInput{.cable = &cable, .channel_or_zone = channel_or_zone};
		break;
	case ::MidiLearn::PLAY_BUTTON:
	case ::MidiLearn::RECORD_BUTTON:
	case ::MidiLearn::TAP_TEMPO_BUTTON:
		should_save_settings_ = true; // `:663-666`, "no break" into the shared write
		break;
	case ::MidiLearn::INSTRUMENT_INPUT:
		input = deluge::scenes::bind::LearnInput{.cable = &cable, .channel_or_zone = channel_or_zone};
		// `:728` `clearMelodicInstrumentMonoExpression()` -- not ported, see the class doc.
		unported_[static_cast<size_t>(UnportedCall::ClearMonoExpression)]++;
		break;
	default:
		// `CLIP`, `NOTEROW_MUTE`, `SECTION`: straight to the shared write (`:669`'s `default`).
		break;
	}

	if (!pressed_.mapping && pressed_.thing == nullptr) {
		return;
	}
	// The write every arm makes -- `:672-674` for the shared path, and `:730-732` again inside the
	// `INSTRUMENT_INPUT` arm, which is the same three fields in a different order.
	::LearnedMIDI learned;
	learned.cable = &cable;
	learned.channelOrZone = channel_or_zone;
	learned.noteOrCC = note; // legacy's comment at `:732`: used for low note in kits
	commit(learned, input);
}

void MidiLearn::cc_received(::MIDICable& cable, int32_t channel, int32_t cc, int32_t value) {
	// `View::ccReceivedForMIDILearn` (`view.cpp:784-811`).
	if (pressed_.kind == ::MidiLearn::NONE) { // `:768`
		return;
	}
	delete_on_release_ = false; // `:769`

	if (pressed_.kind == ::MidiLearn::INSTRUMENT_INPUT) { // `:772`
		// `:775` `getCurrentOutputType() == OutputType::MIDI_OUT`. Legacy asks the CURRENT output,
		// not the held instrument; the two are the same on every path that reaches here (the pad
		// that set the target is on the current clip's output) but the question is legacy's, so
		// the port asks legacy's.
		::Clip* const clip = song_ != nullptr ? song_->getCurrentClip() : nullptr;
		const bool current_output_is_midi =
		    clip != nullptr && clip->output != nullptr && clip->output->type == OutputType::MIDI_OUT;
		// The input the track holds now, from the song's mapping table.
		const deluge::project::Mapping* const held =
		    song_ != nullptr && pressed_.mapping ? song_->project.mappings.find(*pressed_.mapping) : nullptr;
		::LearnedMIDI learned = held != nullptr ? deluge::bridge::learned_of(held->source) : ::LearnedMIDI{};
		if (current_output_is_midi && learned.channelOrZone == MIDI_CHANNEL_NONE) { // `:778`
			// Legacy's comment: but only if the user hasn't already started learning MPE stuff, or
			// regular note-ons. The learn marks the instrument edited, as `:781` does.
			learned.cable = &cable;          // `:779`
			learned.channelOrZone = channel; // `:780`
			commit(learned);
		}
		return;
	}

	// `:787-791`: for everything else, a non-zero CC is a note-on with the CC bias applied.
	if (value != 0) {                                        // `:789`
		note_on_received(cable, channel + IS_A_CC, cc, 127); // `:790`
	}
}

uint32_t MidiLearn::unported_calls(UnportedCall which) const {
	const size_t i = static_cast<size_t>(which);
	return i < static_cast<size_t>(UnportedCall::kCount) ? unported_[i] : 0;
}

// -- Routing -------------------------------------------------------------------------------------

Result MidiLearn::handle(const UiEvent& event, const UiContext& /*ctx*/) {
	// The two MIDI kinds first, and deliberately ABOVE the root-scene gate below. That gate is the
	// `View::buttonAction` reachability rule and nothing else (`Scene::is_root`); legacy's learn
	// dispatchers reach `view.noteOnReceivedForMidiLearn` / `view.ccReceivedForMIDILearn` from
	// `PlaybackHandler` with no root-UI test anywhere on the path
	// (`playback/playback_handler.cpp:2970`, `:3169`), so applying the button gate to them would
	// invent a restriction legacy does not have.
	//
	// Claimed unconditionally, matching legacy's dispatchers: both `return` immediately after the
	// `view.` call whatever it did (`playback_handler.cpp:2972`, `:3171`), and both ported functions
	// are no-ops when nothing is held (`view.cpp:641`, and `cc_received`'s own arms). A null
	// `cable` is the "no message" value `UiEvent` documents and is dropped rather than
	// dereferenced.
	if (event.kind == UiEvent::Kind::MidiNoteOn || event.kind == UiEvent::Kind::MidiCc) {
		if (event.cable == nullptr) {
			return Result::unhandled();
		}
		if (event.kind == UiEvent::Kind::MidiNoteOn) {
			note_on_received(*event.cable, event.channel, event.data1, event.data2);
		}
		else {
			cc_received(*event.cable, event.channel, event.data1, event.data2);
		}
		return Result{.handled = true};
	}

	if (event.kind != UiEvent::Kind::Button) {
		return {};
	}
	// The `View::buttonAction` reachability gate, identical to `GlobalControls::handle`'s and for
	// the identical reason -- see the class doc, and `Scene::is_root`'s two-line proof. This
	// service is routed AFTER `global_controls`, which answers `ViewArm::NotAnArm` for a non-root
	// scene and therefore runs its own tail; without this gate a `LEARN` press inside a menu would
	// reach here and toggle a mode legacy would have handed to `SoundEditor::buttonAction`
	// (`gui/ui/sound_editor.cpp:505-537`) instead.
	if (!stack_.input_top().is_root()) {
		return Result::unhandled();
	}

	switch (event.button) {
	case ButtonId::Learn:
		// `view.cpp:186-197`. Claimed unconditionally -- legacy's arm has no mode guard, and
		// `:469` returns `DEALT_WITH` for it whether the press started or ended the mode.
		if (event.on) {
			begin(); // `:191`
		}
		else {
			end(); // `:194`
		}
		return Result{.handled = true};

	case ButtonId::TapTempo:
		// `view.cpp:147-167`'s learn branch. Its `else if` twin at `:168` is `GlobalControls`'s,
		// and the two are disjoint by legacy's own `if`/`else`.
		if (!active_) {
			return Result::unhandled();
		}
		thing_pressed(LearnTarget::global_command(GlobalMIDICommand::TAP), event.on); // `:152-165`
		return Result{.handled = true};

	case ButtonId::Play:
		// `view.cpp:200-213`, guarded `&& currentUIMode == UI_MODE_MIDI_LEARN`.
		if (!active_) {
			return Result::unhandled();
		}
		thing_pressed(LearnTarget::global_command(GlobalMIDICommand::PLAY), event.on);
		return Result{.handled = true};

	case ButtonId::Record:
		// `view.cpp:216-229`, same shape.
		if (!active_) {
			return Result::unhandled();
		}
		thing_pressed(LearnTarget::global_command(GlobalMIDICommand::RECORD), event.on);
		return Result{.handled = true};

	default:
		return Result::unhandled();
	}
}

// -- LEDs ----------------------------------------------------------------------------------------

void MidiLearn::leds(deluge::ui::LedFrame& frame) {
	if (!active_) {
		// `View::endMIDILearn`'s two LED calls (`view.cpp:515-516`), declaratively: stop declaring
		// PLAY/RECORD/TAP_TEMPO so `GlobalControls`'s `PlaybackHandler::setLedStates` port -- which
		// composed a moment earlier -- is the last writer again, and put LEARN out.
		//
		// LEARN is declared rather than left alone because legacy really does write it here, and an
		// undeclared LED keeps its last sent value (`shell::led_send_needed`). `MenuScene::leds`
		// declares LEARN too and outranks this: `LedCompose::compose` runs every service before
		// every scene (`shell/leds.h`).
		(void)frame.set(led(ButtonId::Learn), kOff);
		return;
	}

	// `indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1)` (`view.cpp:495`): forever, at
	// `blinkingType` 1's `kFastFlashTime` period (`hid/led/indicator_leds.cpp:81-83`). A real
	// `Kind::Blink` because legacy drives this LED from its own blinker phase, NOT from
	// `midiLearnFlashOn`.
	(void)frame.set(led(ButtonId::Learn),
	                deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, static_cast<uint16_t>(kFlashMs), 0});

	// `View::midiLearnFlash`'s three writes (`view.cpp:821-832`). Each is declared only when its
	// condition holds, so an unlearned, unheld button keeps whatever `GlobalControls` declared --
	// which is exactly what legacy's `if` does by simply not calling `setLedState`.
	const deluge::ui::LedState phase = flash_on_ ? kOn : kOff;
	if (global_command_learned(GlobalMIDICommand::PLAY) || pressed_.kind == ::MidiLearn::PLAY_BUTTON) { // `:804-805`
		(void)frame.set(led(ButtonId::Play), phase);                                                    // `:806`
	}
	if (global_command_learned(GlobalMIDICommand::RECORD)
	    || pressed_.kind == ::MidiLearn::RECORD_BUTTON) { // `:808-809`
		(void)frame.set(led(ButtonId::Record), phase);    // `:810`
	}
	if (global_command_learned(GlobalMIDICommand::TAP)
	    || pressed_.kind == ::MidiLearn::TAP_TEMPO_BUTTON) { // `:812-813`
		(void)frame.set(led(ButtonId::TapTempo), phase);     // `:814`
	}
}

// -- Time ----------------------------------------------------------------------------------------

void MidiLearn::flash_step() {
	// `View::midiLearnFlash`'s own two lines (`view.cpp:814-815`): toggle, then re-arm at
	// `kFastFlashTime`. The `getRootUI()->midiLearnFlash()` at `:800-802` is the grid repaint,
	// which `timer_repaints()` declares once rather than requesting per step.
	flash_on_ = !flash_on_;
	flash_remaining_ms_ = kFlashMs;
}

void MidiLearn::tick(uint32_t /*now_ms*/, uint32_t dt_ms) {
	if (!flash_remaining_ms_.has_value()) {
		return;
	}
	// A countdown against `dt_ms`, not a deadline against a `now`: `Service::tick`'s `now_ms` is
	// the only clock this layer has and it is not monotonic across a `Shell` that was never
	// ticked, whereas `dt_ms` is exactly what legacy's `uiTimerManager` counts down
	// (`gui/ui_timer_manager.cpp`). A tick longer than one half-period advances the phase once,
	// not N times -- which is `setTimer`'s behaviour too, since it re-arms from the moment it
	// fires and not from the moment it was due.
	if (dt_ms >= *flash_remaining_ms_) {
		flash_step();
		return;
	}
	flash_remaining_ms_ = *flash_remaining_ms_ - dt_ms;
}

std::optional<uint32_t> MidiLearn::next_timer() const {
	return flash_remaining_ms_;
}

} // namespace deluge::scenes::services
