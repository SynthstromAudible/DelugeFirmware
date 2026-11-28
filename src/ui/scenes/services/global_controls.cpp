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

#include "ui/scenes/services/global_controls.h"
#include "ui/scenes/bind/device_reader.h"
#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/services/view_state.h"

#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/registry.h"

#include "ui/shell/input_lowering.h"

#include "gui/l10n/strings.h"
#include "project/device_settings.h"
#include "project/features.h"
#include "project/song.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::ui::scenes::services {

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

/// @brief Make clip @p clip_id the song's sync-scaling clip (-1 for none): a SONG request to @p writes.
/// @param writes  The installed write sink, or null: nothing is written.
/// @param clip_id The clip's id, or -1 for none.
void write_sync_scaling(bind::WriteSink* writes, int32_t clip_id) {
	if (writes == nullptr) {
		return;
	}
	(void)writes->write({.target = {.kind = bind::Target::Kind::MODEL,
	                                .path = "syncScalingClip",
	                                .group = deluge::project::SettingGroupId::SONG},
	                     .menu_value = clip_id});
}

/// @brief One `l10n::String` as the `uint16_t` `Action::popup_string` carries, exactly as every
///        other scene in `src/ui/scenes` spells it.
/// @param s The string.
/// @return Its id.
constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

constexpr deluge::ui::widgets::core::LedState kOff{deluge::ui::widgets::core::LedState::Kind::Off, 0, 0};
constexpr deluge::ui::widgets::core::LedState kOn{deluge::ui::widgets::core::LedState::Kind::On, 0, 0};

/// @brief `IndicatorLED`'s value for @p b, as the toolkit's opaque `LedId`.
///
/// `LedId` values are `ButtonId` values by the board profile's own contract
/// (`ui/shell/profiles/deluge.h`'s `kDelugeLedByButton`), so this is the whole of the mapping.
/// @param b The button whose LED to name.
/// @return The LED id.
constexpr deluge::ui::widgets::core::LedId led(ButtonId b) {
	return static_cast<deluge::ui::widgets::core::LedId>(b);
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
	// because that is the value `View::buttonAction`'s own arms test first, and
	// because `View::startMIDILearn` only enters the mode from `UI_MODE_NONE` or from
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

// -- `View::buttonAction` ----------------------------------------------------------------------------

bool GlobalControls::load_save_allowed(const UiContext& ctx) const {
	// `View::buttonAction`'s SAVE-arm guard and its identical twin in the LOAD arm. Note the guard wraps the WHOLE arm
	// body: when it fails, legacy does nothing at all and still falls through to `View::buttonAction`'s
	// closing `return ActionResult::DEALT_WITH`, so the caller reports handled either way.
	if (ctx.held.has(ButtonId::Synth) || ctx.held.has(ButtonId::Kit) || ctx.held.has(ButtonId::Midi)
	    || ctx.held.has(ButtonId::Cv)) {
		return false;
	}
	// KEYBOARD blocks it only in performance view.
	return !(view_.root_is_performance && ctx.held.has(ButtonId::Keyboard));
}

void GlobalControls::handle_save(bool on, const UiContext& ctx, Result& result) {
	if (!load_save_allowed(ctx)) {
		return;
	}
	if (on) {
		// Press down: only from a clean mode, and never with shift (which is the
		// SAVE-as-a-modifier gesture other UIs read).
		if (effective_ui_mode() == UiMode::None && !ctx.shift) {
			holding_save_ = true; // `currentUIMode = UI_MODE_HOLDING_SAVE_BUTTON`
			save_pressed_ms_ = ctx.now_ms;
			// `indicator_leds::setLedState(IndicatorLED::SAVE, true)` -- `leds()` recomputes
			// it from `holding_save_`.
		}
		return;
	}

	// Press release.
	if (effective_ui_mode() != UiMode::HoldingSave) {
		// From `UI_MODE_NONE` the SAVE LED is turned off, which `leds()` already does
		// every tick. Nothing else happens.
		return;
	}
	holding_save_ = false; // `currentUIMode = UI_MODE_NONE`
	if (static_cast<uint32_t>(ctx.now_ms - save_pressed_ms_) >= kShortPressMs) {
		// A long press just drops the LED, which `leds()` now does.
		return;
	}
	if (const bind::Transport* t = bind::port_of(ports_, &bind::Ports::transport);
	    t != nullptr && t->overdubs_pending()) {
		popup(result, String::STRING_FOR_CANT_SAVE_WHILE_OVERDUBS_PENDING);
		return;
	}
	// `openUI(&saveSongUI)`.
	//
	// @warning **Not ported.** The load/save browser family is not ported and
	//          `saveSongUI` is on `scenes-no-root-ui`'s forbidden list. Everything above this line
	//          -- the four-button guard, the mode test, the shift test, the hold clock, the
	//          overdub refusal -- is the arm's decision and it is ported and specced; this is the
	//          one statement that is missing. Counted rather than mailed: see `unported_opens()`
	//          and the class doc on why a second outbox is not the answer.
	count_unported(UnportedOpen::SaveSong);
}

void GlobalControls::handle_load(bool on, const UiContext& ctx) {
	if (!load_save_allowed(ctx)) {
		return;
	}
	if (on) {
		// Press down.
		if (effective_ui_mode() != UiMode::None) {
			return;
		}
		if (ctx.shift) {
			// `context_menu::clearSong.setupAndCheckAvailability()` then
			// `openUI(&context_menu::clearSong)`.
			//
			// @warning **Not ported.** `ContextMenuScene` exists, but the clear-song menu is not
			//          among the context menu scenes and its availability check is a
			//          `gui/` call. Decision ported (shift+LOAD from a clean mode), open missing.
			count_unported(UnportedOpen::ClearSong);
			return;
		}
		holding_load_ = true;          // `currentUIMode = UI_MODE_HOLDING_LOAD_BUTTON`
		save_pressed_ms_ = ctx.now_ms; // Legacy really does reuse `timeSaveButtonPressed`
		// Sets the LOAD LED; `leds()` recomputes it from `holding_load_`.
		return;
	}

	// Press release.
	if (effective_ui_mode() != UiMode::HoldingLoad) {
		// From `UI_MODE_NONE` or `UI_MODE_AUDITIONING` the LOAD LED goes off, which
		// `leds()` already does. Note the asymmetry with SAVE, which lists only `UI_MODE_NONE`
		// -- legacy's, not a transcription slip.
		return;
	}
	holding_load_ = false;
	if (static_cast<uint32_t>(ctx.now_ms - save_pressed_ms_) >= kShortPressMs) {
		return; // LED off
	}
	// `openUI(&loadSongUI)`.
	//
	// @warning **Not ported**, for the same reason as `openUI(&saveSongUI)` above. Legacy's
	//          `if (!success)` tail after the open is an empty block with two commented-out lines,
	//          so there is nothing behind the open to port either.
	count_unported(UnportedOpen::LoadSong);
}

void GlobalControls::handle_sync_scaling(bool on, const UiContext& ctx, Result& result) {
	if (model_ == nullptr) {
		return;
	}
	if (bind::read_device<project::SyncScalingAction>(
	        bind::port_of(ports_, &bind::Ports::device),
	        project::device_path::feature(project::Feature::SYNC_SCALING_ACTION))
	    == project::SyncScalingAction::FILL) {
		// `View::buttonAction` (`gui/views/view.cpp`) (the note editor / note-row editor test) needs no port here: in a
		// note mode the press never gets here -- `forwardNoteButton` takes it (`gui/ui/shell_host_ui.cpp`) and
		// gives it to the mode's handler (`gui/ui/shell_note_mode.cpp`), which reaches that test. The fill adjustments
		// (`View::buttonAction` in `gui/views/view.cpp`) name `instrumentClipView`, whose scene is unported.
		//
		// @warning **Not ported.** In this port the arm always takes legacy's "otherwise" branch,
		//          so SYNC_SCALING under the Fill setting toggles fill mode even in the two cases
		//          legacy redirects it to a note-fill adjustment.
		if (bind::TransportActions* a = actions(); a != nullptr) {
			a->fill(on);
		}
		return;
	}

	if (!on || effective_ui_mode() != UiMode::None) {
		return;
	}

	// Legacy's `cant:` label, reached from here and from the arrangement-only and linear-recording refusals below.
	const auto refuse = [&result]() { popup(result, String::STRING_FOR_RECORDING_TO_ARRANGEMENT); };
	if (recording() == bind::TransportRecording::ARRANGEMENT) {
		refuse();
		return;
	}

	bind::WriteSink* const writes = bind::port_of(ports_, &bind::Ports::writes);
	if (!model_->transport.sync_scaling_clip_id) {
		if (!view_.current_ui_is_clip_minder) {
			// `indicator_leds::indicateAlertOnLed(IndicatorLED::CLIP_VIEW)`, which is
			// `blinkLed(led, 3, 1)` (`hid/led/indicator_leds.cpp`). Here that is a deadline
			// `leds()` declares a fast blink until; see `kAlertMs`.
			clip_view_alert_until_ms_ = ctx.now_ms + kAlertMs;
			return;
		}
		// The clip the view has open: legacy dereferences `getCurrentClip()` unguarded (a clip minder always
		// has one); with none, or no clip facts installed, nothing is done.
		const std::optional<project::ClipAddress> clip = services::view_state().current_view().clip;
		const bind::ClipFacts* const clips = bind::port_of(ports_, &bind::Ports::clips);
		if (!clip || clips == nullptr) {
			return;
		}
		if (clips->clip_arrangement_only(*clip)) {
			refuse();
			return;
		}
		if (clips->clip_recording_linearly(*clip)) {
			refuse();
			return;
		}
		// The setting's Edit re-derives the tick magnitude and re-syncs the internal ticks.
		write_sync_scaling(writes, static_cast<int32_t>(clip->clip_id));
	}
	else {
		write_sync_scaling(writes, -1);
	}

	// Undo cannot reach past this. `setTimeBaseScaleLedState()` has no port: `leds()` recomputes it every tick.
	if (bind::DeviceActions* const device = bind::port_of(ports_, &bind::Ports::device_actions); device != nullptr) {
		device->delete_all_logs();
	}
}

void GlobalControls::handle_settings_menu(const UiContext& ctx, Result& result) {
	// Press only, and only from a clean mode.
	if (effective_ui_mode() != UiMode::None) {
		return;
	}
	if (recording() == bind::TransportRecording::ARRANGEMENT) {
		popup(result, String::STRING_FOR_RECORDING_TO_ARRANGEMENT);
		return;
	}
	if (model_ == nullptr) {
		return;
	}
	// `soundEditor.setup(); openUI(&soundEditor);`.
	//
	// `SoundEditor::setup`'s default `clip` argument is `nullptr` (`origin/main:src/deluge/gui/ui/sound_editor.h`), so
	// this call always takes the `else` in `SoundEditor::setup`, and both of that branch's
	// view tests are `&& !Buttons::isShiftButtonPressed()` (the performance-view and song-view ones) -- which is false
	// here by construction, because this arm's own condition is `b == SELECT_ENC &&
	// Buttons::isShiftButtonPressed()` (`View::buttonAction`). So the root is `settingsRootMenu`
	// (its final `else`) whatever view is on screen, and `context_for`'s two matching arguments are passed
	// false for exactly that reason rather than because the view is unknown here.
	// The menu binds through the installed set, whose device port the settings root reads; with none installed it
	// reads every row's default.
	menu::MenuContext menu_ctx = menu::context_for(*model_, ports_, services::MenuView{}, /*source_index=*/0,
	                                               /*performance_view=*/false,
	                                               /*session_or_arranger_root=*/false,
	                                               /*session_view_root=*/false, /*kit_global_fx=*/false);
	// The greyout still follows the root view under the menu (`getGreyoutColsAndRows`).
	menu_ctx.root_greyout = view_.root_greyout;
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
	case ButtonId::TapTempo: { // the TAP_TEMPO arm of `View::buttonAction` (`gui/views/view.cpp`)
		if (mode == UiMode::MidiLearn) {
			// The learn branch is learn work end to end -- `deleteMidiCommandOnRelease`,
			// `endMidiLearnPressSession`, `learnedThing`. `MidiLearn`'s; see the class doc's
			// boundary note and `LearnMode`. Left unhandled deliberately -- but NOT passed to the
			// tail, because legacy's arm reaches the closing `DEALT_WITH` return and so suppresses it.
			return ViewArm::LearnOwns;
		}
		if (mode == UiMode::None || mode == UiMode::RecordCountIn) {
			if (on) {
				bind::TransportActions* const a = actions();
				if (ctx.shift) {
					if (a != nullptr) {
						a->toggle_metronome();
					}
				}
				else if (mode == UiMode::None) {
					// Tap tempo, but not during a record count-in.
					const bool use_normal_tap_tempo_behaviour = !bind::read_device_flag(
					    bind::port_of(ports_, &bind::Ports::device),
					    project::device_path::feature(project::Feature::ALTERNATIVE_TAP_TEMPO_BEHAVIOUR));
					if (a != nullptr) {
						a->tap_tempo(use_normal_tap_tempo_behaviour);
					}
				}
			}
		}
		return ViewArm::Handled;
	}

	case ButtonId::Learn:
		// `startMIDILearn`/`endMIDILearn` -- the mode itself, so all of it is `MidiLearn`'s. Legacy claims
		// LEARN unconditionally, so the tail is suppressed here too.
		return ViewArm::LearnOwns;

	case ButtonId::Play:
	case ButtonId::Record:
		if (mode == UiMode::MidiLearn) {
			// The PLAY and RECORD arms: under learn these two buttons ARE learn targets and
			// nothing else. `MidiLearn`'s. Unhandled -- and note that legacy's arms are guarded by
			// `&& currentUIMode == UI_MODE_MIDI_LEARN`, so outside learn the chain falls to
			// the final `NOT_DEALT_WITH` return and `Buttons::buttonAction`'s tail runs. That is the
			// `break` below.
			return ViewArm::LearnOwns;
		}
		break; // not a `View::buttonAction` arm outside learn -- the tail has them

	case ButtonId::Save:
		handle_save(on, ctx, r);
		return ViewArm::Handled; // Reached whether or not the four-button guard ran the body

	case ButtonId::Load:
		handle_load(on, ctx);
		return ViewArm::Handled;

	case ButtonId::SyncScaling:
		handle_sync_scaling(on, ctx, r);
		return ViewArm::Handled;

	case ButtonId::Back:
		if (on) {
			// The BACK arm's undo, or redo with shift. The reversion runs inside this call, so nothing is read after
			// it.
			if (const bind::Transport* t = bind::port_of(ports_, &bind::Ports::transport);
			    t != nullptr && t->undo_allowed()) {
				if (bind::TransportActions* const a = actions(); a != nullptr) {
					a->undo(ctx.shift);
				}
			}
		}
		return ViewArm::Handled; // BACK is dealt with by `View` on press AND on release

	case ButtonId::SelectEnc:
		if (!ctx.shift) {
			// The SELECT_ENC arm's shift conjunct fails, so the whole chain falls to the final
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
	return ViewArm::NotAnArm; // `return ActionResult::NOT_DEALT_WITH`
}

// -- `Buttons::buttonAction`'s tail (`hid/buttons.cpp`) ----------------------------------

Result GlobalControls::tail_button_action(ButtonId button, bool on, const UiContext& ctx) {
	Result r;

	switch (button) {
	case ButtonId::Play: { // `Buttons::buttonAction` (`hid/buttons.cpp`)
		if (!on) {
			r.handled = true; // the arm is `if (on)`-only, and the tail always ends `dealtWith`
			return r;
		}
		const bool record_held = ctx.held.has(ButtonId::Record);
		// Legacy's PLAY arm asks whether the foreground recorder has a source, the same storage the recorder reads (see
		// `ui/scenes/recorder/recorder_scene.h`).
		const bind::ForegroundRecording source = foreground();
		const bool output_recording =
		    source == bind::ForegroundRecording::EXTERNAL || source == bind::ForegroundRecording::INTERNAL;
		bind::TransportActions* const a = actions();
		if (output_recording && record_held) {
			// Stop output-recording at end of loop.
			const bind::Transport* const t = bind::port_of(ports_, &bind::Ports::transport);
			if (!lowering_.press_used_up(ButtonId::Record) && t != nullptr && t->clocks_active() && a != nullptr) {
				a->stop_output_recording_at_loop_end();
			}
		}
		else {
			if (a != nullptr) {
				a->play();
			}
			// Read after the press: it reads whether the press started playback.
			if (record_held && playing() && !lowering_.press_used_up(ButtonId::Record)) {
				// Begin output-recording simultaneously with playback.
				if (a != nullptr) {
					a->begin_output_recording();
				}
			}
		}
		// `recordButtonPressUsedUp = true` sits INSIDE `if (on)` and OUTSIDE the if/else above (by the braces of
		// `Buttons::buttonAction`'s PLAY arm in `hid/buttons.cpp`). The bit belongs to the button whose *release* it
		// suppresses, which is RECORD, not PLAY -- exactly as `InputLowering::mark_press_used_up`'s
		// own note says.
		lowering_.mark_press_used_up(ButtonId::Record);
		r.handled = true;
		return r;
	}

	case ButtonId::Shift:
		// `commandToggleShift(on)` is already `InputLowering::toggle_shift`
		// (`ui/shell/input_lowering.cpp`), which `on_button` calls *before* this event was
		// ever emitted. Reported handled because legacy's tail deals with it; nothing to do.
		r.handled = true;
		return r;

	case ButtonId::Record: // `Buttons::buttonAction`'s RECORD arm
		if (on) {
			// The press is the tail's: its release may toggle recording. A press a scene takes never reaches here.
			record_press_ms_ = lowering_.last_press_ms(ButtonId::Record);
			// `timeRecordButtonPressed = AudioEngine::audioSampleTimer` and `recordButtonPressUsedUp = false` are both
			// already done by `InputLowering::on_button`, which stamps the press clock and clears the used-up bit for
			// every button on its own press.
			if (foreground() == bind::ForegroundRecording::NONE) {
				if (ctx.shift) {
					if (bind::TransportActions* const a = actions(); a != nullptr) {
						a->begin_output_recording();
					}
					lowering_.mark_press_used_up(ButtonId::Record);
				}
			}
		}
		else {
			// `display->hasPopupOfType(PopupType::THRESHOLD_RECORDING_MODE)` ->
			// `cancelPopup()`.
			//
			// @warning **Not ported, and this one changes behaviour.** That popup is raised by
			//          model code straight to the legacy display (`model/song/song.cpp`),
			//          never through `notify::` or `shell::PopupScene`, so this layer has no way to
			//          ask. Legacy's short RECORD tap with that popup up dismisses it and does
			//          nothing else; here it falls through and toggles recording.
			// Only the release of a press the tail took toggles: the sample browser's RECORD and the recorder's stop
			// take their press, and their release must not arm or disarm recording.
			const bool own_press =
			    record_press_ms_.has_value() && record_press_ms_ == lowering_.last_press_ms(ButtonId::Record);
			if (own_press && !lowering_.press_used_up(ButtonId::Record)
			    && lowering_.was_short_press(ButtonId::Record, ctx.now_ms)) {
				const bind::AudioFacts* const audio = bind::port_of(ports_, &bind::Ports::audio);
				bind::TransportActions* const a = actions();
				if (audio != nullptr && audio->resampling()) {
					if (a != nullptr) {
						a->end_recording_soon();
					}
				}
				else if (a != nullptr) {
					a->record();
				}
			}
		}
		r.handled = true;
		return r;

	case ButtonId::TempoEnc:
		if (on) {
			bind::TransportActions* const a = actions();
			if (ctx.shift) {
				if (a != nullptr) {
					a->clear_tempo_automation();
				}
			}
			else if (ctx.held.has(ButtonId::TapTempo)) {
				if (a != nullptr) {
					a->show_swing_interval();
				}
			}
			else if (!view_.loading_song && !view_.current_ui_is_song_or_arranger) {
				// Legacy's `isOLEDSessionView` is `currentUI == &sessionView ||
				// currentUI == &arrangerView`, injected here because `src/ui/scenes` may not
				// read the root UI.
				if (a != nullptr) {
					a->show_tempo();
				}
			}
		}
		r.handled = true;
		return r;

	case ButtonId::ModEnc0:
	case ButtonId::ModEnc1:
		// `getCurrentUI()->modEncoderButtonAction(n, on)`. These never reach this service: `Router::route`'s
		// `is_mod_or_gold` sends them to `services.mod_knobs` one step earlier (`ui/shell/router.cpp`), which is where
		// `View::modEncoderButtonAction` (`origin/main:src/deluge/gui/views/view.cpp`) is ported. Listed so the tail's
		// arm census is complete.
		return r;

	default:
		break;
	}
	// `dealtWith:` -- every button reaches it, but only the arms above do anything, and a
	// button nothing claimed is more useful reported unhandled here (`Router::dropped_events`).
	return r;
}

Result GlobalControls::handle(const UiEvent& event, const UiContext& ctx) {
	if (event.kind != UiEvent::Kind::Button && event.kind != UiEvent::Kind::EncoderButton) {
		return {};
	}

	// `View::buttonAction` first, because `Buttons::buttonAction` calls it first (at
	// `hid/buttons.cpp`) and skips the tail when it dealt with the event.
	//
	// **Gated on the input-taking scene being a ROOT scene**, because `View::buttonAction` is the
	// root view's fallback and only the root view's -- see `Scene::is_root`'s doc for the two-line
	// proof (`grep "public RootUI"` finds exactly `TimelineView` and `KeyboardScreen`, and both
	// forward). A non-root current UI returns `NOT_DEALT_WITH` from its own `buttonAction` and
	// legacy goes straight to the tail, which is what `ViewArm::NotAnArm` means here. The tail
	// itself is deliberately **not** gated: `Buttons::buttonAction` (`hid/buttons.cpp`) runs it for any current UI
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
		// (`ui/shell/router.cpp`'s `run_chain`, which ends
		// `global_controls` -> `midi_learn` -> drop) and claims exactly these four buttons, so
		// what this returns is a hand-off, not a drop. See the class doc's @warning.
		return Result::unhandled();
	case ViewArm::NotAnArm:
		break;
	}
	return tail_button_action(event.button, event.on, ctx);
}

// -- The installed ports ---------------------------------------------------------------------------

bool GlobalControls::playing() const {
	const bind::Transport* const t = bind::port_of(ports_, &bind::Ports::transport);
	return t != nullptr && t->playing();
}

bind::TransportRecording GlobalControls::recording() const {
	const bind::Transport* const t = bind::port_of(ports_, &bind::Ports::transport);
	return t != nullptr ? t->recording() : bind::TransportRecording::OFF;
}

bind::ForegroundRecording GlobalControls::foreground() const {
	const bind::AudioFacts* const audio = bind::port_of(ports_, &bind::Ports::audio);
	return audio != nullptr ? audio->foreground_recording() : bind::ForegroundRecording::NONE;
}

bind::TransportActions* GlobalControls::actions() const {
	return bind::port_of(ports_, &bind::Ports::transport_actions);
}

// -- LEDs ----------------------------------------------------------------------------------------

void GlobalControls::leds(deluge::ui::widgets::core::LedFrame& frame) {
	// `PlaybackHandler::setLedStates` (`playback/playback_handler.cpp`).
	(void)frame.set(led(ButtonId::Play), playing() ? kOn : kOff);

	// The guard is legacy's: while an internal (resample/output) recording is live,
	// or while recording to the arrangement, the RECORD LED is owned by whoever set it -- so
	// legacy declares nothing and neither does this.
	const bind::TransportRecording mode = recording();
	if (foreground() != bind::ForegroundRecording::INTERNAL && mode != bind::TransportRecording::ARRANGEMENT) {
		(void)frame.set(led(ButtonId::Record), mode == bind::TransportRecording::NORMAL ? kOn : kOff);
	}
	// The tail's `deluge_signal_write(DELUGE_SIGNAL_SYNC_LED, ...)` has no `LedId`; see the
	// class doc.
	const bind::Transport* const t = bind::port_of(ports_, &bind::Ports::transport);
	(void)frame.set(led(ButtonId::TapTempo), t != nullptr && t->metronome_on() ? kOn : kOff);

	// `View::setTimeBaseScaleLedState`, which is the whole of
	// `View::setLedStates`.
	if (blinking_sync_scaling()) {
		// `indicator_leds::blinkLed(IndicatorLED::SYNC_SCALING)` -- the defaults are
		// `numBlinks` 255 (forever) on the normal blink every `blinkingType` 0 LED shares.
		//
		// A sync-scaling clip must exist: legacy's `getCurrentClip() == getSyncScalingClip()` is also true when both
		// are null, which the device never reaches (a clip minder always has a clip).
		(void)frame.set(led(ButtonId::SyncScaling),
		                deluge::ui::widgets::core::LedState::shared(deluge::ui::widgets::core::BlinkRate::Normal));
	}
	else {
		(void)frame.set(led(ButtonId::SyncScaling),
		                model_ != nullptr && model_->transport.sync_scaling_clip_id ? kOn : kOff);
	}

	// `View::buttonAction`'s own two LEDs: SAVE on while held, off otherwise
	// (set when the hold starts, cleared on release); LOAD the same.
	(void)frame.set(led(ButtonId::Save), holding_save_ ? kOn : kOff);
	(void)frame.set(led(ButtonId::Load), holding_load_ ? kOn : kOff);

	// `indicator_leds::indicateAlertOnLed(IndicatorLED::CLIP_VIEW)` (`View::buttonAction`) =
	// `blinkLed(led, 3, 1)` (`hid/led/indicator_leds.cpp`): FIVE half-periods of
	// `kFastFlashTime` -- see `kAlertMs`, and the spec pins it against legacy's own countdown --
	// then `setLedState(led, returnToState)`, the state the LED had when the alert began.
	// Declared only while alerting: once nothing declares it the shell hands the LED back
	// (`SurfaceSet::release_led`), which under the guest restores legacy's state from before the
	// alert -- lit over a clip view, as `SoundEditor::setLedStates` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`)
	// never touches it.
	const bool alerting =
	    clip_view_alert_until_ms_.has_value() && static_cast<int32_t>(now_ms_ - *clip_view_alert_until_ms_) < 0;
	if (alerting) {
		(void)frame.set(led(ButtonId::Clip),
		                deluge::ui::widgets::core::LedState::shared(deluge::ui::widgets::core::BlinkRate::Fast));
	}
}

// -- Time ------------------------------------------------------------------------------------------

void GlobalControls::flash_play_enable(uint32_t now_ms) {
	// `View::flashPlayEnable`: `setTimer(PLAY_ENABLE_FLASH, kFastFlashTime)`, only when it is not already set --
	// scrolling and redrawing must not postpone a flash already scheduled.
	if (!clip_arm_flash_deadline_ms_.has_value()) {
		clip_arm_flash_deadline_ms_ = now_ms + kClipArmFlashMs;
	}
}

void GlobalControls::flash_play_disable() {
	// `View::flashPlayDisable`: clear the phase and unset the timer. The
	// render requests in `View::flashPlayRoutine` are view-side and not ported.
	clip_arm_flash_on_ = false;
	clip_arm_flash_deadline_ms_.reset();
}

void GlobalControls::tick(uint32_t now_ms, uint32_t /*dt_ms*/) {
	now_ms_ = now_ms;

	if (clip_arm_flash_deadline_ms_.has_value() && static_cast<int32_t>(now_ms - *clip_arm_flash_deadline_ms_) >= 0) {
		// `View::flashPlayRoutine`: toggle the phase, then ask for a render.
		// The re-arm is `SessionView::flashPlayRoutine`'s call of `View::flashPlayEnable`
		// (`gui/views/session_view.cpp`) and is view-side, so this fires once per `flash_play_enable()`.
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
	// The two blinks it declares need no wake of their own: a declared blink is one of the shell's.
	return next;
}

bool GlobalControls::blinking_sync_scaling() const {
	// The sync-scaling clip is the one the view has open (`View::setTimeBaseScaleLedState`).
	if (!view_.current_ui_is_clip_minder || model_ == nullptr) {
		return false;
	}
	const std::optional<uint32_t> sync_clip = model_->transport.sync_scaling_clip_id;
	const std::optional<project::ClipAddress> current = services::view_state().current_view().clip;
	return sync_clip && current && current->clip_id == *sync_clip;
}

} // namespace deluge::ui::scenes::services
