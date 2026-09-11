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
/// @brief `GlobalControls`. See `global_controls.h` for the interface, the chain argument, the
///        MIDI-learn boundary and the full list of what is deliberately not ported.

#include "scenes/services/global_controls.h"

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/registry.h"

#include "shell/input_lowering.h"

#include "gui/l10n/strings.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"

#include <cstddef>
#include <cstdint>
#include <span>

/// @name The two pending-command globals the `BACK` arm writes
///
/// Declared here rather than included, exactly as `View::buttonAction` itself declares them
/// (`gui/views/view.cpp:101`, `:138`): they are defined in `playback/playback_handler.cpp:94-95`
/// with no declaration in `playback_handler.h`, so a bare `extern` is the only spelling either
/// side has. (`nodes/settings.cpp`'s `extern uint32_t jcong;` is the same pattern, and
/// `scripts/check_layer_rules.py`'s `scenes-no-root-ui` comment names it as the expected shape.)
///
/// @warning The second one is declared `extern GlobalMIDICommand` at `view.cpp:138` and **defined
///          `int32_t`** at `playback_handler.cpp:95` -- a type mismatch across translation units.
///          It is benign only because `GlobalMIDICommand` has a negative enumerator (`NONE = -1`,
///          `definitions_cxx.hpp:584`) and so takes `int` as its underlying type, making the two
///          spellings the same size and the one value written (`GlobalMIDICommand::PLAYBACK_RESTART`
///          == 0, `view.cpp:417`) the same bits as the `0` the dead-code twin writes at `:436`.
///          Declared with the *defining* type here, and 0 written, rather than reproducing the pun.
/// @{
extern GlobalMIDICommand pendingGlobalMIDICommand;
extern int32_t pendingGlobalMIDICommandNumClustersWritten;
/// @}

namespace deluge::scenes::services {

using deluge::l10n::String;
using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

/// @brief One `l10n::String` as the `uint16_t` `Action::popup_string` carries, exactly as every
///        other scene in this tree spells it.
/// @param s The string.
/// @return Its id.
constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

constexpr deluge::ui::LedState kOff{deluge::ui::LedState::Kind::Off, 0, 0};
constexpr deluge::ui::LedState kOn{deluge::ui::LedState::Kind::On, 0, 0};

/// @brief `IndicatorLED`'s value for @p b, as the toolkit's opaque `LedId`.
///
/// `LedId` values are `ButtonId` values by the board profile's own contract
/// (`shell/profiles/deluge.h:19`), so this is the whole of the mapping.
/// @param b The button whose LED to name.
/// @return The LED id.
constexpr deluge::ui::LedId led(ButtonId b) {
	return static_cast<deluge::ui::LedId>(b);
}

/// @brief Append a `displayPopup` to @p r -- the port of `display->displayPopup(...)`, which
///        `scenes-no-display` forbids spelling directly.
/// @param r The result to append to.
/// @param s The string to show.
void popup(Result& r, String s) {
	(void)r.actions.push_back(
	    Action{.kind = Action::Kind::Popup, .surface = SurfaceRole::Main, .popup_string = sid(s)});
}

} // namespace

// -- The single UI mode -------------------------------------------------------------------------

void GlobalControls::count_unported(UnportedOpen which) {
	const size_t i = static_cast<size_t>(which);
	if (i < static_cast<size_t>(UnportedOpen::Count)) {
		unported_opens_[i]++;
	}
}

uint32_t GlobalControls::unported_opens_total() const {
	uint32_t total = 0;
	for (const uint32_t n : unported_opens_) {
		total += n;
	}
	return total;
}

GlobalControls::UiMode GlobalControls::effective_ui_mode() const {
	// Legacy has one `currentUIMode`; this reassembles it from its three owners. `MidiLearn` first
	// because that is the value `View::buttonAction`'s own arms test first (`view.cpp:149`), and
	// because `View::startMIDILearn` (`:473`) only enters the mode from `UI_MODE_NONE` or from
	// itself -- so a learn-active mode can never be one of the others.
	if (learn_.active()) {
		return UiMode::MidiLearn;
	}
	if (holding_save_) {
		return UiMode::HoldingSave;
	}
	if (holding_load_) {
		return UiMode::HoldingLoad;
	}
	return view_.ui_mode;
}

// -- `View::buttonAction` (`gui/views/view.cpp:140-487`) -----------------------------------------

bool GlobalControls::load_save_allowed(const UiContext& ctx) const {
	// `view.cpp:234-237` and its identical twin at `:279-282`. Note the guard wraps the WHOLE arm
	// body: when it fails, legacy does nothing at all and still falls through to `:469`'s
	// `return ActionResult::DEALT_WITH`, so the caller reports handled either way.
	if (ctx.held.has(ButtonId::Synth) || ctx.held.has(ButtonId::Kit) || ctx.held.has(ButtonId::Midi)
	    || ctx.held.has(ButtonId::Cv)) {
		return false;
	}
	// `:236` / `:281`: KEYBOARD only blocks it in performance view.
	return !(view_.root_is_performance && ctx.held.has(ButtonId::Keyboard));
}

void GlobalControls::handle_save(bool on, const UiContext& ctx, Result& result) {
	if (!load_save_allowed(ctx)) {
		return;
	}
	if (on) {
		// `:238-244`. Press down: only from a clean mode, and never with shift (which is the
		// SAVE-as-a-modifier gesture other UIs read).
		if (effective_ui_mode() == UiMode::None && !ctx.shift) {
			holding_save_ = true;          // `:240` `currentUIMode = UI_MODE_HOLDING_SAVE_BUTTON`
			save_pressed_ms_ = ctx.now_ms; // `:241`
			// `:242` `indicator_leds::setLedState(IndicatorLED::SAVE, true)` -- `leds()` recomputes
			// it from `holding_save_`.
		}
		return;
	}

	// `:246-271`. Press release.
	if (effective_ui_mode() != UiMode::HoldingSave) {
		// `:268-270`: from `UI_MODE_NONE` the SAVE LED is turned off, which `leds()` already does
		// every tick. Nothing else happens.
		return;
	}
	holding_save_ = false; // `:253` `currentUIMode = UI_MODE_NONE`
	if (static_cast<uint32_t>(ctx.now_ms - save_pressed_ms_) >= kShortPressMs) {
		// `:264-266`: a long press just drops the LED, which `leds()` now does.
		return;
	}
	if (song_ != nullptr && song_->hasAnyPendingNextOverdubs()) {
		// `:256-259`.
		popup(result, String::STRING_FOR_CANT_SAVE_WHILE_OVERDUBS_PENDING);
		return;
	}
	// `:261` `openUI(&saveSongUI)`.
	//
	// @warning **Not ported.** The load/save browser family is phase 6 (migration spec §10.4) and
	//          `saveSongUI` is on `scenes-no-root-ui`'s forbidden list. Everything above this line
	//          -- the four-button guard, the mode test, the shift test, the hold clock, the
	//          overdub refusal -- is the arm's decision and it is ported and specced; this is the
	//          one statement that is missing. Counted rather than mailed: see `unported_opens()`
	//          and the class doc's ruling on why a second outbox is not the answer.
	count_unported(UnportedOpen::SaveSong);
}

void GlobalControls::handle_load(bool on, const UiContext& ctx) {
	if (!load_save_allowed(ctx)) {
		return;
	}
	if (on) {
		// `:282-302`. Press down.
		if (effective_ui_mode() != UiMode::None) {
			return; // `:284`
		}
		if (ctx.shift) {
			// `:286-294` `context_menu::clearSong.setupAndCheckAvailability()` then
			// `openUI(&context_menu::clearSong)`.
			//
			// @warning **Not ported.** `ContextMenuScene` exists, but the clear-song menu is not
			//          among the context menus E2 task 6 ported and its availability check is a
			//          `gui/` call. Decision ported (shift+LOAD from a clean mode), open missing.
			count_unported(UnportedOpen::ClearSong);
			return;
		}
		holding_load_ = true;          // `:297` `currentUIMode = UI_MODE_HOLDING_LOAD_BUTTON`
		save_pressed_ms_ = ctx.now_ms; // `:298` -- legacy really does reuse `timeSaveButtonPressed`
		// `:299` sets the LOAD LED; `leds()` recomputes it from `holding_load_`.
		return;
	}

	// `:304-329`. Press release.
	if (effective_ui_mode() != UiMode::HoldingLoad) {
		// `:326-328`: from `UI_MODE_NONE` or `UI_MODE_AUDITIONING` the LOAD LED goes off, which
		// `leds()` already does. Note the asymmetry with SAVE, which lists only `UI_MODE_NONE`
		// (`:268`) -- legacy's, not a transcription slip.
		return;
	}
	holding_load_ = false; // `:310`
	if (static_cast<uint32_t>(ctx.now_ms - save_pressed_ms_) >= kShortPressMs) {
		return; // `:322-324`, LED off
	}
	// `:313` `openUI(&loadSongUI)`.
	//
	// @warning **Not ported**, for the same reason as `openUI(&saveSongUI)` above. Legacy's
	//          `if (!success)` tail at `:317-320` is an empty block with two commented-out lines,
	//          so there is nothing behind the open to port either.
	count_unported(UnportedOpen::LoadSong);
}

void GlobalControls::handle_sync_scaling(bool on, const UiContext& ctx, Result& result) {
	if (song_ == nullptr) {
		return;
	}
	if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::SyncScalingAction)
	    == RuntimeFeatureStateSyncScalingAction::Fill) { // `:335-336`
		// `gui/views/view.cpp:340-343` (the note editor / note-row editor test) needs no port here: in a
		// ported note mode the press never gets here -- `forwardNoteButton` takes it (`gui/ui/shell_host_ui.cpp:907`)
		// and gives it to the mode's handler (`gui/ui/shell_note_mode.cpp:187-191`), which reaches that test.
		// The fill adjustments (`gui/views/view.cpp:345-354`) name `instrumentClipView`: E5's, unported.
		//
		// @warning **Not ported.** In this port the arm always takes legacy's "otherwise" branch,
		//          so SYNC_SCALING under the Fill setting toggles fill mode even in the two cases
		//          legacy redirects it to a note-fill adjustment. Recorded in the E4 task-4
		//          report's owed list.
		song_->changeFillMode(on); // `:356`
		return;
	}

	if (!on || effective_ui_mode() != UiMode::None) {
		return; // `:359`
	}

	// `:361-365` -- legacy's `cant:` label, reached from here and from `:380`/`:385` below.
	const auto refuse = [&result]() { popup(result, String::STRING_FOR_RECORDING_TO_ARRANGEMENT); };
	if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {
		refuse();
		return;
	}

	if (song_->getSyncScalingClip() == nullptr) { // `:372`
		if (!view_.current_ui_is_clip_minder) {
			// `:373-376` `indicator_leds::indicateAlertOnLed(IndicatorLED::CLIP_VIEW)`, which is
			// `blinkLed(led, 3, 1)` (`hid/led/indicator_leds.cpp:160-162`). Here that is a deadline
			// `leds()` declares a fast blink until; see `kAlertMs`.
			clip_view_alert_until_ms_ = ctx.now_ms + kAlertMs;
			return;
		}
		Clip* const clip = song_->getCurrentClip();
		if (clip == nullptr) {
			// Legacy dereferences `getCurrentClip()` unguarded at `:379`; a clip minder always has
			// one. Refusing rather than crashing is this layer's own guard, not a ported branch.
			return;
		}
		if (clip->isArrangementOnlyClip()) { // `:379-381`
			refuse();
			return;
		}
		if (clip->getCurrentlyRecordingLinearly()) { // `:384-386`
			refuse();
			return;
		}
		song_->setInputTickScaleClip(clip); // `:388`
	}
	else {
		song_->setInputTickScaleClip(nullptr); // `:393`
	}

	actionLogger.deleteAllLogs();                           // `:396` -- can't undo past this
	playbackHandler.resyncInternalTicksToInputTicks(song_); // `:398`
	// `:399` `setTimeBaseScaleLedState()` -- `leds()` recomputes it every tick.
}

void GlobalControls::handle_settings_menu(const UiContext& ctx, Result& result) {
	// `:450` -- press only, and only from a clean mode.
	if (effective_ui_mode() != UiMode::None) {
		return;
	}
	if (playbackHandler.recording == RecordingMode::ARRANGEMENT) { // `:452-455`
		popup(result, String::STRING_FOR_RECORDING_TO_ARRANGEMENT);
		return;
	}
	if (song_ == nullptr) {
		return;
	}
	// `:461-462` `soundEditor.setup(); openUI(&soundEditor);`.
	//
	// `SoundEditor::setup`'s default `clip` argument is `nullptr` (`gui/ui/sound_editor.h:122`), so
	// this call always takes the `else` at `sound_editor.cpp:1815`, and both of that branch's
	// view tests are `&& !Buttons::isShiftButtonPressed()` (`:1761`, `:1764`) -- which is false
	// here by construction, because this arm's own condition is `b == SELECT_ENC &&
	// Buttons::isShiftButtonPressed()` (`view.cpp:450`). So the root is `settingsRootMenu`
	// (`:1769`) whatever view is on screen, and `context_for`'s two matching arguments are passed
	// false for exactly that reason rather than because the view is unknown here.
	const menu::MenuContext menu_ctx = menu::context_for(*song_, /*clip=*/nullptr, /*source_index=*/0,
	                                                     /*performance_view=*/false,
	                                                     /*session_or_arranger_root=*/false,
	                                                     /*session_view_root=*/false, /*kit_global_fx=*/false);
	const std::span<const menu::Node* const> roots = menu::MenuScene::roots();
	const size_t i = static_cast<size_t>(menu::MenuContext::Root::Settings);
	if (i >= roots.size() || roots[i] == nullptr) {
		return;
	}
	menu_scene().open(*roots[i], menu_ctx);
	(void)result.actions.push_back(Action{.kind = Action::Kind::Push, .id = SceneId{.kind = SceneKind::FirstApp}});
}

GlobalControls::ViewArm GlobalControls::view_button_action(ButtonId button, bool on, const UiContext& ctx, Result& r) {
	const UiMode mode = effective_ui_mode();

	switch (button) {
	case ButtonId::TapTempo: { // `view.cpp:147-183`
		if (mode == UiMode::MidiLearn) {
			// `:148-166` is learn work end to end -- `deleteMidiCommandOnRelease`,
			// `endMidiLearnPressSession`, `learnedThing`. E4 task 5's; see the class doc's
			// boundary note and `LearnMode`. Left unhandled deliberately -- but NOT passed to the
			// tail, because legacy's arm reaches `:469`'s `DEALT_WITH` and so suppresses it.
			return ViewArm::LearnOwns;
		}
		if (mode == UiMode::None || mode == UiMode::RecordCountIn) { // `:167`
			if (on) {
				if (ctx.shift) {
					playbackHandler.toggleMetronomeStatus(); // `:170-172`
				}
				else if (mode == UiMode::None) {
					// `:174-179` -- tap tempo, but not during a record count-in.
					const bool use_normal_tap_tempo_behaviour =
					    runtimeFeatureSettings.get(RuntimeFeatureSettingType::AlternativeTapTempoBehaviour)
					    == RuntimeFeatureStateToggle::Off;
					playbackHandler.tapTempoButtonPress(use_normal_tap_tempo_behaviour);
				}
			}
		}
		return ViewArm::Handled; // `:469`
	}

	case ButtonId::Learn: // `:185-196`
		// `startMIDILearn`/`endMIDILearn` -- the mode itself, so E4 task 5's whole. Legacy claims
		// LEARN unconditionally (`:185`), so the tail is suppressed here too.
		return ViewArm::LearnOwns;

	case ButtonId::Play:
	case ButtonId::Record:
		if (mode == UiMode::MidiLearn) {
			// `:199-212` and `:215-228`: under learn these two buttons ARE learn targets and
			// nothing else. E4 task 5's. Unhandled -- and note that legacy's arms are guarded by
			// `&& currentUIMode == UI_MODE_MIDI_LEARN`, so outside learn the chain falls to
			// `:465-467`'s `NOT_DEALT_WITH` and `Buttons::buttonAction`'s tail runs. That is the
			// `break` below.
			return ViewArm::LearnOwns;
		}
		break; // not a `View::buttonAction` arm outside learn -- the tail has them

	case ButtonId::Save: // `:231-273`
		handle_save(on, ctx, r);
		return ViewArm::Handled; // `:469` -- reached whether or not the four-button guard ran the body

	case ButtonId::Load: // `:276-331`
		handle_load(on, ctx);
		return ViewArm::Handled;

	case ButtonId::SyncScaling: // `:334-401`
		handle_sync_scaling(on, ctx, r);
		return ViewArm::Handled;

	case ButtonId::Back: // `:404-421`
		if (on) {
#ifndef undoButtonX
			// `:407`'s `#ifndef undoButtonX`, and `undoButtonX` is defined nowhere in this tree --
			// see the class doc. This is the live undo/redo; `view.cpp:424-447`'s dedicated undo
			// and redo buttons are dead code.
			if (actionLogger.allowedToDoReversion()) { // `:409`
				// `:411-417` -- legacy pends the command rather than performing it, reusing the
				// machinery the MIDI-triggered case needs anyway, then runs `slowRoutine()` to
				// perform it immediately if the card is not being read.
				pendingGlobalMIDICommand = ctx.shift ? GlobalMIDICommand::REDO : GlobalMIDICommand::UNDO; // `:414-415`
				// `:416` writes `GlobalMIDICommand::PLAYBACK_RESTART` (== 0) into a variable
				// `playback_handler.cpp:95` defines as `int32_t`, through a mismatched `extern
				// GlobalMIDICommand` declaration at `view.cpp:138`. The value is 0 either way (and
				// `:435`, the dead-code twin, simply writes `0`), so this writes 0. Recorded in the
				// task report as a legacy oddity rather than reproduced as a type pun.
				pendingGlobalMIDICommandNumClustersWritten = 0;
				playbackHandler.slowRoutine(); // `:417`
			}
#endif
		}
		return ViewArm::Handled; // `:469` -- BACK is dealt with by `View` on press AND on release

	case ButtonId::SelectEnc: // `:449-464`
		if (!ctx.shift) {
			// `:449`'s second conjunct fails, so the whole chain falls to `:465-467`'s
			// `NOT_DEALT_WITH`. The tail has no SELECT_ENC arm, so nothing happens there either.
			return ViewArm::NotAnArm;
		}
		if (on) {
			handle_settings_menu(ctx, r);
		}
		return ViewArm::Handled;

	default:
		break;
	}
	return ViewArm::NotAnArm; // `:465-467` `return ActionResult::NOT_DEALT_WITH`
}

// -- `Buttons::buttonAction`'s tail (`hid/buttons.cpp:143-231`) ----------------------------------

Result GlobalControls::tail_button_action(ButtonId button, bool on, const UiContext& ctx) {
	Result r;

	switch (button) {
	case ButtonId::Play: { // `hid/buttons.cpp:144-167`
		if (!on) {
			r.handled = true; // the arm is `if (on)`-only, and the tail always ends `dealtWith`
			return r;
		}
		const bool record_held = ctx.held.has(ButtonId::Record);
		// `:147` `audioRecorder.recordingSource > AudioInputChannel::NONE`, read through the one
		// seam that exists for it (`AudioEngine::foregroundRecordingSource`, the same storage --
		// see `scenes/recorder/recorder_scene.h`).
		const bool output_recording = AudioEngine::foregroundRecordingSource() > AudioInputChannel::NONE;
		if (output_recording && record_held) {
			// `:149-151` -- stop output-recording at end of loop.
			if (!lowering_.press_used_up(ButtonId::Record) && playbackHandler.isEitherClockActive()
			    && currentPlaybackMode != nullptr) {
				currentPlaybackMode->stopOutputRecordingAtLoopEnd(); // `:150`
			}
		}
		else {
			playbackHandler.playButtonPressed(kInternalButtonPressLatency); // `:157`
			if (record_held && playbackHandler.playbackState != 0
			    && !lowering_.press_used_up(ButtonId::Record)) { // `:160`
				// `:161` `audioRecorder.beginOutputRecording()`.
				//
				// @warning **Not ported** -- `audioRecorder` is a `gui/` global this layer may not
				//          name and the output recorder deliberately stayed on it (see the class
				//          doc). The *decision* is ported: this is the RECORD-held-while-PLAY
				//          combo, and its other, observable half -- the used-up bit below -- is.
				count_unported(UnportedOpen::OutputRecordFromPlay);
			}
		}
		// `:165` `recordButtonPressUsedUp = true`, INSIDE `if (on)` and OUTSIDE the if/else above
		// (braces read: `:145` opens `if (on)`, `:152`/`:163` close the two branches, `:165` is the
		// statement, `:166` closes `if (on)`). The bit belongs to the button whose *release* it
		// suppresses, which is RECORD, not PLAY -- exactly as `InputLowering::mark_press_used_up`'s
		// own note says.
		lowering_.mark_press_used_up(ButtonId::Record);
		r.handled = true;
		return r;
	}

	case ButtonId::Shift: // `:169-171`
		// `commandToggleShift(on)` (`:170`) is already `InputLowering::toggle_shift`
		// (`shell/input_lowering.cpp:304-324`), which `on_button` calls *before* this event was
		// ever emitted. Reported handled because legacy's tail deals with it; nothing to do.
		r.handled = true;
		return r;

	case ButtonId::Record: // `:173-203`
		if (on) {
			// `:176` `timeRecordButtonPressed = AudioEngine::audioSampleTimer` and `:178`
			// `recordButtonPressUsedUp = false` are both already done by
			// `InputLowering::on_button` (`input_lowering.cpp:281-283`), which stamps the press
			// clock and clears the used-up bit for every button on its own press.
			if (AudioEngine::foregroundRecordingSource() == AudioInputChannel::NONE) { // `:180`
				if (ctx.shift) {                                                       // `:181`
					// `:182` `audioRecorder.beginOutputRecording()` -- @warning not ported, as
					// above.
					count_unported(UnportedOpen::OutputRecordFromShift);
					lowering_.mark_press_used_up(ButtonId::Record); // `:183`
				}
			}
		}
		else {
			// `:190-192` `display->hasPopupOfType(PopupType::THRESHOLD_RECORDING_MODE)` ->
			// `cancelPopup()`.
			//
			// @warning **Not ported, and this one changes behaviour.** That popup is raised by
			//          model code straight to the legacy display (`model/song/song.cpp:5856`),
			//          never through `notify::` or `shell::PopupScene`, so this layer has no way to
			//          ask. Legacy's short RECORD tap with that popup up dismisses it and does
			//          nothing else; here it falls through and toggles recording.
			if (!lowering_.press_used_up(ButtonId::Record)
			    && lowering_.was_short_press(ButtonId::Record, ctx.now_ms)) { // `:193-194`
				// `:195-197` `audioRecorder.isCurrentlyResampling()` ->
				// `audioRecorder.endRecordingSoon(kInternalButtonPressLatency)` -- @warning not
				// ported; `isCurrentlyResampling` reads `audioRecorder.recorder`'s status
				// (`gui/ui/audio_recorder.cpp:303-306`), which no seam exposes. The port always
				// takes the `else`.
				playbackHandler.recordButtonPressed(); // `:199`
			}
		}
		r.handled = true;
		return r;

	case ButtonId::TempoEnc: // `:206-223`
		if (on) {
			if (ctx.shift) {
				playbackHandler.commandClearTempoAutomation(); // `:208-209`
			}
			else if (ctx.held.has(ButtonId::TapTempo)) {
				playbackHandler.commandDisplaySwingInterval(); // `:211-212`
			}
			else if (!view_.loading_song && !view_.current_ui_is_song_or_arranger) {
				// `:215-220`. Legacy's `isOLEDSessionView` is `currentUI == &sessionView ||
				// currentUI == &arrangerView` (`:216`), injected here because `src/scenes` may not
				// read the root UI.
				playbackHandler.commandDisplayTempo();
			}
		}
		r.handled = true;
		return r;

	case ButtonId::ModEnc0:
	case ButtonId::ModEnc1:
		// `:226-231` `getCurrentUI()->modEncoderButtonAction(n, on)`. These never reach this
		// service: `Router::route`'s `is_mod_or_gold` sends them to `services.mod_knobs` one step
		// earlier (`shell/router.cpp`), which is where E4 task 3 ported
		// `View::modEncoderButtonAction`. Listed so the tail's arm census is complete.
		return r;

	default:
		break;
	}
	// `:232` `dealtWith:` -- every button reaches it, but only the arms above do anything, and a
	// button nothing claimed is more useful reported unhandled here (`Router::dropped_events`).
	return r;
}

Result GlobalControls::handle(const UiEvent& event, const UiContext& ctx) {
	if (event.kind != UiEvent::Kind::Button && event.kind != UiEvent::Kind::EncoderButton) {
		return {};
	}

	// `View::buttonAction` first, because `Buttons::buttonAction` calls it first (at
	// `hid/buttons.cpp:134`) and skips the tail when it dealt with the event (`:139-141`).
	//
	// **Gated on the input-taking scene being a ROOT scene**, because `View::buttonAction` is the
	// root view's fallback and only the root view's -- see `Scene::is_root`'s doc for the two-line
	// proof (`grep "public RootUI"` finds exactly `TimelineView` and `KeyboardScreen`, and both
	// forward). A non-root current UI returns `NOT_DEALT_WITH` from its own `buttonAction` and
	// legacy goes straight to the tail, which is what `ViewArm::NotAnArm` means here. The tail
	// itself is deliberately **not** gated: `hid/buttons.cpp:139-141` runs it for any current UI
	// that declined, root or not.
	Result r;
	const ViewArm arm =
	    stack_.input_top().is_root() ? view_button_action(event.button, event.on, ctx, r) : ViewArm::NotAnArm;
	switch (arm) {
	case ViewArm::Handled:
		r.handled = true;
		return r;
	case ViewArm::LearnOwns:
		// Legacy's arm returned `DEALT_WITH`, so the tail is skipped -- but the work is
		// `MidiLearn`'s and this service did none of it, so the event is reported **unhandled**
		// and continues down the chain. `services::MidiLearn` is routed one step further on
		// (E4 task 5; `shell/router.cpp`'s `run_chain`, which ends
		// `global_controls` -> `midi_learn` -> drop) and claims exactly these four buttons, so
		// what this returns is a hand-off, not a drop. See the class doc's @warning.
		return Result::unhandled();
	case ViewArm::NotAnArm:
		break;
	}
	return tail_button_action(event.button, event.on, ctx);
}

// -- LEDs ----------------------------------------------------------------------------------------

void GlobalControls::leds(deluge::ui::LedFrame& frame) {
	// `PlaybackHandler::setLedStates` (`playback/playback_handler.cpp:2499-2512`).
	(void)frame.set(led(ButtonId::Play), playbackHandler.playbackState != 0 ? kOn : kOff); // `:2501`

	// `:2503-2506`. The guard is legacy's: while an internal (resample/output) recording is live,
	// or while recording to the arrangement, the RECORD LED is owned by whoever set it -- so
	// legacy declares nothing and neither does this.
	if (AudioEngine::foregroundRecordingSource() < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION
	    && playbackHandler.recording != RecordingMode::ARRANGEMENT) {
		(void)frame.set(led(ButtonId::Record),
		                playbackHandler.recording == RecordingMode::NORMAL ? kOn : kOff); // `:2505`
	}
	// `:2508-2509`'s `deluge_signal_write(DELUGE_SIGNAL_SYNC_LED, ...)` has no `LedId`; see the
	// class doc.
	(void)frame.set(led(ButtonId::TapTempo), playbackHandler.metronomeOn ? kOn : kOff); // `:2511`

	// `View::setTimeBaseScaleLedState` (`view.cpp:519-529`), which is the whole of
	// `View::setLedStates` (`:514-516`).
	Clip* const sync_clip = song_ != nullptr ? song_->getSyncScalingClip() : nullptr;
	if (view_.current_ui_is_clip_minder && song_ != nullptr && song_->getCurrentClip() == sync_clip
	    && sync_clip != nullptr) {
		// `:504-506` `indicator_leds::blinkLed(IndicatorLED::SYNC_SCALING)` -- the defaults are
		// `numBlinks` 255 (forever) and `blinkingType` 0, whose period is `kFlashTime`
		// (`definitions_cxx.hpp:159`), carried by the toolkit as `deluge::ui::kFlashTime`.
		//
		// The `sync_clip != nullptr` conjunct is NOT in legacy, and it is not a widening: legacy's
		// `getCurrentClip() == currentSong->getSyncScalingClip()` is true when both are null, and
		// it then blinks an LED for a scaling clip that does not exist. It cannot happen on the
		// device because `toClipMinder()` implies a current clip, but this layer's `song_` may be a
		// fixture with neither. Stated rather than silent.
		(void)frame.set(
		    led(ButtonId::SyncScaling),
		    deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, static_cast<uint16_t>(deluge::ui::kFlashTime), 0});
	}
	else {
		(void)frame.set(led(ButtonId::SyncScaling), sync_clip != nullptr ? kOn : kOff); // `:510`
	}

	// `View::buttonAction`'s own two LEDs: SAVE on while held (`view.cpp:243`), off otherwise
	// (`:265`, `:269`); LOAD the same (`:299`, `:323`, `:327`).
	(void)frame.set(led(ButtonId::Save), holding_save_ ? kOn : kOff);
	(void)frame.set(led(ButtonId::Load), holding_load_ ? kOn : kOff);

	// `indicator_leds::indicateAlertOnLed(IndicatorLED::CLIP_VIEW)` (`view.cpp:375`) =
	// `blinkLed(led, 3, 1)` (`hid/led/indicator_leds.cpp:160-162`): FIVE half-periods of
	// `kFastFlashTime` -- see `kAlertMs`, and the spec pins it against legacy's own countdown --
	// then `setLedState(led, returnToState)` (`:128`). `returnToState` is the
	// LED's state when the alert began, and this arm only fires when the current UI is NOT a clip
	// minder -- where CLIP_VIEW is off -- so Off is the fold-back, declared explicitly rather than
	// through `LedState::until_ms` (whose fold-back is *On*, `shell/leds.h`). A root scene that
	// declares CLIP_VIEW for itself outranks this: `LedCompose::compose` runs services first and
	// the last writer wins.
	//
	// **That justification is false under a guest, and F1 task 11's A/B measured it.** When
	// `ShellHostUI` is what is open over `InstrumentClipView`, the root UI underneath IS a clip
	// minder and legacy has CLIP_VIEW lit -- `SoundEditor::setLedStates` (`gui/ui/sound_editor.cpp:
	// 340-354`) clears SAVE, SYNTH, KIT, MIDI, CV, CROSS_SCREEN and SCALE and never touches
	// CLIP_VIEW, so opening the legacy menu leaves it on. `MenuScene::leds` ports exactly that list
	// (`scenes/menu/menu_scene.cpp:1123-1126`) and so declares no CLIP_VIEW to outrank this line --
	// and this line then puts the LED out. Measured on `tests/ui-scripts/app/menu.txt`: every
	// toggle-off menu frame has `led Clip on` and every toggle-on one does not, while `boot` and
	// `menu-exit` agree.
	//
	// Recorded rather than fixed here, and the fix is named: declare nothing when not alerting,
	// which is what "restore the state it was in" (`returnToState`) actually means and what the
	// guest's own rule -- "an LED no scene declares is legacy's" (`gui/ui/shell_host_ui.h`'s
	// `@par The LEDs`) -- expects. It is not this task's to make: it moves `global_controls_spec.cpp`
	// and every level-2 golden recorded with the real services, which is E4's fixture.
	const bool alerting =
	    clip_view_alert_until_ms_.has_value() && static_cast<int32_t>(now_ms_ - *clip_view_alert_until_ms_) < 0;
	(void)frame.set(led(ButtonId::Clip), alerting ? deluge::ui::LedState{deluge::ui::LedState::Kind::Blink,
	                                                                     static_cast<uint16_t>(kClipArmFlashMs), 0}
	                                              : kOff);
}

// -- Time ------------------------------------------------------------------------------------------

void GlobalControls::flash_play_enable(uint32_t now_ms) {
	// `View::flashPlayEnable` (`view.cpp:2783-2785`): `setTimer(PLAY_ENABLE_FLASH, kFastFlashTime)`.
	clip_arm_flash_deadline_ms_ = now_ms + kClipArmFlashMs;
}

void GlobalControls::flash_play_disable() {
	// `View::flashPlayDisable` (`view.cpp:2787-2800`): clear the phase and unset the timer. The
	// render requests at `:2774-2782` are view-side and not ported.
	clip_arm_flash_on_ = false;
	clip_arm_flash_deadline_ms_.reset();
}

void GlobalControls::tick(uint32_t now_ms, uint32_t /*dt_ms*/) {
	now_ms_ = now_ms;

	if (clip_arm_flash_deadline_ms_.has_value() && static_cast<int32_t>(now_ms - *clip_arm_flash_deadline_ms_) >= 0) {
		// `View::flashPlayRoutine` (`view.cpp:2771-2781`): toggle the phase, then ask for a render.
		// The re-arm is `SessionView::flashPlayRoutine`'s (`gui/views/session_view.cpp:2441-2442`,
		// `:2441-2444`) and is view-side, so this fires once per `flash_play_enable()`.
		clip_arm_flash_on_ = !clip_arm_flash_on_;
		clip_arm_flash_deadline_ms_.reset();
	}

	if (clip_view_alert_until_ms_.has_value() && static_cast<int32_t>(now_ms - *clip_view_alert_until_ms_) >= 0) {
		clip_view_alert_until_ms_.reset();
	}
}

std::optional<uint32_t> GlobalControls::next_timer() const {
	std::optional<uint32_t> next;
	const auto consider = [&next](uint32_t in) {
		if (!next.has_value() || in < *next) {
			next = in;
		}
	};
	const auto consider_deadline = [&](const std::optional<uint32_t>& deadline) {
		if (!deadline.has_value()) {
			return;
		}
		const int32_t remaining = static_cast<int32_t>(*deadline - now_ms_);
		consider(remaining > 0 ? static_cast<uint32_t>(remaining) : 0);
	};
	consider_deadline(clip_arm_flash_deadline_ms_);
	consider_deadline(clip_view_alert_until_ms_);

	// A **blinking LED needs a wake of its own.** `Shell::compute_next_wake` considers gate
	// deadlines, scene timers and service timers -- and nothing at all for
	// `LedState::Kind::Blink`, whose phase `shell::led_is_on` derives from `now_ms`
	// (`shell/leds.h`). So on a quiet system a declared blink simply freezes at whatever phase the
	// last tick left it in unless somebody asks to be woken. This service declares two blinks (the
	// SYNC_SCALING flash and the CLIP_VIEW alert), so it asks.
	if (blinking_sync_scaling()) {
		const uint32_t half = deluge::ui::kFlashTime;
		consider(half - (now_ms_ % half));
	}
	if (clip_view_alert_until_ms_.has_value()) {
		consider(kClipArmFlashMs - (now_ms_ % kClipArmFlashMs));
	}
	return next;
}

bool GlobalControls::blinking_sync_scaling() const {
	Clip* const sync_clip = song_ != nullptr ? song_->getSyncScalingClip() : nullptr;
	return view_.current_ui_is_clip_minder && sync_clip != nullptr && song_ != nullptr
	       && song_->getCurrentClip() == sync_clip;
}

} // namespace deluge::scenes::services
