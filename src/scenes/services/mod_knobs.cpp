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
/// @brief `ModKnobs`. See `mod_knobs.h` for the interface and the layering.

#include "scenes/services/mod_knobs.h"

#include "controls/board.h"
#include "controls/knob_math.h"
#include "controls/pages.h"
#include "controls/section.h"

#include "scenes/bind/knob_assignment.h"
#include "scenes/bind/page_sources.h"
#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/registry.h"
#include "scenes/services/knob_push.h"

#include "shell/input_lowering.h"

#include "effects/global_effectable/params.h"
#include "gui/l10n/strings.h"
#include "instruments/deluge/cables.h"
#include "instruments/deluge/params.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "model/project_bridge/param_ids.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "playback/playback_handler.h"

#include <algorithm>
#include <cstdint>

namespace deluge::scenes::services {

using deluge::modulation::params::Kind;
using deluge::project::ParamAddress;
using deluge::project::ParamSpace;
using deluge::ui::EncoderRole;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::UiEvent;

namespace {

/// @brief Which `EncoderRole` mod encoder @p which_encoder is: 0 is `mod0`, the lower (`hid/encoders.h`'s
/// `mod0`/`mod1`).
EncoderRole role_of(int32_t which_encoder) {
	return which_encoder == 0 ? EncoderRole::GoldLower : EncoderRole::GoldUpper;
}

/// @brief The descriptor of the param @p assignment names, from its owner's engine table; null for a cable, a MIDI
///        CC or an expression dimension.
const deluge::plugin::ParamInfo* info_of(const ParamAddress& address, Kind kind) {
	if (address.space != ParamSpace::ENGINE || deluge::instrument::is_cable_depth(address.id)) {
		return nullptr;
	}
	return kind == Kind::UNPATCHED_GLOBAL ? deluge::plugin::find_param(deluge::global_effectable::kParams, address.id)
	                                      : deluge::plugin::find_param(deluge::instrument::kParams, address.id);
}

/// @brief The param @p address names as the engine's collections number it, for the popup, the menu's same-param
///        test and MIDI follow.
int32_t legacy_id(const ParamAddress& address, Kind kind) {
	switch (address.space) {
	case ParamSpace::MIDI_CC:
	case ParamSpace::EXPRESSION:
		return static_cast<int32_t>(address.id.value);
	case ParamSpace::ENGINE:
		break;
	}
	if (deluge::instrument::is_cable_depth(address.id)) {
		return static_cast<int32_t>(deluge::bridge::cableWordOf(address.id));
	}
	if (kind == Kind::UNPATCHED_GLOBAL) {
		return deluge::global_effectable::runtime_index(address.id).value_or(0);
	}
	const std::optional<deluge::instrument::RuntimeSlot> slot = deluge::instrument::runtime_slot(address.id);
	return slot ? slot->index : 0;
}

} // namespace

bool is_clip_context(const bind::KnobFocus& focus) {
	// `View::knobsOnClipContext`: an owner, in a clip.
	if (const auto* sound = std::get_if<bind::SoundFocus>(&focus.owner)) {
		return sound->clip.has_value();
	}
	if (const auto* chain = std::get_if<bind::ChainFocus>(&focus.owner)) {
		return chain->clip.has_value();
	}
	if (const auto* midi = std::get_if<bind::MidiFocus>(&focus.owner)) {
		return midi->clip.has_value();
	}
	if (const auto* empty = std::get_if<bind::NoControlsFocus>(&focus.owner)) {
		return empty->clip_id.has_value();
	}
	return false;
}

// -- What the knobs point at -------------------------------------------------------------------

bool ModKnobs::bound() const {
	return song_ != nullptr && menu::host_binds(*song_);
}

const effects::EffectStates* ModKnobs::owner_effects() const {
	return services::owner_effects(*focus_, menu::host_runtime_facts());
}

PushInputs ModKnobs::push_inputs(bool mode_held) const {
	return {.button_held = mode_held, .clip_context = is_clip_context(*focus_), .board = board_};
}

bool ModKnobs::owner_stutter_quantized() const {
	const effects::EffectStates* fx = owner_effects();
	const effects::EffectStates* song =
	    menu::host_runtime_facts().chain_effects(project::ChainAddress{.kind = project::ChainAddress::Kind::SONG});
	return fx != nullptr && song != nullptr && stutter_quantized(*fx, *song);
}

void ModKnobs::set_focus(const bind::KnobFocus& focus, bool send_midi_feedback) {
	// The mod region, the compressor view and the VU meter are the knobs' own view state, which a new focus keeps.
	focus_->owner = focus.owner;
	if (focus_changed_) {
		focus_changed_();
	}
	selecting_cc_ = false;
	if (send_midi_feedback) {
		// `View::setActiveModControllableTimelineCounter`, `View::setActiveModControllableWithoutTimelineCounter`.
		send_midi_follow_feedback(Kind::NONE, 0, kNoSelection, false, false);
	}
}

void ModKnobs::set_mod_region(uint32_t pos, uint32_t length, int32_t note_row_id) {
	focus_->region = {.pos = pos, .length = length, .note_row_id = note_row_id}; // `View::setModRegion`
	if (focus_changed_) {
		focus_changed_();
	}
	// Holding a note on a stopped clip grabs its params' values from there.
	if (length != 0 && is_clip_context(*focus_) && !playbackHandler.isEitherClockActive() && bound()) {
		(void)menu::host_controls_sink().grab_region(*focus_, pos);
	}
	// Unless midi follow feedback automation is disabled.
	if (midiEngine.midiFollowFeedbackAutomation != MIDIFollowFeedbackAutomationMode::DISABLED) {
		send_midi_follow_feedback(Kind::NONE, 0, kNoSelection, false, true);
	}
}

int32_t ModKnobs::mode() const {
	// `View::getModKnobMode`.
	if (song_ == nullptr || bind::has_no_knobs(*focus_)) {
		return -1;
	}
	return bind::knob_mode(song_->project, *focus_, board_);
}

void ModKnobs::notify_param_automation_occurred(const ParamAddress& owner, bool update_mod_levels) {
	// `View::notifyParamAutomationOccurred`, for the knobs' owner's params.
	const std::optional<ParamAddress> ours = bind::knob_owner(*focus_);
	if (!ours || !(ours->owner == owner.owner) || ours->clip_id != owner.clip_id) {
		return;
	}
	if (!automation_display_ms_.has_value()) { // `isTimerSet(DISPLAY_AUTOMATION)`
		automation_updates_mod_levels_ = update_mod_levels;
		automation_display_ms_ = now_ms_ + kAutomationTimerMs;
	}
	else if (update_mod_levels) {
		automation_updates_mod_levels_ = true;
	}
	if (!automation_feedback_ms_.has_value()) {
		automation_feedback_ms_ = now_ms_ + kAutomationTimerMs;
	}
}

// -- The turn ----------------------------------------------------------------------------------

Result ModKnobs::handle(const deluge::ui::shell::UiEvent& event, const deluge::ui::shell::UiContext& ctx) {
	now_ms_ = ctx.now_ms;

	// A mod button: `Buttons::buttonAction`'s mod-button loop ends in `goto dealtWith`, so it is always claimed.
	if (event.kind == UiEvent::Kind::Button) {
		const int32_t which = static_cast<int32_t>(event.button) - static_cast<int32_t>(ButtonId::Mod0);
		if (which >= 0 && event.button <= ButtonId::Mod7) {
			// Claimed even when the board has no such section: the button is a mod button all the same.
			if (const std::optional<controls::Logical> section = controls::map_input(
			        board_, {controls::Physical::Kind::SECTION_BUTTON, static_cast<uint8_t>(which)})) {
				section_button(section->index, event.on, guest_);
			}
			Result r = guest_.take_result();
			r.handled = true;
			return r;
		}
		return Result::unhandled();
	}

	// A gold knob's push: `buttonAction`'s tail (`hid/buttons.cpp`'s `buttonAction`).
	if (event.kind == UiEvent::Kind::EncoderButton) {
		if (event.button == ButtonId::ModEnc0 || event.button == ButtonId::ModEnc1) {
			const uint8_t physical = event.button == ButtonId::ModEnc0 ? 0 : 1;
			const int32_t current = mode();
			const bool mode_held =
			    current >= 0 && ctx.held.has(static_cast<ButtonId>(static_cast<uint8_t>(ButtonId::Mod0) + current));
			if (const std::optional<controls::Logical> slot =
			        controls::map_input(board_, {controls::Physical::Kind::SLOT_PRESS, physical})) {
				push(slot->index, event.on,
				     {.shift = ctx.shift, .learn = ctx.held.has(ButtonId::Learn), .mode_held = mode_held}, guest_);
			}
			Result r = guest_.take_result();
			r.handled = true;
			return r;
		}
		return Result::unhandled();
	}

	if (event.kind != UiEvent::Kind::Encoder || event.delta == 0) {
		return Result::unhandled();
	}
	// The board numbers its encoders as `mod0` (the lower) and `mod1` (the upper).
	uint8_t physical = 0;
	if (event.encoder == EncoderRole::GoldLower) {
		physical = 0;
	}
	else if (event.encoder == EncoderRole::GoldUpper) {
		physical = 1;
	}
	else {
		return Result::unhandled();
	}

	// `interpretEncoders` (`origin/main:src/deluge/hid/encoder_input.cpp`): `offset *
	// encoder.calcNextKnobSpeed(offset)`, narrowed back to `int8_t` as legacy narrows it (truncation toward zero). A
	// host's lowered turn is accelerated already.
	const double speed =
	    event.lowered ? 1.0 : lowering_.calc_next_knob_speed(event.encoder, event.delta, ctx.acceleration_clock_ms);
	const int32_t offset = static_cast<int8_t>(static_cast<double>(event.delta) * speed);
	// Legacy consumes a gold-knob turn unconditionally.
	if (const std::optional<controls::Logical> slot =
	        controls::map_input(board_, {controls::Physical::Kind::SLOT_ENCODER, physical})) {
		turn(slot->index, offset, ctx.now_ms, guest_);
	}
	Result r = guest_.take_result();
	r.handled = true;
	return r;
}

void ModKnobs::turn(int32_t knob, int32_t offset, uint32_t now_ms, KnobPresentation& out) {
	// `View::modEncoderAction`.
	if (!bound() || std::holds_alternative<std::monostate>(focus_->owner)) {
		return;
	}
	turn_knob(knob, offset, now_ms, out);
	// Every turn on a clip marks its instrument edited, whatever it wrote.
	if (is_clip_context(*focus_)) {
		(void)menu::host_controls_sink().instrument_edited(*focus_);
	}
}

void ModKnobs::turn_knob(int32_t which_encoder, int32_t offset, uint32_t now_ms, KnobPresentation& out) {
	const bind::RuntimeFacts& facts = menu::host_runtime_facts();
	controls::Sink& sink = menu::host_controls_sink();
	const bind::KnobAssignment assignment = bind::knob_assignment(song_->project, *focus_, which_encoder, board_);

	if (const auto* comp = std::get_if<bind::CompArm>(&assignment.target)) {
		// The song's compressor knobs edit settings, not params (`modEncoderActionForNonExistentParam`).
		const project::ChainAddress song_chain{.kind = project::ChainAddress::Kind::SONG};
		const effects::EffectStates* fx = facts.chain_effects(song_chain);
		if (fx != nullptr) {
			const CompTurn turn = comp_turn(*comp, *fx, offset);
			if (sink.write_effect(song_chain, turn.path, turn.value)) {
				out.setting_indicator(which_encoder, turn.knob_pos);
				out.notification(turn.title, turn.value_text);
			}
		}
		return;
	}
	const auto* address = std::get_if<ParamAddress>(&assignment.target);
	if (address == nullptr) {
		return;
	}

	// `View::modEncoderAction_existentParam`. A knob on a param its owner does not hold yet reads 0: the write
	// makes it.
	const deluge::plugin::ParamInfo* info = info_of(*address, assignment.kind);
	const bool at_region = focus_->region.pos != 0xFFFFFFFF;
	const std::optional<int32_t> value = facts.param_value_at(*address, focus_->region.pos);
	ModTurn turn;
	turn.kind = assignment.kind;
	turn.param_id = legacy_id(*address, assignment.kind);
	turn.old_knob_pos = controls::knob_pos_of(*address, info, value.value_or(0));
	turn.new_knob_pos = controls::turn(controls::knob_kind(*address), turn.old_knob_pos, offset);

	// The popup, unless the presentation shows the value its own way.
	if (!out.suppress_value_popup(turn)) {
		show_popup(turn, now_ms, out);
	}
	if (turn.new_knob_pos == turn.old_knob_pos) {
		return;
	}

	const auto* sound = std::get_if<bind::SoundFocus>(&focus_->owner);
	const bool tails_before = sound != nullptr && sound->clip && facts.allows_note_tails(sound->slot, sound->clip);
	const int32_t new_value = controls::value_of_knob(*address, info, turn.new_knob_pos);
	const int32_t pos = at_region ? static_cast<int32_t>(focus_->region.pos) : 0;
	if (!sink.write_param(*address, new_value, pos, static_cast<int32_t>(focus_->region.length),
	                      controls::Resolution::KNOB)) {
		return; // refused: nothing changed, so nothing is fed back
	}
	out.value_written();
	turn.value_changed = true;
	turn.note_tails_changed =
	    sound != nullptr && sound->clip && tails_before != facts.allows_note_tails(sound->slot, sound->clip);

	turn.should_indicate_middle_value =
	    controls::indicates_middle(*address, info, owner_stutter_quantized(), facts.stutter_active());

	send_midi_follow_feedback(turn.kind, turn.param_id, turn.new_knob_pos, true, false);
	if (turn.note_tails_changed) {
		out.note_tails_changed();
	}
	potentially_make_it_harder_to_turn_knob(which_encoder, turn, out);
}

void ModKnobs::show_popup(const ModTurn& turn, uint32_t now_ms, KnobPresentation& out) {
	// The two patch sources, then the popup itself.
	PatchSource source1 = PatchSource::NONE;
	PatchSource source2 = PatchSource::NONE;
	if (turn.kind == Kind::PATCH_CABLE) {
		const deluge::synth::CableTarget descriptor{static_cast<uint32_t>(turn.param_id)};
		source1 = descriptor.bottom_source();
		if (!descriptor.has_just_one_source()) {
			source2 = descriptor.top_source();
		}
	}

	// What the popup reads of the owner: its effects' filter modes, its stutter, a MIDI track's CC names.
	// Copied, not pointed at: the popup shows it on a later tick.
	overlay::PopupContext context;
	if (const effects::EffectStates* fx = owner_effects()) {
		context.filter = fx->filter;
	}
	context.stutter_quantized = owner_stutter_quantized();
	if (const auto* midi = std::get_if<bind::MidiFocus>(&focus_->owner); midi != nullptr && midi->clip) {
		context.midi_clip = true;
		const project::DocOutput* output = song_->project.output_structure.find(midi->track.output_id);
		if (output != nullptr && turn.kind == Kind::MIDI) {
			if (const auto label = output->cc_labels.find(static_cast<uint8_t>(turn.param_id));
			    label != output->cc_labels.end()) {
				context.cc_label = label->second;
			}
		}
	}
	context.stuttering = menu::host_runtime_facts().stutter_active();
	out.show_value_popup(turn, source1, source2, context, now_ms);
}

void ModKnobs::potentially_make_it_harder_to_turn_knob(int32_t which_encoder, const ModTurn& turn,
                                                       KnobPresentation& out) {
	// `View::potentiallyMakeItHarderToTurnKnob`.
	if (which_encoder < 0 || which_encoder >= board_.slots) {
		return;
	}
	int32_t knob_pos = turn.new_knob_pos;
	if (turn.kind == Kind::PATCH_CABLE) {
		// A patch cable is -128..+128; fold it into the indicator's range so "the
		// middle" is the same number the indicator blinks at.
		knob_pos = controls::fold_cable(knob_pos + kKnobPosOffset) - kKnobPosOffset;
	}

	if (knob_pos == 0 && turn.should_indicate_middle_value) {
		// `indicator_leds::blinkKnobIndicator(whichModEncoder, isBipolar)`. `main`'s
		// `pretendModKnobsUntouchedForAWhile` here writes `timeModEncoderLastTurned`, which nothing reads, so it has no
		// port.
		out.blink_indicator(which_encoder, deluge::modulation::params::isParamBipolar(turn.kind, turn.param_id));
	}
	else {
		out.stop_blink_indicator(which_encoder); // `stopBlinkingKnobIndicator`
	}
}

// -- The buttons -------------------------------------------------------------------------------

void ModKnobs::section_button(int32_t which_button, bool on, KnobPresentation& out) {
	// `View::modButtonAction`.

	// The automation editor ignores a mod button, except mod button 0 on the arranger's, which keeps
	// the VU meter toggleable there.
	if (view_.in_automation_editor && !(view_.automation_on_arranger && which_button == 0)) {
		return;
	}
	if (bind::has_no_knobs(*focus_) || !bound()) {
		return;
	}
	controls::Sink& sink = menu::host_controls_sink();

	// A MIDI track choosing its knob's CC: a mod button's press changes nothing, and its release ends the choice.
	if (selecting_cc_) {
		if (on) {
			return;
		}
		(void)sink.midi_cc_select(*focus_, selecting_knob_, false);
		selecting_cc_ = false;
		out.cancel_popup();
		return;
	}

	// The button's rules (`View::modButtonAction`, `ModControllable::modButtonAction`): the VU meter's toggle and the
	// mode change come before the stutter's end, which the popup reads, and a press the rules ignore does nothing.
	const controls::SectionResult section = controls::resolve_section(
	    *focus_, static_cast<uint8_t>(mode()), which_button, on,
	    {.root_is_performance = view_.root_is_performance,
	     .mod_button_ui_mode_in_range = view_.mod_button_ui_mode_in_range,
	     .root_is_clip_minder = view_.root_is_clip_minder,
	     .keeps_stutter = controls::section_keeps_stutter(song_->project, *focus_, static_cast<uint32_t>(which_button),
	                                                      bind::app_page_sources(menu::host_runtime_facts(), board_))});
	if (on && !section.taken) {
		return;
	}
	if (section.toggle_vu_meter) {
		focus_->vu_meter = !focus_->vu_meter;
	}
	if (on && section.taken && !view_.root_is_clip_minder) {
		out.vu_sidebar_changed(); // the VU meter is offered only outside a clip minder
	}
	if (section.change_mode) {
		(void)sink.knob_mode(*controls::knob_mode_owner(*focus_), static_cast<uint8_t>(which_button));
	}
	if (section.end_stutter) {
		(void)sink.stutter(*focus_, false);
	}
	if (!section.taken) {
		out.cancel_popup();
		return;
	}
	if (const std::string text = mod_button_popup(song_->project, *focus_, which_button, push_inputs(false));
	    !text.empty()) {
		out.popup_text(text);
	}
	out.mode_changed();
}

void ModKnobs::push(int32_t which_encoder, bool on, const PushContext& ctx, KnobPresentation& out) {
	// `View::modEncoderButtonAction`. Legacy's tail always reaches `dealtWith`.
	if (ctx.learn || bind::has_no_knobs(*focus_) || !bound()) {
		return;
	}
	controls::Sink& sink = menu::host_controls_sink();

	if (ctx.shift && on) { // `View::modEncoderButtonAction_deleteAutomation`
		const std::optional<ParamAddress> address = knob_address(which_encoder);
		if (address && menu::host_runtime_facts().param_value(*address).has_value()
		    && sink.delete_knob_automation(*address)) {
			out.popup(deluge::l10n::String::STRING_FOR_AUTOMATION_DELETED);
		}
		return;
	}

	// `View::modEncoderButtonAction_changeModControllable`: the push's table, sent to the sink.
	const KnobPush push = knob_push(song_->project, *focus_, which_encoder, on, push_inputs(ctx.mode_held));
	if (push.midi_cc_select) {
		// The instrument takes it from a clip minder with no UI mode active; a release ends it.
		selecting_cc_ = sink.midi_cc_select(*focus_, which_encoder, on) && on;
		selecting_knob_ = which_encoder;
	}
	// A push that edited the instrument marks it, on a clip.
	if (apply_push(menu::host_write_sink(), sink, *focus_, push) && push.edited && is_clip_context(*focus_)) {
		(void)sink.instrument_edited(*focus_);
	}
	if (push.comp) {
		focus_->comp = *push.comp;
	}
	if (!push.popup.empty()) {
		out.popup_text(push.popup);
	}
	else if (!push.notice.empty()) {
		out.notice_text(push.notice);
	}
	if (&stack_.input_top() == static_cast<const deluge::ui::shell::Scene*>(&menu_scene())) {
		menu_scene().read_value_again();
	}
	out.indicators_changed();
}

std::optional<ParamAddress> ModKnobs::knob_address(int32_t which_encoder) const {
	if (song_ == nullptr || std::holds_alternative<std::monostate>(focus_->owner)) {
		return std::nullopt;
	}
	const bind::KnobAssignment assignment = bind::knob_assignment(song_->project, *focus_, which_encoder, board_);
	if (const auto* address = std::get_if<ParamAddress>(&assignment.target)) {
		return *address;
	}
	return std::nullopt;
}

// -- The LEDs and the indicators -----------------------------------------------------------------

ModKnobs::IndicatorLevel ModKnobs::indicator_level(const ModIndicatorInfo& info, bool stuttering) {
	// `View::setKnobIndicatorLevel`'s arithmetic, over reads
	// the reads `indicator_info` made.
	IndicatorLevel out{};

	if (info.has_auto_param) {
		int32_t knob_pos = info.knob_pos;
		// The same "never drag a position that is already below the limit up"
		// clamp `calculateKnobPosForModEncoderTurn` uses, with the same two limits.
		const int32_t lower_limit = info.kind == Kind::PATCH_CABLE ? std::min(-192, knob_pos) : std::min(-64, knob_pos);
		knob_pos = std::clamp(knob_pos, lower_limit, 64);

		if (info.quantized_stutter && !stuttering) {
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
		knob_pos += kKnobPosOffset;

		if (info.kind == Kind::PATCH_CABLE) {
			knob_pos = controls::fold_cable(knob_pos);
		}
		out.declared = true;
		// **The `uint8_t` is legacy's, not a narrowing this port introduces**, and the E4 final
		// review asked for the argument at the code rather than only on `IndicatorLevel::level`.
		// `View::setKnobIndicatorLevel` carries `int32_t knobPos` right to its last line and hands
		// it to `indicator_leds::setKnobIndicatorLevel(uint8_t whichKnob, uint8_t level, bool)`
		// (`View::setKnobIndicatorLevel` calling `indicator_leds::setKnobIndicatorLevel`), so the value is
		// truncated by an implicit conversion at exactly this point. The `>> 5` and `& 31` the
		// review cites (`indicator_leds::actuallySetKnobIndicatorLevel`) run on that already-truncated
		// parameter, not on the `int32_t` -- there is no wider legacy arithmetic to be faithful to.
		// An explicit cast where legacy has an implicit one is the same value and a visible one.
		out.level = static_cast<uint8_t>(knob_pos);
		out.bipolar = info.bipolar;
		return out;
	}

	if (info.non_existent_param) {
		out.declared = true;
		// Same implicit-narrowing point as above.
		out.level = static_cast<uint8_t>(info.non_existent_knob_pos + kKnobPosOffset);
		out.bipolar = false;
		return out;
	}
	if (info.patch_cable_default) {
		out.declared = true;
		out.level = 64; // "equals 0 (midpoint) in -128 to +128 range"
		out.bipolar = true;
		return out;
	}

	// Legacy's fourth case: no `autoParam`, `paramId != 255`, and it *is* just a param.
	//
	// @warning **Legacy defect (the fifth entry), recorded in `src/scenes/README.md`'s "Defects this port
	//          found in shipping firmware".** `View::setKnobIndicatorLevel` declares
	//          `int32_t knobPos;` with no initialiser (`View::setKnobIndicatorLevel`) and assigns it on three of
	//          its four paths -- an `autoParam`'s, a non-existent param's and a patch cable's default -- leaving it
	//          **uninitialised** on this one. It is then passed to `indicator_leds::setKnobIndicatorLevel`, which
	//          truncates it to `uint8_t` and lights the gold knob's four-segment column from it:
	//          undefined behaviour with a visible symptom. There is no behaviour to preserve, so
	//          nothing is declared here and the knob keeps whatever it was last showing -- the
	//          shell's own rule for an undeclared indicator (`Shell::flush`) and the only defined
	//          thing to do. Not annotated in `view.cpp` itself; see the sibling @warning above.
	return out;
}

bool ModKnobs::indicator_blinking(int32_t which_encoder) const {
	if (!guest_.blink_on(which_encoder)) {
		return false;
	}
	return (now_ms_ - guest_.blink_start_ms(which_encoder)) < deluge::ui::kKnobIndicatorBlinkMs;
}

ModIndicatorInfo ModKnobs::indicator_info(int32_t which_encoder) const {
	// The reads `View::setKnobIndicatorLevel` makes, from the records and the facts.
	ModIndicatorInfo out{};
	if (song_ == nullptr) {
		return out; // no song: nothing to read, so nothing is declared
	}
	const bind::RuntimeFacts& facts = menu::host_runtime_facts();
	const bind::KnobAssignment assignment = bind::knob_assignment(song_->project, *focus_, which_encoder, board_);
	if (const auto* comp = std::get_if<bind::CompArm>(&assignment.target)) {
		const effects::EffectStates* fx =
		    facts.chain_effects(project::ChainAddress{.kind = project::ChainAddress::Kind::SONG});
		out.non_existent_param = true;
		out.non_existent_knob_pos = fx != nullptr ? comp_knob_pos(*comp, *fx) : -64;
		return out;
	}
	const auto* address = std::get_if<ParamAddress>(&assignment.target);
	if (address == nullptr && std::holds_alternative<bind::MidiFocus>(focus_->owner)) {
		// A MIDI knob that sends nothing, or a MIDI track with no clip, shows at its midpoint (legacy's param 0).
		out.patch_cable_default = true;
		return out;
	}
	if (address == nullptr) {
		// A chain's knob with no param on this button shows at the bottom (`getKnobPosForNonExistentParam`).
		out.non_existent_param = true;
		out.non_existent_knob_pos = -64;
		return out;
	}
	const std::optional<int32_t> value = facts.param_value_at(*address, focus_->region.pos);
	// A MIDI clip's CC or expression lane it has not made yet shows as one at 0 (legacy makes it to show it); a cable
	// the sound does not hold yet shows at its midpoint.
	const bool lane = address->space != ParamSpace::ENGINE;
	if (!value && !lane) {
		out.patch_cable_default = true;
		return out;
	}
	const deluge::plugin::ParamInfo* info = info_of(*address, assignment.kind);
	out.has_auto_param = true;
	out.kind = assignment.kind;
	out.param_id = legacy_id(*address, assignment.kind);
	out.knob_pos = controls::knob_pos_of(*address, info, value.value_or(0));
	out.bipolar = controls::bipolar(*address, info);
	if (info != nullptr && info->file_name == "stutterRate") {
		out.quantized_stutter = owner_stutter_quantized();
	}
	return out;
}

void ModKnobs::declare_indicators(deluge::ui::LedFrame& frame) {
	// `View::setKnobIndicatorLevels`.
	// No root UI: an empty scene stack.
	if (stack_.depth() == 0) {
		return;
	}
	if (view_.in_automation_editor) {
		return; // the automation editor's own display owns the columns
	}
	if (std::holds_alternative<std::monostate>(focus_->owner) || !bound()) {
		// `indicator_leds::clearKnobIndicatorLevels()`.
		for (int32_t k = 0; k < board_.slots; k++) {
			(void)frame.indicator(static_cast<deluge::ui::IndicatorId>(k), deluge::ui::knob_indicator_levels(0, false));
		}
		return;
	}
	const bool stuttering = menu::host_runtime_facts().stutter_active();
	for (int32_t k = 0; k < board_.slots; k++) {
		if (indicator_blinking(k)) {
			// A blinking knob is driven by its blink's phase.
			(void)frame.indicator(
			    static_cast<deluge::ui::IndicatorId>(k),
			    deluge::ui::knob_indicator_blink(now_ms_ - guest_.blink_start_ms(k), guest_.blink_bipolar(k)));
			continue;
		}
		const IndicatorLevel level = indicator_level(indicator_info(k), stuttering);
		if (level.declared) {
			(void)frame.indicator(static_cast<deluge::ui::IndicatorId>(k),
			                      deluge::ui::knob_indicator_levels(level.level, level.bipolar));
		}
	}
}

void ModKnobs::leds(deluge::ui::LedFrame& frame) {
	// `View::setModLedStates`' final block, "Sort out actual mod LEDs". The
	// three LED groups above it (AFFECT_ENTIRE, CLIP_VIEW, SESSION_VIEW) are root-view questions
	// and are not this service's -- see the class doc.
	const bool its_the_song = !is_clip_context(*focus_); // `itsTheSong`
	const int32_t mod_knob_mode = mode();

	constexpr deluge::ui::LedState kOff{deluge::ui::LedState::Kind::Off, 0, 0};
	constexpr deluge::ui::LedState kOn{deluge::ui::LedState::Kind::On, 0, 0};
	// `indicator_leds::blinkLed(led)`'s defaults (`view.cpp`): `numBlinks` 255 (i.e. forever) on
	// the normal blink every `blinkingType` 0 LED shares, MenuScene's BACK and LEARN among them.
	constexpr deluge::ui::LedState kBlink = deluge::ui::LedState::shared(deluge::ui::BlinkRate::Normal);

	for (int32_t i = 0; i < board_.sections; i++) {
		const bool on = (i == mod_knob_mode);
		deluge::ui::LedState state = kOff;
		if (its_the_song && on && mod_knob_mode == 0 && focus_->vu_meter) {
			state = kBlink; // the volume mod LED blinks while the VU meter is up
		}
		else if (view_.in_automation_editor) {
			state = kOff; // every mod LED off in the automation editor
		}
		else {
			state = on ? kOn : kOff;
		}
		(void)frame.set(static_cast<deluge::ui::LedId>(static_cast<uint8_t>(ButtonId::Mod0) + i), state);
	}

	declare_indicators(frame);
}

void ModKnobs::send_midi_follow_feedback(Kind kind, int32_t param_id, int32_t knob_pos, bool have_param,
                                         bool is_automation) {
	// `View::sendMidiFollowFeedback`.
	if (!midiFollow.isFeedbackEnabled()) {
		return;
	}
	if (!is_clip_context(*focus_)) { // don't send feedback for song params
		return;
	}
	if (have_param) {
		// In a global effectable's context only its own params are followed, elsewhere a sound's.
		const deluge::bridge::OwnerPlugin owner = midiFollow.isGlobalEffectableContext()
		                                              ? deluge::bridge::OwnerPlugin::GLOBAL_EFFECTABLE
		                                              : deluge::bridge::OwnerPlugin::INSTRUMENT;
		const std::optional<deluge::project::ParamTarget> target =
		    kind == Kind::NONE ? std::nullopt : deluge::bridge::targetOf(owner, kind, param_id);
		if (const std::optional<uint8_t> cc_number = target ? midiFollow.ccOf(owner, *target) : std::nullopt) {
			midiFollow.sendCCForMidiFollowFeedback(*cc_number, knob_pos);
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
		// `TimerName::DISPLAY_AUTOMATION`'s callback (`UITimerManager::routine`) is
		// `automationView.displayAutomation()` / `view.displayAutomation()`, both view-side and not
		// ported. Disarming is all this can do; `automation_display_pending()` is the fact
		// `leds()` reads, exactly as legacy reads `isTimerSet(DISPLAY_AUTOMATION)`.
		automation_display_ms_.reset();
	}

	if (automation_feedback_ms_.has_value() && static_cast<int32_t>(now_ms - *automation_feedback_ms_) >= 0) {
		automation_feedback_ms_.reset();
		// `TimerName::SEND_MIDI_FEEDBACK_FOR_AUTOMATION`'s callback (`UITimerManager::routine`):
		// only while playback is active, and only when feedback automation is not disabled. The
		// LOW/MEDIUM/HIGH send-rate re-arm below those lines is `ui_timer_manager`'s own and is not
		// ported here -- it is not part of any `View` function this service ports.
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
	// the ordinary level comes back (`indicator_leds::blinkKnobIndicatorLevelTimeout`'s 20 ms re-arm).
	for (int32_t k = 0; k < board_.slots; k++) {
		if (indicator_blinking(k)) {
			const uint32_t elapsed = now_ms_ - guest_.blink_start_ms(k);
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
	return guest_.blink_on(which_encoder);
}

bool ModKnobs::blink_indicator_bipolar(int32_t which_encoder) const {
	return guest_.blink_bipolar(which_encoder);
}

} // namespace deluge::scenes::services
