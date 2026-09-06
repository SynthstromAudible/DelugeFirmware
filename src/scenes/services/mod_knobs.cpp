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
/// @brief `ModKnobs`. See `mod_knobs.h` for the interface, the layering argument, and what is
///        deliberately still owed.

#include "scenes/services/mod_knobs.h"

#include "scenes/menu/context.h" // the four model transactions (see the class doc)
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/registry.h"

#include "shell/input_lowering.h"

#include "gui/l10n/strings.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "model/clip/clip.h"
#include "model/instrument/midi_instrument.h"
#include "model/mod_controllable/mod_controllable.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "playback/playback_handler.h"

#include <algorithm>
#include <cstdint>

namespace deluge::scenes::services {

using deluge::modulation::params::Kind;
using deluge::ui::EncoderRole;
using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::UiEvent;

namespace {

/// @brief `deluge::hid::encoders::modEncoderAt` (`hid/encoder_input.cpp:182`) in reverse: which
///        `EncoderRole` mod encoder @p which_encoder is.
///
/// Mod encoder 0 is `mod0`, the **lower** gold encoder (`hid/encoders.h:80-81`), which is the same
/// numbering `ui::IndicatorId` already documents at length (`ui/core/led_frame.h`).
/// @param which_encoder 0 or 1.
/// @return The role.
EncoderRole role_of(int32_t which_encoder) {
	return which_encoder == 0 ? EncoderRole::GoldLower : EncoderRole::GoldUpper;
}

} // namespace

bool is_clip_context(const ModTarget& target) {
	// `View::isClipContext` (`view.cpp:1732-1739`), all three clauses. The mod-controllable guard is
	// legacy's outer `if` (`:1734`) and is load-bearing, not decoration: a target whose timeline
	// counter resolved no mod controllable is not a clip context, and treating it as one makes
	// `target_changed` send a `sendCCWithoutModelStackForMidiFollowFeedback` legacy would never
	// send (fix round 1, Important 1 -- it was missing here, and `mod_knobs_spec.cpp`'s
	// "agrees with View::isClipContext" example now compares all five target shapes).
	//
	// Legacy compares the counter against `currentSong`; the target carries its own song for the
	// reason `ModTarget::song` records.
	return target.mod_controllable != nullptr && target.timeline_counter != nullptr
	       && target.timeline_counter != static_cast<const TimelineCounter*>(target.song);
}

// -- Pure arithmetic ---------------------------------------------------------------------------

int32_t ModKnobs::calculate_knob_pos_for_mod_encoder_turn(Kind kind, int32_t knob_pos, int32_t offset) {
	// `View::calculateKnobPosForModEncoderTurn` (`view.cpp:949-970`), transcribed.
	int32_t lower_limit = 0;
	int32_t upper_limit = 64;

	if (kind == Kind::PATCH_CABLE) {
		lower_limit = std::min(-192, knob_pos);
	}
	else {
		lower_limit = std::min(-64, knob_pos);
		if (kind == Kind::MIDI) {
			upper_limit = 63;
		}
	}
	return std::clamp(knob_pos + offset, lower_limit, upper_limit);
}

int32_t ModKnobs::convert_patch_cable_knob_pos_to_indicator_level(int32_t knob_pos) {
	// `View::convertPatchCableKnobPosToIndicatorLevel` (`view.cpp:1473-1486`), transcribed,
	// including the "so it only returns 64 once" adjustment and its comment's reasoning: the
	// indicator is centred on 64, and without this a position of both 0 and something just past it
	// would map to 64 and blink twice.
	int32_t new_knob_pos = (knob_pos + kMaxKnobPos) >> 1;
	if (new_knob_pos == 64 && knob_pos != 0) {
		new_knob_pos += knob_pos;
	}
	return new_knob_pos;
}

// -- The target --------------------------------------------------------------------------------

void ModKnobs::set_target_with_timeline(TimelineCounter* counter, bool send_midi_feedback) {
	pretend_knobs_untouched(); // `:1871`
	if (target_.song == nullptr) {
		// Legacy cannot reach this: `currentSong` is non-null by the time anything sets a mod
		// target. Here the song is injected (`set_song`), so an unwired caller is possible and is
		// made a no-op rather than a null dereference inside the stack setup.
		return;
	}
	menu::mod_target_from_timeline(target_, *target_.song, counter); // `:1868-1882`
	target_changed(send_midi_feedback);
}

void ModKnobs::set_target_without_timeline(ModControllable* controllable, ParamManager* params) {
	pretend_knobs_untouched(); // `:1903` -- legacy's own comment here is "Why again?"

	// `:1905-1907`: `setupModelStackWithSong(…)->addTimelineCounter(nullptr)
	// ->addOtherTwoThingsButNoNoteRow(modControllable, paramManager)`. Every one of those calls is
	// a field assignment on the same bytes (`model_stack.h:347-372`), and with a null timeline
	// counter there is nothing for the model to resolve, so this is the whole of it -- no
	// `menu::` transaction needed.
	target_.timeline_counter = nullptr;
	target_.note_row = nullptr;
	target_.note_row_id = 0;
	target_.mod_controllable = controllable;
	target_.param_manager = params;

	target_changed(true); // `:1920` -- this entry point always sends feedback.
}

void ModKnobs::target_changed(bool send_midi_feedback) {
	// `:1884-1885` / `:1909-1910` `setModLedStates()` + `setKnobIndicatorLevels()`: E4 task 3's
	// `leds(LedFrame&)`, which recomputes from `target()` every frame, so there is nothing to
	// invalidate here.
	//
	// `:1889-1891` / `:1914-1916`, the VU-meter sidebar refresh: not ported. `View::renderedVUMeter`
	// is a render flag of the views E5/E6 port; see the class doc.
	if (send_midi_feedback) {
		send_midi_follow_feedback(Kind::NONE, 0, kNoSelection, false, false); // `:1896` / `:1920`
	}
}

void ModKnobs::set_mod_region(uint32_t pos, uint32_t length, int32_t note_row_id) {
	target_.mod_pos = pos;                 // `:1925`
	target_.mod_length = length;           // `:1926`
	target_.mod_note_row_id = note_row_id; // `:1927`

	pretend_knobs_untouched(); // `:1929`

	// `:1931-1935` -- if holding down a note and not playing, permanently grab values from pos.
	if (length != 0 && target_.timeline_counter_set() && target_.mod_controllable != nullptr
	    && target_.param_manager != nullptr && !playbackHandler.isEitherClockActive()
	    && target_.param_manager->containsAnyMainParamCollections()) {
		menu::mod_region_grab_values(target_);
	}

	// `:1941` `setKnobIndicatorLevels()`: task 3's, as above.

	// `:1946-1947` -- don't send if midi follow feedback automation is disabled.
	if (midiEngine.midiFollowFeedbackAutomation != MIDIFollowFeedbackAutomationMode::DISABLED) {
		send_midi_follow_feedback(Kind::NONE, 0, kNoSelection, false, true);
	}
}

int32_t ModKnobs::mode() const {
	// `View::getModKnobMode` (`view.cpp:1680-1689`).
	if (target_.mod_controllable != nullptr) {
		const uint8_t* mode_pointer = target_.mod_controllable->getModKnobMode();
		if (mode_pointer != nullptr) {
			return *mode_pointer;
		}
	}
	return -1;
}

void ModKnobs::pretend_knobs_untouched() {
	// `View::pretendModKnobsUntouchedForAWhile` (`view.cpp:1951-1955`), both knobs at once.
	lowering_.pretend_knob_untouched(EncoderRole::GoldLower, now_ms_);
	lowering_.pretend_knob_untouched(EncoderRole::GoldUpper, now_ms_);
}

void ModKnobs::instrument_been_edited() {
	// `View::instrumentBeenEdited` (`view.cpp:1326-1331`).
	if (target_.timeline_counter_set()) {
		target_.timeline_counter->instrumentBeenEdited();
	}
}

void ModKnobs::notify_param_automation_occurred(ParamManager* params, bool update_mod_levels) {
	// `View::notifyParamAutomationOccurred` (`view.cpp:1691-1712`). Legacy's second disjunct --
	// `getCurrentUI() == &soundEditor && paramManager == soundEditor.currentParamManager`
	// (`:1692-1693`) -- is a UI read this layer cannot make; see the header.
	if (params != target_.param_manager) {
		return;
	}

	if (!automation_display_ms_.has_value()) { // `:1696` `isTimerSet(DISPLAY_AUTOMATION)`
		automation_updates_mod_levels_ = update_mod_levels;
		automation_display_ms_ = now_ms_ + kAutomationTimerMs; // `:1698`
	}
	else if (update_mod_levels) { // `:1701-1705`
		automation_updates_mod_levels_ = true;
	}

	if (!automation_feedback_ms_.has_value()) {
		automation_feedback_ms_ = now_ms_ + kAutomationTimerMs; // `:1708`
	}
}

// -- The turn ----------------------------------------------------------------------------------

Result ModKnobs::handle(const deluge::ui::shell::UiEvent& event, const deluge::ui::shell::UiContext& ctx) {
	now_ms_ = ctx.now_ms;

	// A mod button: `Buttons::buttonAction`'s pre-UI loop, `getCurrentUI()->modButtonAction(i, on)`
	// (`hid/buttons.cpp:100-107`), reached here as `View`'s base-class fallback. Legacy's loop ends
	// in `goto dealtWith`, so a mod button never reaches the tail whatever anyone does with it --
	// which is why this claims one unconditionally.
	if (event.kind == UiEvent::Kind::Button) {
		const int32_t which = static_cast<int32_t>(event.button) - static_cast<int32_t>(ButtonId::Mod0);
		if (which >= 0 && which < kNumModButtons) {
			mod_button_action(which, event.on);
			Result r;
			r.handled = true;
			return r;
		}
		return Result::unhandled();
	}

	// A gold knob's push: `buttonAction`'s tail, `getCurrentUI()->modEncoderButtonAction(n, on)`
	// (`hid/buttons.cpp:225-231`), again as the base-class fallback (`gui/ui/ui.cpp:43-45`).
	if (event.kind == UiEvent::Kind::EncoderButton) {
		if (event.button == ButtonId::ModEnc0 || event.button == ButtonId::ModEnc1) {
			return mod_encoder_button_action(event.button == ButtonId::ModEnc0 ? 0 : 1, event.on, ctx.shift,
			                                 ctx.held.has(ButtonId::Learn));
		}
		return Result::unhandled();
	}

	if (event.kind != UiEvent::Kind::Encoder || event.delta == 0) {
		return Result::unhandled();
	}
	int32_t which_encoder = 0;
	if (event.encoder == EncoderRole::GoldLower) {
		which_encoder = 0;
	}
	else if (event.encoder == EncoderRole::GoldUpper) {
		which_encoder = 1;
	}
	else {
		return Result::unhandled();
	}

	// `hid/encoder_input.cpp:192`: `offset * encoder.calcNextKnobSpeed(offset)`, narrowed back to
	// `int8_t` exactly as legacy narrows it (truncation toward zero, not rounding). This is the
	// call that closes the acceleration carry -- see the class doc.
	const double speed = lowering_.calc_next_knob_speed(event.encoder, event.delta, ctx.now_ms);
	const int32_t offset = static_cast<int8_t>(static_cast<double>(event.delta) * speed);

	return mod_encoder_action(which_encoder, offset, ctx.now_ms);
}

Result ModKnobs::mod_encoder_action(int32_t which_encoder, int32_t offset, uint32_t now_ms) {
	Result result;

	// `View::modEncoderAction` (`view.cpp:818-842`). The model half is one transaction; see
	// `menu::mod_encoder_turn` for why it is one call and not three.
	const ModTurn turn =
	    menu::mod_encoder_turn(target_, which_encoder, offset, &ModKnobs::calculate_knob_pos_for_mod_encoder_turn);

	if (turn.has_controllable) {
		if (!turn.param_exists) {
			// `modEncoderAction_nonExistentParam` (`:843-852`). `:849`'s
			// `setKnobIndicatorLevel(whichModEncoder)` is task 3's; the levels there are recomputed
			// from the model every frame, so the fact that this arm changed one needs no flag.
			(void)turn.caused_change;
		}
		else {
			// `:868-899` -- the popup, and the three tests that suppress it.
			if (!popup_suppressed(turn)) {
				show_popup(turn, now_ms);
			}

			if (turn.value_changed) { // `:902-904`, negated
				// `:908`. Legacy sends this *before* the write at `:923`; the write happens inside
				// `menu::mod_encoder_turn`, so it has already happened here. Unobservable: this only
				// queues a MIDI CC, and it queues the same one either way.
				send_midi_follow_feedback(turn.kind, turn.param_id, turn.new_knob_pos, true, false);

				if (turn.note_tails_changed) {
					// `:929-933` -- `uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0)`, i.e. the main pad
					// area only, guarded by "the root UI is a timeline view". `RepaintAll` is the
					// nearest thing the shell has: `SurfaceRole` names OLED panels only, so it is the
					// one action that reaches the grid at all (`shell/router.h`'s
					// `grid_repaint_requested`). The guard is a root-UI read this layer cannot make;
					// a repaint of a scene that did not need one costs a frame, not correctness.
					(void)result.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
				}

				potentially_make_it_harder_to_turn_knob(which_encoder, turn); // `:939`
			}

			// `:944-947` -- refreshing the automation editor's grid when the sound editor is open
			// over it. Both are views E5/E6 port; not reachable here.
		}
	}

	instrument_been_edited(); // `:840` -- outside the `modControllable` test, so it always runs.

	// Legacy consumes a gold-knob turn unconditionally: `interpretEncoders` calls
	// `getCurrentUI()->modEncoderAction` (`hid/encoder_input.cpp:194`) and nothing else ever sees the
	// turn afterwards. What that call reaches FIRST is the current UI's own override, with
	// `UI::modEncoderAction` (`gui/ui/ui.cpp:35-37`) forwarding to `view` as the fallback -- so this
	// `handled` is the *fallback's*, which is exactly why the router asks the scene first
	// (E4 task 3 part A; `shell/router.h`'s `route()`).
	result.handled = true;
	return result;
}

bool ModKnobs::popup_suppressed(const ModTurn& turn) const {
	// `:876-882` -- "let's see if we're editing the same param in the menu, if so, don't show
	// pop-up". `SoundEditor`'s port is `MenuScene`, so legacy's `getCurrentUI() == &soundEditor` is
	// this identity test against the one menu interpreter (`scenes/registry.h`'s `menu_scene()`) --
	// not a `kind()` comparison, which would only *infer* that the scene on top is that object
	// (fix round 1, Minor 6).
	//
	// The other two arms (`:869-874` performance view, `:885` the song browser) have no scene to
	// ask yet; the header records both as uncovered rather than guessed at.
	if (&stack_.input_top() != static_cast<const deluge::ui::shell::Scene*>(&menu_scene())) {
		return false;
	}
	const menu::Node* focused = menu_scene().focused();
	if (focused == nullptr || !focused->bind.param.valid()) {
		return false;
	}
	return focused->bind.param.kind == turn.kind && static_cast<int32_t>(focused->bind.param.index) == turn.param_id;
}

void ModKnobs::show_popup(const ModTurn& turn, uint32_t now_ms) {
	// `:887-899` -- the two patch sources, then the popup itself.
	PatchSource source1 = PatchSource::NONE;
	PatchSource source2 = PatchSource::NONE;
	if (turn.kind == Kind::PATCH_CABLE) {
		ParamDescriptor descriptor;
		descriptor.data = turn.param_id;
		source1 = descriptor.getBottomLevelSource();
		if (!descriptor.hasJustOneSource()) {
			source2 = descriptor.getTopLevelSource();
		}
	}

	// The context E4 task 1 could not receive and this class can -- see `overlay::PopupContext`.
	overlay::PopupContext context;
	// `view.cpp:1119`'s own cast, spelled as the downcast it is. Every `ModControllable` a mod
	// target can hold is a `ModControllableAudio` (`Sound`, `GlobalEffectable`); the three
	// consumers null-check it anyway.
	context.mod = static_cast<ModControllableAudio*>(target_.mod_controllable);
	if (is_clip_context(target_)) {
		// `:1122-1123`'s `getCurrentOutputType() == OutputType::MIDI_OUT` and
		// `(MIDIInstrument*)getCurrentOutput()`, resolved from the *target's* clip rather than from
		// `currentSong->getCurrentClip()`. The two agree wherever legacy's popup can be reached (the
		// knobs point at the clip being minded); taking it from the target is the narrower read, and
		// the only one available without `currentSong` (`scenes-currentSong`).
		Output* output = static_cast<Clip*>(target_.timeline_counter)->output;
		if (output != nullptr && output->type == OutputType::MIDI_OUT) {
			context.midi_instrument = static_cast<MIDIInstrument*>(output);
		}
	}
	// `context.stuttering` stays false: nothing in the port owns the stutter gesture yet, so
	// `isUIModeActive(UI_MODE_STUTTERING)` has no answer here. See `overlay::PopupContext`.

	popup_.show(turn.kind, turn.param_id, turn.new_knob_pos, source1, source2, now_ms, context);

	// The popup is a scene, so it has to be *on* the stack to be drawn. `SceneStack::push` only,
	// and only when it is not already stacked: it is `input_transparent()`, so a scene may
	// legitimately sit above it, and pushing a second copy would leak a stack entry per turn.
	for (const deluge::ui::shell::Scene* scene : stack_.scenes()) {
		if (scene->kind() == overlay::kParamPopupSceneKind) {
			return;
		}
	}
	(void)stack_.push(SceneId{.kind = overlay::kParamPopupSceneKind});
}

void ModKnobs::potentially_make_it_harder_to_turn_knob(int32_t which_encoder, const ModTurn& turn) {
	// `View::potentiallyMakeItHarderToTurnKnob` (`view.cpp:1058-1083`).
	if (which_encoder < 0 || which_encoder >= kKnobCount) {
		return;
	}
	int32_t knob_pos = turn.new_knob_pos;
	if (turn.kind == Kind::PATCH_CABLE) {
		// `:1063-1065` -- a patch cable is -128..+128; fold it into the indicator's range so "the
		// middle" is the same number the indicator blinks at.
		knob_pos = convert_patch_cable_knob_pos_to_indicator_level(knob_pos + kKnobPosOffset) - kKnobPosOffset;
	}

	if (knob_pos == 0 && turn.should_indicate_middle_value) { // `:1071`
		blink_indicator_[which_encoder] = true;
		blink_indicator_bipolar_[which_encoder] =
		    deluge::modulation::params::isParamBipolar(turn.kind, turn.param_id); // `:1072`
		// `blinkKnobIndicator` restarts the flash from the top on every qualifying turn
		// (`indicator_leds.cpp:290-294` re-seeds `levelIndicatorBlinksLeft`), so the phase origin
		// moves with it.
		blink_indicator_start_ms_[which_encoder] = now_ms_;
		// `:1074` `indicator_leds::blinkKnobIndicator(whichModEncoder, isBipolar)` -- recorded here,
		// declared by task 3's `leds()`.
		//
		// `:1077` -- "Make it harder to turn that knob away from its centred position". In *current*
		// legacy this write is inert (the encoder overhaul dropped the only reader); the shell's
		// `InputLowering::action_gold_turn` restored the working shape in plan D, so here it does
		// what it was written to do. See `InputLowering::pretend_knob_untouched`.
		lowering_.pretend_knob_untouched(role_of(which_encoder), now_ms_);
	}
	else {
		blink_indicator_[which_encoder] = false; // `:1080` `stopBlinkingKnobIndicator`
		blink_indicator_start_ms_[which_encoder] = 0;
	}
}

// -- The buttons -------------------------------------------------------------------------------

void ModKnobs::mod_button_action(int32_t which_button, bool on) {
	// `View::modButtonAction` (`view.cpp:1497-1546`).

	// `:1500-1506` -- ignore the mod button in the automation editor, except mod button 0 on
	// arranger automation view, which is how the VU meter stays toggleable there.
	if (view_.in_automation_editor && !(view_.automation_on_arranger && which_button == 0)) {
		return;
	}

	pretend_knobs_untouched(); // `:1508`

	if (target_.mod_controllable == nullptr) { // `:1510`
		return;
	}

	if (!on) {
		// `:1541-1544` -- a release goes straight through, with none of the guards above it.
		target_.mod_controllable->modButtonAction(static_cast<uint8_t>(which_button), false,
		                                          param_manager_for_timeline());
		return;
	}

	// `:1512`. `isUIModeWithinRange(modButtonUIModes)` is `ViewState::mod_button_ui_mode_in_range`;
	// see its doc for what the default cannot answer.
	if (!view_.mod_button_ui_mode_in_range && !view_.root_is_performance) {
		return;
	}

	// `:1513-1529` -- "only displaying VU meter in session view, arranger view, performance view
	// and arranger automation view".
	if (!view_.root_is_clip_minder) {
		// `:1517-1523`.
		//
		// @warning **Legacy defect (4 of 5), recorded in `src/scenes/README.md`'s "Defects this port
		//          found in shipping firmware".** `View::modButtonAction` dereferences
		//          `getModKnobMode()` **unguarded**, here at `view.cpp:1517` (a read) and again at
		//          `:1532` (a *write*). Both sit inside `if (…modControllable)` (`:1510`),
		//          `if (on)` (`:1511`) and the UI-mode/performance-view gate (`:1512`), past the
		//          automation-editor early return at `:1501-1505` -- so a release, or a press during
		//          a UI mode outside `modButtonUIModes`, reaches neither. What makes `:1532` the
		//          wider of the two is that it is **outside `!rootUIIsClipMinderScreen()`**
		//          (`:1515`): it runs on a press in a clip-minder view, where `:1517` does not.
		//          Meanwhile `View::getModKnobMode` (`:1680-1689`) null-checks the very
		//          same pointer at `:1684`, and `ModControllable::getModKnobMode`
		//          (`model/mod_controllable/mod_controllable.cpp:47-49`) really does return null for
		//          anything without mod-knob banks. Six classes override it; the one that can be an
		//          active mod controllable and does not is `CVInstrument`
		//          (`model/instrument/cv_instrument.h:31`). Established by reading, not reproduced:
		//          no device here, and no `sim/` fixture builds a CV clip. Both sites are guarded in
		//          this port, and the guard can only differ from legacy in the case legacy faults
		//          in. Not annotated in `view.cpp` itself -- this stack cites that file by line more
		//          than a hundred times.
		const uint8_t* mode_pointer = target_.mod_controllable->getModKnobMode();
		if (mode_pointer != nullptr && *mode_pointer == which_button && which_button == 0) {
			display_vu_meter_ = !display_vu_meter_;
		}
		// `:1526-1528` -- `if (renderedVUMeter) uiNeedsRendering(rootUI, 0)`, the sidebar refresh.
		// Not ported: `View::renderedVUMeter` is a render flag of the views E5/E6 port (see the
		// class doc), and nothing here can read it.
	}

	// `:1530-1532` -- change the selection *before* calling the mod-button action, so that action
	// sees the new mod-button parameter context. Legacy's `:1532` is the *unguarded write* half of
	// the defect the @warning above describes.
	if (uint8_t* mode_pointer = target_.mod_controllable->getModKnobMode(); mode_pointer != nullptr) {
		*mode_pointer = static_cast<uint8_t>(which_button);
	}

	// `:1534-1535`. This is the call that reaches `ModControllableAudio::displayOtherModKnobSettings`
	// (`model/mod_controllable/mod_controllable_audio.cpp:1651-1701`) and, through it, legacy's own
	// `view`/`display` singletons -- the back-reference the class doc's naming bullet describes.
	target_.mod_controllable->modButtonAction(static_cast<uint8_t>(which_button), true, param_manager_for_timeline());

	// `:1537-1538` `setKnobIndicatorLevels()` + `setModLedStates()`: both are `leds()`, recomputed
	// from `target()` every tick, so there is nothing to invalidate.
}

Result ModKnobs::mod_encoder_button_action(int32_t which_encoder, bool on, bool shift, bool learn) {
	// `View::modEncoderButtonAction` (`view.cpp:1332-1349`).
	Result result;
	// Legacy's tail always reaches `dealtWith` (`hid/buttons.cpp:225-233`), so the push is consumed
	// on every arm below, the two early returns included.
	result.handled = true;

	if (learn) {
		// `:1336-1338` -- "the user is trying to copy or paste, and the fact that we've ended up
		// here means they can't".
		return result;
	}
	if (target_.mod_controllable == nullptr) { // `:1340`
		return result;
	}

	if (shift && on) { // `:1342-1344`
		if (menu::mod_delete_automation(target_, which_encoder)) {
			// `:1359` `display->displayPopup(STRING_FOR_AUTOMATION_DELETED)`, as the action a scene
			// or service spells a popup with (`shell/scene.h`).
			(void)result.actions.push_back(
			    Action{.kind = Action::Kind::Popup,
			           .surface = deluge::ui::shell::SurfaceRole::Main,
			           .popup_string = static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_AUTOMATION_DELETED)});
		}
		return result;
	}

	// `modEncoderButtonAction_changeModControllable` (`:1363-1377`).
	if (menu::mod_encoder_button(target_, which_encoder, on)) { // `:1368-1370`
		instrument_been_edited();                               // `:1371`
	}
	// `:1373` `setKnobIndicatorLevels()` -- `leds()` recomputes it, as everywhere else here.
	if (&stack_.input_top() == static_cast<const deluge::ui::shell::Scene*>(&menu_scene())) {
		// `:1374-1376` `soundEditor.getCurrentMenuItem()->readValueAgain()`. Same identity test as
		// `popup_suppressed`: `MenuScene` is `SoundEditor`'s port.
		menu_scene().read_value_again();
	}
	return result;
}

ParamManagerForTimeline* ModKnobs::param_manager_for_timeline() const {
	// Legacy's `(ParamManagerForTimeline*)activeModControllableModelStack.paramManager`
	// (`view.cpp:1535`, `:1543`) -- a C cast on a possibly-null pointer, spelled here as the
	// downcast helper `ParamManager` already provides, with the null kept null.
	return target_.param_manager != nullptr ? target_.param_manager->toForTimeline() : nullptr;
}

// -- The LEDs and the indicators -----------------------------------------------------------------

ModKnobs::IndicatorLevel ModKnobs::indicator_level(const ModIndicatorInfo& info, bool stuttering) {
	// `View::setKnobIndicatorLevel`'s arithmetic (`view.cpp:1413-1468`), over reads
	// `menu::mod_indicator_info` already made.
	IndicatorLevel out{};

	if (info.has_auto_param) {
		int32_t knob_pos = info.knob_pos;
		// `:1419-1427` -- the same "never drag a position that is already below the limit up"
		// clamp `calculateKnobPosForModEncoderTurn` uses, with the same two limits.
		const int32_t lower_limit = info.kind == Kind::PATCH_CABLE ? std::min(-192, knob_pos) : std::min(-64, knob_pos);
		knob_pos = std::clamp(knob_pos, lower_limit, 64);

		if (info.quantized_stutter && !stuttering) { // `:1428-1446`
			// The five stutter rate buckets, each snapped to the position that lights that many of
			// the four segments: 4ths none, 8ths one, 16ths two, 32nds three, 64ths all four.
			if (knob_pos < -39) {
				knob_pos = -64;
			}
			else if (knob_pos < -14) {
				knob_pos = -32;
			}
			else if (knob_pos < 14) {
				knob_pos = 0;
			}
			else if (knob_pos < 39) {
				knob_pos = 32;
			}
			else {
				knob_pos = 64;
			}
		}
		knob_pos += kKnobPosOffset; // `:1447`

		if (info.kind == Kind::PATCH_CABLE) { // `:1449-1451`
			knob_pos = convert_patch_cable_knob_pos_to_indicator_level(knob_pos);
		}
		out.declared = true;
		out.level = static_cast<uint8_t>(knob_pos);
		out.bipolar = info.bipolar;
		return out;
	}

	if (info.non_existent_param) { // `:1454-1458`
		out.declared = true;
		out.level = static_cast<uint8_t>(info.non_existent_knob_pos + kKnobPosOffset);
		out.bipolar = false;
		return out;
	}
	if (info.patch_cable_default) { // `:1460-1465`
		out.declared = true;
		out.level = 64; // "equals 0 (midpoint) in -128 to +128 range"
		out.bipolar = true;
		return out;
	}

	// Legacy's fourth case: no `autoParam`, `paramId != 255`, and it *is* just a param.
	//
	// @warning **Legacy defect (5 of 5), recorded in `src/scenes/README.md`'s "Defects this port
	//          found in shipping firmware".** `View::setKnobIndicatorLevel` declares
	//          `int32_t knobPos;` with no initialiser (`view.cpp:1410`) and assigns it on three of
	//          its four paths -- `:1413`, `:1454`, `:1460` -- leaving it **uninitialised** on this
	//          one. It is then passed to `indicator_leds::setKnobIndicatorLevel` at `:1468`, which
	//          truncates it to `uint8_t` and lights the gold knob's four-segment column from it:
	//          undefined behaviour with a visible symptom. There is no behaviour to preserve, so
	//          nothing is declared here and the knob keeps whatever it was last showing -- the
	//          shell's own rule for an undeclared indicator (`Shell::flush`) and the only defined
	//          thing to do. Not annotated in `view.cpp` itself; see the sibling @warning above.
	return out;
}

bool ModKnobs::indicator_blinking(int32_t which_encoder) const {
	if (which_encoder < 0 || which_encoder >= kKnobCount || !blink_indicator_[which_encoder]) {
		return false;
	}
	return (now_ms_ - blink_indicator_start_ms_[which_encoder]) < deluge::ui::kKnobIndicatorBlinkMs;
}

void ModKnobs::declare_indicators(deluge::ui::LedFrame& frame) {
	// `View::setKnobIndicatorLevels` (`view.cpp:1379-1401`).
	// `:1380-1382` -- legacy returns early when there is no root UI, under a comment that reads, in
	// full, "What's this?". An empty scene stack is this port's "no root UI".
	if (stack_.depth() == 0) {
		return;
	}
	if (view_.in_automation_editor) {
		// `:1385-1388` -- "don't update knob indicator levels when you're in automation editor".
		// Legacy calls `automationView.displayAutomation()` instead, which is a view-side redraw
		// and not ported; declaring nothing leaves both columns exactly as they were, which is what
		// legacy's early return does too.
		return;
	}

	if (target_.mod_controllable == nullptr) {
		// `:1398-1400` `indicator_leds::clearKnobIndicatorLevels()`, which is
		// `setKnobIndicatorLevel(i, 0)` for both (`indicator_leds.cpp:318-322`).
		for (int32_t k = 0; k < kKnobCount; k++) {
			(void)frame.indicator(static_cast<deluge::ui::IndicatorId>(k), deluge::ui::knob_indicator_levels(0, false));
		}
		return;
	}

	for (int32_t k = 0; k < kKnobCount; k++) { // `:1390-1396`
		if (indicator_blinking(k)) {
			// `:1392` skips a blinking knob so legacy's own blink timer keeps driving it; here that
			// timer is a phase, so the blink frame is declared instead of nothing.
			(void)frame.indicator(
			    static_cast<deluge::ui::IndicatorId>(k),
			    deluge::ui::knob_indicator_blink(now_ms_ - blink_indicator_start_ms_[k], blink_indicator_bipolar_[k]));
			continue;
		}
		const IndicatorLevel level = indicator_level(menu::mod_indicator_info(target_, k), /*stuttering=*/false);
		if (level.declared) {
			(void)frame.indicator(static_cast<deluge::ui::IndicatorId>(k),
			                      deluge::ui::knob_indicator_levels(level.level, level.bipolar));
		}
	}
}

void ModKnobs::leds(deluge::ui::LedFrame& frame) {
	// `View::setModLedStates`' final block, "Sort out actual mod LEDs" (`view.cpp:1659-1678`). The
	// three LED groups above it (AFFECT_ENTIRE, CLIP_VIEW, SESSION_VIEW) are root-view questions
	// and are not this service's -- see the class doc.
	const bool its_the_song = !is_clip_context(target_); // `:1559`
	const int32_t mod_knob_mode = mode();                // `:1660`

	constexpr deluge::ui::LedState kOff{deluge::ui::LedState::Kind::Off, 0, 0};
	constexpr deluge::ui::LedState kOn{deluge::ui::LedState::Kind::On, 0, 0};
	// `indicator_leds::blinkLed(led)`'s defaults: `numBlinks` 255 (i.e. forever) and
	// `blinkingType` 0, whose period is `kFlashTime` (`definitions_cxx.hpp:159`) -- already carried
	// by the toolkit as `deluge::ui::kFlashTime` (`ui/core/metrics.h:25`), which is the same
	// constant `MenuScene::leds` blinks BACK and LEARN at.
	constexpr deluge::ui::LedState kBlink{deluge::ui::LedState::Kind::Blink,
	                                      static_cast<uint16_t>(deluge::ui::kFlashTime), 0};

	for (int32_t i = 0; i < kNumModButtons; i++) { // `:1662`
		const bool on = (i == mod_knob_mode);      // `:1663`
		deluge::ui::LedState state = kOff;
		if (its_the_song && on && mod_knob_mode == 0 && display_vu_meter_) {
			state = kBlink; // `:1666-1667` -- the volume mod LED blinks while the VU meter is up
		}
		else if (view_.in_automation_editor) {
			state = kOff; // `:1670-1671` -- every mod LED off in the automation editor
		}
		else {
			state = on ? kOn : kOff; // `:1674-1675`
		}
		(void)frame.set(static_cast<deluge::ui::LedId>(static_cast<uint8_t>(ButtonId::Mod0) + i), state);
	}

	declare_indicators(frame);
}

void ModKnobs::send_midi_follow_feedback(Kind kind, int32_t param_id, int32_t knob_pos, bool have_param,
                                         bool is_automation) {
	// `View::sendMidiFollowFeedback` (`view.cpp:1713-1731`).
	if (!midiFollow.isFeedbackEnabled()) {
		return;
	}
	if (!is_clip_context(target_)) { // `:1716` -- don't send feedback for song params
		return;
	}
	if (have_param) {
		const int32_t cc_number = midiFollow.getCCFromParam(kind, param_id);
		if (cc_number != MIDI_CC_NONE) {
			midiFollow.sendCCForMidiFollowFeedback(cc_number, knob_pos);
		}
	}
	else {
		midiFollow.sendCCWithoutModelStackForMidiFollowFeedback(is_automation);
	}
}

// -- Time --------------------------------------------------------------------------------------

void ModKnobs::tick(uint32_t now_ms, uint32_t /*dt_ms*/) {
	now_ms_ = now_ms;

	if (automation_display_ms_.has_value() && static_cast<int32_t>(now_ms - *automation_display_ms_) >= 0) {
		// `TimerName::DISPLAY_AUTOMATION`'s callback (`gui/ui_timer_manager.cpp:157-171`) is
		// `automationView.displayAutomation()` / `view.displayAutomation()`, both view-side and not
		// ported. Disarming is all this can do; `automation_display_pending()` is the fact task 3's
		// `leds()` reads, exactly as legacy reads `isTimerSet(DISPLAY_AUTOMATION)`.
		automation_display_ms_.reset();
	}

	if (automation_feedback_ms_.has_value() && static_cast<int32_t>(now_ms - *automation_feedback_ms_) >= 0) {
		automation_feedback_ms_.reset();
		// `TimerName::SEND_MIDI_FEEDBACK_FOR_AUTOMATION`'s callback (`ui_timer_manager.cpp:173-176`):
		// only while playback is active, and only when feedback automation is not disabled. The
		// LOW/MEDIUM/HIGH send-rate re-arm below those lines is `ui_timer_manager`'s own and is not
		// ported here -- it is not part of any `View` function this task cites.
		if (playbackHandler.isEitherClockActive()
		    && midiEngine.midiFollowFeedbackAutomation != MIDIFollowFeedbackAutomationMode::DISABLED) {
			send_midi_follow_feedback(Kind::NONE, 0, kNoSelection, false, true);
		}
	}
}

std::optional<uint32_t> ModKnobs::next_timer() const {
	std::optional<uint32_t> next;
	const auto consider = [&](const std::optional<uint32_t>& deadline) {
		if (!deadline.has_value()) {
			return;
		}
		const int32_t remaining = static_cast<int32_t>(*deadline - now_ms_);
		const uint32_t clamped = remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
		if (!next.has_value() || clamped < *next) {
			next = clamped;
		}
	};
	consider(automation_display_ms_);
	consider(automation_feedback_ms_);
	// A blinking indicator needs a frame every half-period, and one more when the flash ends and
	// the ordinary level comes back (`indicator_leds.cpp:304-312`'s 20 ms re-arm).
	for (int32_t k = 0; k < kKnobCount; k++) {
		if (indicator_blinking(k)) {
			const uint32_t elapsed = now_ms_ - blink_indicator_start_ms_[k];
			const uint32_t to_next_half =
			    deluge::ui::kKnobIndicatorBlinkHalfMs - (elapsed % deluge::ui::kKnobIndicatorBlinkHalfMs);
			if (!next.has_value() || to_next_half < *next) {
				next = to_next_half;
			}
		}
	}
	return next;
}

bool ModKnobs::blink_indicator(int32_t which_encoder) const {
	return which_encoder >= 0 && which_encoder < kKnobCount && blink_indicator_[which_encoder];
}

bool ModKnobs::blink_indicator_bipolar(int32_t which_encoder) const {
	return which_encoder >= 0 && which_encoder < kKnobCount && blink_indicator_bipolar_[which_encoder];
}

} // namespace deluge::scenes::services
