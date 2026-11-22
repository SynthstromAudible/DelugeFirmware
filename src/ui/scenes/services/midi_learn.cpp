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

#include "ui/scenes/services/midi_learn.h"
#include "ui/scenes/services/view_state.h"

#include "project/mapping_table.h"
#include "project/midi_command.h"
#include "project/song.h"
#include "project/track_kind.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace deluge::ui::scenes::services {

using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

constexpr deluge::ui::widgets::core::LedState kOff{deluge::ui::widgets::core::LedState::Kind::Off, 0, 0};
constexpr deluge::ui::widgets::core::LedState kOn{deluge::ui::widgets::core::LedState::Kind::On, 0, 0};

constexpr uint32_t kFlashMs = MidiLearn::kFlashMs;

/// @brief `LedId` for a `ButtonId`, exactly as `global_controls.cpp` spells it: the Deluge profile
///        numbers its LEDs by their button (`ui/shell/profiles/deluge.h`'s `kDelugeLedByButton`).
/// @param b The button.
/// @return Its LED id.
constexpr deluge::ui::widgets::core::LedId led(ButtonId b) {
	return static_cast<deluge::ui::widgets::core::LedId>(static_cast<uint8_t>(b));
}

/// @brief Whether @p device is one the installed MIDI devices port knows.
bool known(const bind::Ports* ports, midi::DeviceId device) {
	const bind::MidiDevices* const midi = bind::port_of(ports, &bind::Ports::midi);
	return midi != nullptr && midi->settings(device).has_value();
}

/// @brief How a song names @p device, through the installed MIDI devices port.
/// @return Its reference; none with no port, or for a device the port does not know or a song cannot name.
std::optional<project::MidiDeviceRef> reference_of(const bind::Ports* ports, midi::DeviceId device) {
	const bind::MidiDevices* const midi = bind::port_of(ports, &bind::Ports::midi);
	return midi != nullptr ? midi->reference(device) : std::nullopt;
}

/// @brief Whether @p kind is one of the three buttons' global commands.
constexpr bool is_button(LearnKind kind) {
	return kind == LearnKind::PLAY_BUTTON || kind == LearnKind::RECORD_BUTTON || kind == LearnKind::TAP_TEMPO_BUTTON;
}

} // namespace

bool MidiLearn::global_command_learned(project::GlobalCommand command) const {
	const bind::MidiDevices* const midi = bind::port_of(ports_, &bind::Ports::midi);
	return midi != nullptr && midi->global_command(command).has_value();
}

// -- `LearnTarget` -------------------------------------------------------------------------------

LearnTarget LearnTarget::section(uint8_t section) {
	// `View::sectionMidiLearnPadPressed`: the song's mapping table holds the section's launch command.
	return LearnTarget{
	    .kind = LearnKind::SECTION, .mapping = project::SectionLaunchTarget{section}, .section_index = section};
}

LearnTarget LearnTarget::clip(deluge::project::ClipAddress clip) {
	// `View::clipStatusMidiLearnPadPressed` (`gui/views/view.cpp`).
	return LearnTarget{.kind = LearnKind::CLIP, .mapping = deluge::project::ClipMuteTarget{clip.clip_id}};
}

LearnTarget LearnTarget::note_row_mute(std::optional<uint32_t> drum_id) {
	// `View::noteRowMuteMidiLearnPadPressed` (`gui/views/view.cpp`). Legacy dereferences the row's drum unguarded;
	// every call site has already refused a drumless row (`InstrumentClipView::commandLearnMutePad` in
	// `gui/views/instrument_clip_view.cpp`), so the guard here is the same refusal spelled where the target is built.
	if (!drum_id) {
		return LearnTarget{};
	}
	return LearnTarget{.kind = LearnKind::NOTEROW_MUTE, .mapping = deluge::project::DrumMuteTarget{*drum_id}};
}

LearnTarget LearnTarget::drum_input(uint32_t drum_id) {
	// `View::drumMidiLearnPadPressed` (`gui/views/view.cpp`).
	return LearnTarget{.kind = LearnKind::DRUM_INPUT, .mapping = deluge::project::LaneInputTarget{drum_id}};
}

LearnTarget LearnTarget::instrument_input(uint32_t output_id) {
	// `View::instrumentMidiLearnPadPressed` (`gui/views/view.cpp`).
	return LearnTarget{.kind = LearnKind::INSTRUMENT_INPUT, .mapping = deluge::project::TrackInputTarget{output_id}};
}

LearnTarget LearnTarget::global_command(project::GlobalCommand command) {
	// `View::buttonAction`'s three learn arms (`gui/views/view.cpp`): the button's session kind and the device's
	// global command.
	LearnKind kind = LearnKind::NONE;
	switch (command) {
	case project::GlobalCommand::PLAY:
		kind = LearnKind::PLAY_BUTTON;
		break;
	case project::GlobalCommand::RECORD:
		kind = LearnKind::RECORD_BUTTON;
		break;
	case project::GlobalCommand::TAP:
		kind = LearnKind::TAP_TEMPO_BUTTON;
		break;
	default:
		// The other nine global commands are learned through the settings menu's
		// `midi_command_*` nodes (`ui/scenes/menu/nodes/midi_command.cpp`), never through a button
		// held under learn -- legacy's `View::buttonAction` has arms for exactly these three.
		return LearnTarget{};
	}
	return LearnTarget{.kind = kind, .global = command};
}

// -- The mode ------------------------------------------------------------------------------------

void MidiLearn::begin() {
	// `View::startMIDILearn` (`gui/views/view.cpp`). Its guard is
	// `currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_MIDI_LEARN`: a press while some
	// *other* mode is running is refused outright, and re-entering while already in learn is a
	// deliberate no-op-shaped reset (it clears the held thing).
	if (other_mode_active_ && !active_) {
		return;
	}
	pressed_ = LearnTarget{}; // `thingPressedForMidiLearn = MidiLearn::NONE`
	should_save_settings_ = false;
	active_ = true;
	flash_step(); // `midiLearnFlash()`
	              // `indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1)` -- declarative here; see
	              // `leds()`.
}

void MidiLearn::end() {
	// `View::endMIDILearn` (`gui/views/view.cpp`).
	// The inner guard: rarely this is called during the audio routine, which never writes flash.
	// With no audio port the routine reads as idle, and with no device actions port nothing is saved.
	if (should_save_settings_) {
		const bind::AudioFacts* const audio = bind::port_of(ports_, &bind::Ports::audio);
		if (audio == nullptr || !audio->audio_busy()) {
			if (bind::DeviceActions* const to = bind::port_of(ports_, &bind::Ports::device_actions)) {
				to->save_settings();
			}
		}
	}
	flash_remaining_ms_.reset(); // `unsetTimer(TimerName::MIDI_LEARN_FLASH)`
	flash_on_ = false;
	// `getRootUI()->midiLearnFlash()` is the root view's grid repaint. Here that is
	// `timer_repaints()`'s grid claim, which `Shell::tick` has already armed for the deadline this
	// call is cancelling -- the gate fires once more and the scenes repaint with `flash_on()`
	// false, which is exactly what legacy's call buys.
	active_ = false;

	// `playbackHandler.setLedStates()` + `setLedState(LEARN, false)`: declarative, see
	// `leds()`. Not clearing `pressed_` here is legacy's too -- `endMIDILearn` leaves
	// `thingPressedForMidiLearn` alone and the next `startMIDILearn` resets it.
}

// -- The pressed thing ---------------------------------------------------------------------------

void MidiLearn::end_press_session(LearnTarget new_thing) {
	// `View::endMidiLearnPressSession` (`gui/views/view.cpp`): its switch calls `playbackHandler.setLedStates()` for
	// the three button kinds and nothing for the other six. In a declarative LED model that call is not a state change
	// at all: `leds()` recomposes from scratch every tick and stops declaring the flash the moment `pressed_.kind`
	// stops naming a button. Nothing to port, and it is named rather than dropped.
	pressed_ = new_thing; // `View::endMidiLearnPressSession`'s `learnedThing = nullptr` and the new kind, as one write
	unported_[static_cast<size_t>(UnportedCall::SpecificDeviceHook)]++;
}

void MidiLearn::release_pressed_thing() {
	// The `else if (thingPressedForMidiLearn == K)` body every caller shares. The `if
	// (deleteMidiCommandOnRelease)` test is `View::buttonAction`'s TAP_TEMPO arm and each of the `*MidiLearnPadPressed`
	// functions; what varies between the six is only what runs *alongside* the `clear()`.
	if (delete_on_release_ && pressed_.kind != LearnKind::NONE) {
		switch (pressed_.kind) {
		case LearnKind::PLAY_BUTTON:
		case LearnKind::RECORD_BUTTON:
		case LearnKind::TAP_TEMPO_BUTTON:
			// The three button arms, and only they, ask for the settings write that
			// `endMIDILearn` then performs. Clearing a *pad's* command is a song edit, which the
			// song save owns; clearing a global command is a settings edit.
			if (bind::LearnSink* const to = bind::port_of(ports_, &bind::Ports::learn)) {
				to->learn_global(*pressed_.global, std::nullopt);
			}
			should_save_settings_ = true;
			break;
		case LearnKind::INSTRUMENT_INPUT:
			// `View::instrumentMidiLearnPadPressed`: its `clearMelodicInstrumentMonoExpressionIfPossible()` runs
			// **before** the clear ("in case it gets stuck") and is not ported; see the class doc.
			// The unlearn marks the instrument edited, as `View::instrumentMidiLearnPadPressed` does.
			unported_[static_cast<size_t>(UnportedCall::ClearMonoExpression)]++;
			commit(std::nullopt);
			break;
		default:
			// `CLIP`, `NOTEROW_MUTE`, `SECTION`: the bare clear. `DRUM_INPUT`'s (`View::drumMidiLearnPadPressed`) marks
			// the kit edited, which its unlearn does.
			commit(std::nullopt);
			break;
		}
	}
	end_press_session(LearnTarget{}); // as the TAP_TEMPO arm and each `*MidiLearnPadPressed` release do
}

void MidiLearn::thing_pressed(LearnTarget target, bool on) {
	// A `NONE` target is "there is nothing learnable under that pad", which two of the factories can
	// answer (`note_row_mute` for a drumless row, `global_command` for a command with no button).
	// Legacy spells the same refusal at the CALL SITE instead -- `InstrumentClipView::commandLearnMutePad` returns
	// early on `!noteRow->drum` (`gui/views/instrument_clip_view.cpp`) rather than calling
	// the overload -- so this is the same behaviour with the test moved next to the thing it is
	// about.
	if (target.kind == LearnKind::NONE) {
		return;
	}
	if (on) {
		end_press_session(
		    target); // `View::endMidiLearnPressSession` in `gui/views/view.cpp` / `View::buttonAction`'s TAP_TEMPO arm
		delete_on_release_ = true; // as the TAP_TEMPO arm and `View::sectionMidiLearnPadPressed` do
		// `learnedThing = &…` is `end_press_session`'s own assignment of the target.
		return;
	}
	// `else if (thingPressedForMidiLearn == MidiLearn::SECTION)`. The comparison is on the
	// KIND, not on the target: legacy has one `thingPressedForMidiLearn`, and a release of a
	// different kind of thing falls through doing nothing.
	if (pressed_.kind == target.kind) {
		release_pressed_thing();
	}
}

// -- The commit ----------------------------------------------------------------------------------

void MidiLearn::commit(std::optional<project::MidiCommand> command, std::optional<bind::LearnInput> input) {
	// `View::commitLearnedThing`: a song binding, through the learn port. A global command is learned by its caller.
	if (!pressed_.mapping) {
		return;
	}
	if (bind::LearnSink* const to = bind::port_of(ports_, &bind::Ports::learn)) {
		(void)to->learn(*pressed_.mapping, command && command->contains_something() ? command : std::nullopt, input);
	}
}

void MidiLearn::note_on_received(deluge::midi::DeviceId device, int32_t channel_or_zone, int32_t note,
                                 int32_t /*velocity*/) {
	// `View::noteOnReceivedForMidiLearn` (`gui/views/view.cpp`).
	if (pressed_.kind == LearnKind::NONE) {
		return;
	}
	delete_on_release_ = false; // Something was learned, so the release must not wipe it.

	// A note learned on an input brings its device's bend ranges and velocity default
	// (`View::noteOnReceivedForMidiLearn`'s `DRUM_INPUT` and `INSTRUMENT_INPUT` arms commit with `inputFromNote`),
	// which the learn takes with it.
	std::optional<bind::LearnInput> input;
	switch (pressed_.kind) {
	case LearnKind::DRUM_INPUT:
		input = bind::LearnInput{.device = device, .channel_or_zone = channel_or_zone};
		break;
	case LearnKind::PLAY_BUTTON:
	case LearnKind::RECORD_BUTTON:
	case LearnKind::TAP_TEMPO_BUTTON:
		should_save_settings_ = true; // "no break" into the shared write
		break;
	case LearnKind::INSTRUMENT_INPUT:
		input = bind::LearnInput{.device = device, .channel_or_zone = channel_or_zone};
		// `clearMelodicInstrumentMonoExpressionIfPossible()` -- not ported, see the class doc.
		unported_[static_cast<size_t>(UnportedCall::ClearMonoExpression)]++;
		break;
	default:
		// `CLIP`, `NOTEROW_MUTE`, `SECTION`: straight to the shared write (`View::noteOnReceivedForMidiLearn`'s
		// `default`).
		break;
	}

	// The write every arm makes -- the `default` arm's `commitLearnedThing` for the shared path, and again
	// inside the `INSTRUMENT_INPUT` arm, which is the same three fields in a different order. The note is
	// legacy's "used for low note in kits".
	const auto channel = static_cast<uint8_t>(channel_or_zone);
	const auto number = static_cast<uint8_t>(note);
	if (is_button(pressed_.kind)) {
		// A global command is the device's, not the song's. A device the port does not know is learned as no
		// device, as legacy's null cable is.
		if (bind::LearnSink* const to = bind::port_of(ports_, &bind::Ports::learn)) {
			to->learn_global(
			    *pressed_.global,
			    bind::LearnedBinding{.device = known(ports_, device) ? std::optional{device} : std::nullopt,
			                         .channel_or_zone = channel,
			                         .note_or_cc = number});
		}
		return;
	}
	commit(
	    project::MidiCommand{.channel_or_zone = channel, .note_or_cc = number, .device = reference_of(ports_, device)},
	    input);
}

void MidiLearn::cc_received(deluge::midi::DeviceId device, int32_t channel, int32_t cc, int32_t value) {
	// `View::ccReceivedForMIDILearn` (`gui/views/view.cpp`).
	if (pressed_.kind == LearnKind::NONE) {
		return;
	}
	delete_on_release_ = false;

	if (pressed_.kind == LearnKind::INSTRUMENT_INPUT) {
		// `getCurrentOutputType() == OutputType::MIDI_OUT`. Legacy asks the CURRENT output,
		// not the held instrument; the two are the same on every path that reaches here (the pad
		// that set the target is on the current clip's output) but the question is legacy's, so
		// the port asks legacy's.
		const MenuView& current = services::view_state().current_view();
		const bool current_output_is_midi = current.clip && current.kind == project::TrackKind::MIDI_OUT;
		// The input the track holds now, from the song's mapping table.
		const project::Mapping* const held =
		    model_ != nullptr && pressed_.mapping ? model_->mappings.find(*pressed_.mapping) : nullptr;
		project::MidiCommand learned = held != nullptr ? held->source : project::MidiCommand{};
		if (current_output_is_midi && learned.channel_or_zone == project::MidiCommand::kNone) {
			// Legacy's comment: but only if the user hasn't already started learning MPE stuff, or
			// regular note-ons. The learn marks the instrument edited, as `View::ccReceivedForMIDILearn` does.
			learned.device = reference_of(ports_, device);
			learned.channel_or_zone = static_cast<uint8_t>(channel);
			commit(learned);
		}
		return;
	}

	// For everything else, a non-zero CC is a note-on with the CC bias applied.
	if (value != 0) {
		note_on_received(device, channel + project::kFirstCcChannel, cc, 127);
	}
}

uint32_t MidiLearn::unported_calls(UnportedCall which) const {
	const size_t i = static_cast<size_t>(which);
	return i < static_cast<size_t>(UnportedCall::kCount) ? unported_[i] : 0;
}

// -- Routing -------------------------------------------------------------------------------------

Result MidiLearn::handle(const UiEvent& event, const UiContext& /*ctx*/) {
	// The MIDI kinds first, and deliberately ABOVE the root-scene gate below. That gate is the
	// `View::buttonAction` reachability rule and nothing else (`Scene::is_root`); legacy's learn
	// dispatchers reach `View`'s learn from `PlaybackHandler` with no root-UI test anywhere on the path, so applying
	// the button gate to them would invent a restriction legacy does not have.
	//
	// In learn mode only, as the dispatchers reach `View` only then; there, a note-on, a CC and a program change are
	// used up whatever they learned (`View::pcReceivedForMIDILearn` learns nothing), and a pitch bend is never used. A
	// null device is the "no message" value `UiEvent` documents; it, and any device the MIDI devices port does not know
	// (every device, with no port installed), is dropped.
	switch (event.kind) {
	case UiEvent::Kind::MidiNoteOn:
	case UiEvent::Kind::MidiCc:
	case UiEvent::Kind::MidiProgramChange: {
		if (!active_ || !known(ports_, event.device)) {
			return Result::unhandled();
		}
		if (event.kind == UiEvent::Kind::MidiNoteOn) {
			note_on_received(event.device, event.channel, event.data1, event.data2);
		}
		else if (event.kind == UiEvent::Kind::MidiCc) {
			cc_received(event.device, event.channel, event.data1, event.data2);
		}
		return Result{.handled = true};
	}
	case UiEvent::Kind::MidiPitchBend:
		return Result::unhandled();
	default:
		break;
	}

	if (event.kind != UiEvent::Kind::Button) {
		return {};
	}
	// The `View::buttonAction` reachability gate, identical to `GlobalControls::handle`'s and for
	// the identical reason -- see the class doc, and `Scene::is_root`'s two-line proof. This
	// service is routed AFTER `global_controls`, which answers `ViewArm::NotAnArm` for a non-root
	// scene and therefore runs its own tail; without this gate a `LEARN` press inside a menu would
	// reach here and toggle a mode legacy would have handed to `SoundEditor::buttonAction`
	// (`SoundEditor::buttonAction`) instead.
	if (!stack_.input_top().is_root()) {
		return Result::unhandled();
	}

	switch (event.button) {
	case ButtonId::Learn:
		// The LEARN arm of `View::buttonAction`. Claimed unconditionally -- legacy's arm has no mode guard, and
		// returns `DEALT_WITH` for it whether the press started or ended the mode.
		if (event.on) {
			begin();
		}
		else {
			end();
		}
		return Result{.handled = true};

	case ButtonId::TapTempo:
		// `View::buttonAction`'s TAP_TEMPO learn branch. Its `else if` twin is `GlobalControls`'s,
		// and the two are disjoint by legacy's own `if`/`else`.
		if (!active_) {
			return Result::unhandled();
		}
		thing_pressed(LearnTarget::global_command(project::GlobalCommand::TAP), event.on);
		return Result{.handled = true};

	case ButtonId::Play:
		// The PLAY-under-learn arm of `View::buttonAction`, guarded `&& currentUIMode == UI_MODE_MIDI_LEARN`.
		if (!active_) {
			return Result::unhandled();
		}
		thing_pressed(LearnTarget::global_command(project::GlobalCommand::PLAY), event.on);
		return Result{.handled = true};

	case ButtonId::Record:
		// The RECORD-under-learn arm, same shape.
		if (!active_) {
			return Result::unhandled();
		}
		thing_pressed(LearnTarget::global_command(project::GlobalCommand::RECORD), event.on);
		return Result{.handled = true};

	default:
		return Result::unhandled();
	}
}

// -- LEDs ----------------------------------------------------------------------------------------

void MidiLearn::leds(deluge::ui::widgets::core::LedFrame& frame) {
	if (!active_) {
		// `View::endMIDILearn`'s two LED calls (`gui/views/view.cpp`), declaratively: stop declaring
		// PLAY/RECORD/TAP_TEMPO so `GlobalControls`'s `PlaybackHandler::setLedStates` port -- which
		// composed a moment earlier -- is the last writer again, and put LEARN out.
		//
		// LEARN is declared rather than left alone because legacy really does write it here, and an
		// undeclared LED keeps its last sent value (`shell::led_send_needed`). `MenuScene::leds`
		// declares LEARN too and outranks this: `LedCompose::compose` runs every service before
		// every scene (`ui/shell/leds.h`).
		(void)frame.set(led(ButtonId::Learn), kOff);
		return;
	}

	// `indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1)` (`View::startMIDILearn` in `gui/views/view.cpp`):
	// forever, on the fast blink every `blinkingType` 1 LED shares -- legacy drives this LED from that blinker phase,
	// NOT from `midiLearnFlashOn`.
	(void)frame.set(led(ButtonId::Learn),
	                deluge::ui::widgets::core::LedState::shared(deluge::ui::widgets::core::BlinkRate::Fast));

	// `View::midiLearnFlash`'s three writes (`gui/views/view.cpp`). Each is declared only when its
	// condition holds, so an unlearned, unheld button keeps whatever `GlobalControls` declared --
	// which is exactly what legacy's `if` does by simply not calling `setLedState`.
	const deluge::ui::widgets::core::LedState phase = flash_on_ ? kOn : kOff;
	if (global_command_learned(project::GlobalCommand::PLAY) || pressed_.kind == LearnKind::PLAY_BUTTON) {
		(void)frame.set(led(ButtonId::Play), phase);
	}
	if (global_command_learned(project::GlobalCommand::RECORD) || pressed_.kind == LearnKind::RECORD_BUTTON) {
		(void)frame.set(led(ButtonId::Record), phase);
	}
	if (global_command_learned(project::GlobalCommand::TAP) || pressed_.kind == LearnKind::TAP_TEMPO_BUTTON) {
		(void)frame.set(led(ButtonId::TapTempo), phase);
	}
}

// -- Time ----------------------------------------------------------------------------------------

void MidiLearn::flash_step() {
	// `View::midiLearnFlash`'s own two lines (`gui/views/view.cpp`): toggle, then re-arm at
	// `kFastFlashTime`. The `getRootUI()->midiLearnFlash()` is the grid repaint,
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

} // namespace deluge::ui::scenes::services
