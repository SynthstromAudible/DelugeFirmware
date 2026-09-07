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
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/flash_storage.h"

#include <cstddef>
#include <cstdint>

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
/// `midiEngine.globalMIDICommands[util::to_underlying(c)]` (`view.cpp:155`, `:207`, `:223`,
/// `:804`, `:808`, `:812`), spelled with a `static_cast` because `util/` is outside this layer's
/// include allowance (`scripts/check_layer_rules.py`'s `scenes-includes-outside`) --
/// `menu/nodes/midi_command.cpp`'s `slot_for` makes the identical substitution for the identical
/// reason.
/// @param c The command.
/// @return Its learned-MIDI slot.
::LearnedMIDI& slot_for(GlobalMIDICommand c) {
	return midiEngine.globalMIDICommands[static_cast<size_t>(c)];
}

/// @brief Does @p c already carry a learned message? `containsSomething()` (`view.cpp:804`).
/// @param c The command.
/// @return Whether something is learned to it.
bool global_command_learned(GlobalMIDICommand c) {
	return slot_for(c).containsSomething();
}

} // namespace

// -- `LearnTarget` -------------------------------------------------------------------------------

LearnTarget LearnTarget::section(::Song& song, uint8_t section) {
	// `View::sectionMidiLearnPadPressed` (`view.cpp:520-522`).
	return LearnTarget{.kind = ::MidiLearn::SECTION, .thing = &song.sections[section].launchMIDICommand};
}

LearnTarget LearnTarget::clip(::Clip& clip) {
	// `View::clipStatusMidiLearnPadPressed` (`view.cpp:534-536`).
	return LearnTarget{.kind = ::MidiLearn::CLIP, .thing = &clip.muteMIDICommand};
}

LearnTarget LearnTarget::note_row_mute(::NoteRow& note_row) {
	// `View::noteRowMuteMidiLearnPadPressed` (`view.cpp:548-550`). Legacy dereferences
	// `whichNoteRow->drum` unguarded; every call site has already refused a drumless row
	// (`gui/views/instrument_clip_view.cpp:2253-2255`), so the guard here is the same refusal
	// spelled where the target is built rather than where the pad is read.
	if (note_row.drum == nullptr) {
		return LearnTarget{};
	}
	return LearnTarget{.kind = ::MidiLearn::NOTEROW_MUTE, .thing = &note_row.drum->muteMIDICommand};
}

LearnTarget LearnTarget::drum_input(::Drum& drum, ::Kit& kit) {
	// `View::drumMidiLearnPadPressed` (`view.cpp:562-567`).
	return LearnTarget{.kind = ::MidiLearn::DRUM_INPUT, .thing = &drum.midiInput, .drum = &drum, .kit = &kit};
}

LearnTarget LearnTarget::instrument_input(::Instrument& instrument) {
	// `View::instrumentMidiLearnPadPressed` (`view.cpp:581-584`).
	return LearnTarget{
	    .kind = ::MidiLearn::INSTRUMENT_INPUT, .thing = &instrument.midiInput, .instrument = &instrument};
}

LearnTarget LearnTarget::global_command(GlobalMIDICommand command) {
	// `View::buttonAction`'s three learn arms (`view.cpp:154-155`, `:206-207`, `:222-223`): the
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
	// `View::startMIDILearn` (`view.cpp:472-480`). The guard at `:473` is
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
	// `View::endMIDILearn` (`view.cpp:482-500`).
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
	// `View::endMidiLearnPressSession` (`view.cpp:597-621`).
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
	// (deleteMidiCommandOnRelease)` test is `view.cpp:160`, `:525`, `:539`, `:553`, `:571`, `:588`;
	// what varies between the six is only what runs *alongside* the `clear()`.
	if (delete_on_release_ && pressed_.thing != nullptr) {
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
		case ::MidiLearn::DRUM_INPUT:
			// `:572-573`.
			pressed_.thing->clear();
			if (::Instrument* const instrument = current_instrument(); instrument != nullptr) {
				instrument->beenEdited(false);
			}
			break;
		case ::MidiLearn::INSTRUMENT_INPUT:
			// `:589-591`. The `clearMelodicInstrumentMonoExpressionIfPossible()` at `:589` runs
			// **before** the clear ("in case it gets stuck") and is not ported; see the class doc.
			unported_[static_cast<size_t>(UnportedCall::ClearMonoExpression)]++;
			pressed_.thing->clear();
			// `:591` `instrument->beenEdited(false)` is legacy's *argument*, not
			// `instrumentPressedForMIDILearn`. This uses the held target, which is the same object
			// on every path that gets here (the kind matched, so the same pad is being released)
			// and is the one whose `LearnedMIDI` was just cleared. Stated because the two spellings
			// are not identical by construction, only in practice.
			if (pressed_.instrument != nullptr) {
				pressed_.instrument->beenEdited(false);
			}
			break;
		default:
			// `CLIP` (`:540`), `NOTEROW_MUTE` (`:554`), `SECTION` (`:526`): the bare clear.
			pressed_.thing->clear();
			break;
		}
	}
	end_press_session(LearnTarget{}); // `:164`, `:528`, `:542`, `:556`, `:575`, `:593`
}

void MidiLearn::thing_pressed(LearnTarget target, bool on) {
	// A `NONE` target is "there is nothing learnable under that pad", which two of the factories can
	// answer (`note_row_mute` for a drumless row, `global_command` for a command with no button).
	// Legacy spells the same refusal at the CALL SITE instead -- `commandLearnMuteRow` returns
	// early on `!noteRow->drum` (`gui/views/instrument_clip_view.cpp:2253-2255`) rather than calling
	// the overload -- so this is the same behaviour with the test moved next to the thing it is
	// about.
	if (target.kind == ::MidiLearn::NONE) {
		return;
	}
	if (on) {
		end_press_session(target); // `view.cpp:520` / `:154`
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

::Instrument* MidiLearn::current_instrument() const {
	// `getCurrentInstrument()` (`model/song/song.cpp:120-133`), resolved from `song_`.
	if (song_ == nullptr) {
		return nullptr;
	}
	::Clip* const clip = song_->getCurrentClip();
	if (clip == nullptr || clip->output == nullptr) {
		return nullptr;
	}
	const OutputType type = clip->output->type;
	if (type == OutputType::AUDIO || type == OutputType::NONE) {
		return nullptr;
	}
	return static_cast<::Instrument*>(clip->output);
}

void MidiLearn::commit_drum_extras(::MIDICable& cable, int32_t channel_or_zone) {
	// `View::noteOnReceivedForMidiLearn`'s `DRUM_INPUT` arm (`view.cpp:629-660`), which ends by
	// jumping into the shared three-field write -- so everything here is what happens *before* it.
	if (::Instrument* const instrument = current_instrument(); instrument != nullptr) {
		instrument->beenEdited(false); // `:631`
	}

	// `:633-641`. Legacy's own comment: this logic is duplicated in `NoteRow::setDrum()`.
	int32_t new_bend_range = 0;
	const int32_t zone = channel_or_zone - MIDI_CHANNEL_MPE_LOWER_ZONE;
	if (zone >= 0) { // `:636` MPE input
		new_bend_range = cable.mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL];
	}
	else { // `:639` regular MIDI input
		new_bend_range = cable.inputChannels[channel_or_zone].bendRange;
	}

	if (new_bend_range != 0 && song_ != nullptr && pressed_.drum != nullptr) { // `:643`
		// `:644` `getCurrentInstrumentClip()->getNoteRowForDrum(drumPressedForMIDILearn)`.
		::Clip* const clip = song_->getCurrentClip();
		auto* const instrument_clip =
		    (clip != nullptr && clip->type == ClipType::INSTRUMENT) ? static_cast<::InstrumentClip*>(clip) : nullptr;
		::NoteRow* const note_row =
		    instrument_clip != nullptr ? instrument_clip->getNoteRowForDrum(pressed_.drum) : nullptr;
		if (note_row != nullptr) { // `:645`
			ExpressionParamSet* const expression_params =
			    note_row->paramManager.getOrCreateExpressionParamSet(true);                    // `:646`
			if (expression_params != nullptr && !expression_params->params[0].isAutomated()) { // `:647-648`
				expression_params->bendRanges[BEND_RANGE_FINGER_LEVEL] = new_bend_range;       // `:649`
			}
		}
	}

	// `:655-658`.
	if (song_ != nullptr && pressed_.drum != nullptr && pressed_.drum->type == DrumType::SOUND) {
		song_->grabVelocityToLevelFromMIDICableAndSetupPatchingForAllParamManagersForDrum(
		    cable, static_cast<SoundDrum*>(pressed_.drum), pressed_.kit);
	}
}

void MidiLearn::commit_instrument_extras(::MIDICable& cable, int32_t channel_or_zone) {
	// `View::noteOnReceivedForMidiLearn`'s `INSTRUMENT_INPUT` arm (`view.cpp:677-740`). Unlike the
	// drum arm this one does NOT jump to the shared write -- it repeats the three fields itself at
	// `:730-732` -- so `note_on_received` runs the shared write for it and this holds only the
	// extras.
	//
	// @note `isMPEZone:` at `:686` is a label with no `goto` anywhere in the tree. It is dead
	//       syntax, not a second entry point, so the branch is a plain `if`/`else` here.
	::Instrument* const instrument = pressed_.instrument;
	if (instrument == nullptr || song_ == nullptr) {
		return;
	}
	// `:681-682`. Could be null, e.g. for CVInstruments with no Clips -- legacy's own comment.
	ParamManager* const param_manager = instrument->getParamManager(song_);

	uint8_t new_bend_ranges[2] = {0, 0};
	if (channel_or_zone == MIDI_CHANNEL_MPE_LOWER_ZONE || channel_or_zone == MIDI_CHANNEL_MPE_UPPER_ZONE) { // `:685`
		const int32_t zone = channel_or_zone - MIDI_CHANNEL_MPE_LOWER_ZONE;                                 // `:689`
		new_bend_ranges[BEND_RANGE_MAIN] = cable.mpeZoneBendRanges[zone][BEND_RANGE_MAIN];                  // `:691`
		new_bend_ranges[BEND_RANGE_FINGER_LEVEL] = cable.mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL];  // `:692`

		if (new_bend_ranges[BEND_RANGE_FINGER_LEVEL] != 0) {                                // `:694`
			auto* const clip = static_cast<::InstrumentClip*>(instrument->getActiveClip()); // `:695`
			if (clip == nullptr || !clip->hasAnyPitchExpressionAutomationOnNoteRows()) {    // `:696`
				if (param_manager != nullptr) {                                             // `:697`
					ExpressionParamSet* const expression_params = param_manager->getOrCreateExpressionParamSet();
					if (expression_params != nullptr) { // `:699`
						expression_params->bendRanges[BEND_RANGE_FINGER_LEVEL] =
						    new_bend_ranges[BEND_RANGE_FINGER_LEVEL]; // `:700-701`
					}
				}
			}
		}
	}
	else {
		// `:709-713`: not MPE, so take the stored bend range for the input MIDI channel.
		new_bend_ranges[BEND_RANGE_MAIN] = cable.inputChannels[channel_or_zone].bendRange;
	}

	if (new_bend_ranges[BEND_RANGE_MAIN] != 0 && param_manager != nullptr) {                          // `:715-716`
		ExpressionParamSet* const expression_params = param_manager->getOrCreateExpressionParamSet(); // `:717`
		if (expression_params != nullptr && !expression_params->params[0].isAutomated()) {            // `:718`
			expression_params->bendRanges[BEND_RANGE_MAIN] = new_bend_ranges[BEND_RANGE_MAIN];        // `:719`
		}
	}
	// `:723-724`: in a perfect world we would also update `CVInstrument::cachedBendRanges[]`.
	// Legacy does not; neither does this.

	// `:728` `clearMelodicInstrumentMonoExpressionIfPossible()` -- not ported, see the class doc.
	unported_[static_cast<size_t>(UnportedCall::ClearMonoExpression)]++;
}

void MidiLearn::note_on_received(::MIDICable& cable, int32_t channel_or_zone, int32_t note, int32_t /*velocity*/) {
	// `View::noteOnReceivedForMidiLearn` (`view.cpp:623-743`).
	if (pressed_.kind == ::MidiLearn::NONE) { // `:624`
		return;
	}
	delete_on_release_ = false; // `:625` -- something was learned, so the release must not wipe it.

	switch (pressed_.kind) { // `:627`
	case ::MidiLearn::DRUM_INPUT:
		commit_drum_extras(cable, channel_or_zone); // `:629-660`, then `goto` the shared write
		break;
	case ::MidiLearn::PLAY_BUTTON:
	case ::MidiLearn::RECORD_BUTTON:
	case ::MidiLearn::TAP_TEMPO_BUTTON:
		should_save_settings_ = true; // `:663-666`, "no break" into the shared write
		break;
	case ::MidiLearn::INSTRUMENT_INPUT:
		commit_instrument_extras(cable, channel_or_zone); // `:677-728`
		break;
	default:
		// `CLIP`, `NOTEROW_MUTE`, `SECTION`: straight to the shared write (`:669`'s `default`).
		break;
	}

	if (pressed_.thing == nullptr) {
		return;
	}
	// The write every arm makes -- `:672-674` for the shared path, and `:730-732` again inside the
	// `INSTRUMENT_INPUT` arm, which is the same three fields in a different order.
	pressed_.thing->cable = &cable;
	pressed_.thing->channelOrZone = channel_or_zone;
	pressed_.thing->noteOrCC = note; // legacy's comment at `:732`: used for low note in kits

	if (pressed_.kind == ::MidiLearn::INSTRUMENT_INPUT && pressed_.instrument != nullptr) {
		pressed_.instrument->beenEdited(false); // `:733` -- legacy's own comment: "Why again?"
		if (song_ != nullptr && pressed_.instrument->type == OutputType::SYNTH) { // `:735`
			song_->grabVelocityToLevelFromMIDICableAndSetupPatchingForAllParamManagersForInstrument(
			    cable, static_cast<SoundInstrument*>(pressed_.instrument)); // `:736-737`
		}
	}
}

void MidiLearn::cc_received(::MIDICable& cable, int32_t channel, int32_t cc, int32_t value) {
	// `View::ccReceivedForMIDILearn` (`view.cpp:767-794`).
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
		if (current_output_is_midi && pressed_.thing != nullptr
		    && pressed_.thing->channelOrZone == MIDI_CHANNEL_NONE) { // `:778`
			// Legacy's comment: but only if the user hasn't already started learning MPE stuff, or
			// regular note-ons.
			pressed_.thing->cable = &cable;          // `:779`
			pressed_.thing->channelOrZone = channel; // `:780`
			if (::Instrument* const instrument = current_instrument(); instrument != nullptr) {
				instrument->beenEdited(false); // `:781`
			}
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
		// `view.cpp:185-196`. Claimed unconditionally -- legacy's arm has no mode guard, and
		// `:469` returns `DEALT_WITH` for it whether the press started or ended the mode.
		if (event.on) {
			begin(); // `:191`
		}
		else {
			end(); // `:194`
		}
		return Result{.handled = true};

	case ButtonId::TapTempo:
		// `view.cpp:146-166`'s learn branch. Its `else if` twin at `:167` is `GlobalControls`'s,
		// and the two are disjoint by legacy's own `if`/`else`.
		if (!active_) {
			return Result::unhandled();
		}
		thing_pressed(LearnTarget::global_command(GlobalMIDICommand::TAP), event.on); // `:152-165`
		return Result{.handled = true};

	case ButtonId::Play:
		// `view.cpp:199-212`, guarded `&& currentUIMode == UI_MODE_MIDI_LEARN`.
		if (!active_) {
			return Result::unhandled();
		}
		thing_pressed(LearnTarget::global_command(GlobalMIDICommand::PLAY), event.on);
		return Result{.handled = true};

	case ButtonId::Record:
		// `view.cpp:215-228`, same shape.
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
		// `View::endMIDILearn`'s two LED calls (`view.cpp:498-499`), declaratively: stop declaring
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

	// `indicator_leds::blinkLed(IndicatorLED::LEARN, 255, 1)` (`view.cpp:478`): forever, at
	// `blinkingType` 1's `kFastFlashTime` period (`hid/led/indicator_leds.cpp:81-83`). A real
	// `Kind::Blink` because legacy drives this LED from its own blinker phase, NOT from
	// `midiLearnFlashOn`.
	(void)frame.set(led(ButtonId::Learn),
	                deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, static_cast<uint16_t>(kFlashMs), 0});

	// `View::midiLearnFlash`'s three writes (`view.cpp:804-815`). Each is declared only when its
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
	// `View::midiLearnFlash`'s own two lines (`view.cpp:797-798`): toggle, then re-arm at
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
