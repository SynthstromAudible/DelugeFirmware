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

#include "ui/scenes/services/mod_knobs.h"

#include "plugin/types/display_mapping.h"
#include "project/device_settings.h"
#include "project/song.h"
#include "ui/controls/board.h"
#include "ui/controls/follow.h"
#include "ui/controls/knob_math.h"
#include "ui/controls/pages.h"
#include "ui/controls/section.h"

#include "ui/scenes/bind/device_reader.h"
#include "ui/scenes/bind/knob_assignment.h"
#include "ui/scenes/bind/page_sources.h"
#include "ui/scenes/bind/param_facts.h"
#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/services/knob_push.h"

#include "ui/shell/input_lowering.h"

#include "gui/l10n/strings.h"
#include "host/chains/params.h"
#include "instruments/classic/cables.h"
#include "instruments/classic/params.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::ui::scenes::services {

using deluge::project::ParamAddress;
using deluge::project::ParamSpace;
using deluge::ui::scenes::bind::port_of;
using deluge::ui::scenes::bind::Ports;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::UiEvent;
using deluge::ui::widgets::core::EncoderRole;

namespace {

/// @brief Which `EncoderRole` mod encoder @p which_encoder is: 0 is `mod0`, the lower (`hid/encoders.h`'s
/// `mod0`/`mod1`).
EncoderRole role_of(int32_t which_encoder) {
	return which_encoder == 0 ? EncoderRole::GoldLower : EncoderRole::GoldUpper;
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
	return model_ != nullptr && ports_ != nullptr;
}

bool ModKnobs::feedback_automation_on() const {
	return bind::read_device<project::FeedbackAutomation>(port_of(ports_, &Ports::device),
	                                                      project::device_path::kFollowFeedbackAutomation)
	       != project::FeedbackAutomation::DISABLED;
}

const effects::EffectStates* ModKnobs::owner_effects() const {
	const bind::TrackFacts* const tracks = port_of(ports_, &Ports::tracks);
	const bind::SoundFacts* const sound = port_of(ports_, &Ports::sound);
	return tracks != nullptr && sound != nullptr ? services::owner_effects(*focus_, *tracks, *sound) : nullptr;
}

PushInputs ModKnobs::push_inputs(bool mode_held) const {
	return {.button_held = mode_held, .clip_context = is_clip_context(*focus_), .board = board_, .ports = ports_};
}

bool ModKnobs::owner_stutter_quantized() const {
	const effects::EffectStates* fx = owner_effects();
	const bind::TrackFacts* const tracks = port_of(ports_, &Ports::tracks);
	const effects::EffectStates* song =
	    tracks != nullptr ? tracks->chain_effects(project::ChainAddress{.kind = project::ChainAddress::Kind::SONG})
	                      : nullptr;
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
		send_midi_follow_feedback(std::nullopt, false);
	}
}

void ModKnobs::set_mod_region(uint32_t pos, uint32_t length, int32_t note_row_id) {
	focus_->region = {.pos = pos, .length = length, .note_row_id = note_row_id}; // `View::setModRegion`
	if (focus_changed_) {
		focus_changed_();
	}
	// Holding a note on a stopped clip grabs its params' values from there.
	const bind::Transport* const transport = port_of(ports_, &Ports::transport);
	const bool clocks = transport != nullptr && transport->clocks_active();
	if (length != 0 && is_clip_context(*focus_) && !clocks && bound()) {
		if (ui::controls::Sink* const sink = port_of(ports_, &Ports::controls_sink)) {
			(void)sink->grab_region(*focus_, pos);
		}
	}
	// Unless midi follow feedback automation is disabled.
	if (feedback_automation_on()) {
		send_midi_follow_feedback(std::nullopt, true);
	}
}

int32_t ModKnobs::mode() const {
	// `View::getModKnobMode` (`origin/main:src/deluge/gui/views/view.cpp`).
	if (model_ == nullptr || bind::has_no_knobs(*focus_)) {
		return -1;
	}
	return bind::knob_mode(*model_, *focus_, board_);
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
			if (const std::optional<ui::controls::Logical> section = ui::controls::map_input(
			        board_, {ui::controls::Physical::Kind::SECTION_BUTTON, static_cast<uint8_t>(which)})) {
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
			if (const std::optional<ui::controls::Logical> slot =
			        ui::controls::map_input(board_, {ui::controls::Physical::Kind::SLOT_PRESS, physical})) {
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
	if (const std::optional<ui::controls::Logical> slot =
	        ui::controls::map_input(board_, {ui::controls::Physical::Kind::SLOT_ENCODER, physical})) {
		turn(slot->index, offset, ctx.now_ms, guest_);
	}
	Result r = guest_.take_result();
	r.handled = true;
	return r;
}

void ModKnobs::turn(int32_t knob, int32_t offset, uint32_t now_ms, KnobPresentation& out) {
	// `View::modEncoderAction` (`origin/main:src/deluge/gui/views/view.cpp`).
	if (!bound() || std::holds_alternative<std::monostate>(focus_->owner)) {
		return;
	}
	turn_knob(knob, offset, now_ms, out);
	// Every turn on a clip marks its instrument edited, whatever it wrote.
	if (ui::controls::Sink* const sink = port_of(ports_, &Ports::controls_sink);
	    sink != nullptr && is_clip_context(*focus_)) {
		(void)sink->instrument_edited(*focus_);
	}
}

void ModKnobs::turn_knob(int32_t which_encoder, int32_t offset, uint32_t now_ms, KnobPresentation& out) {
	const ui::controls::Facts* const facts = port_of(ports_, &Ports::controls);
	ui::controls::Sink* const sink = port_of(ports_, &Ports::controls_sink);
	const bind::KnobAssignment assignment =
	    playing_assignment(*model_, *focus_, which_encoder, board_, port_of(ports_, &Ports::sound));

	if (const auto* comp = std::get_if<bind::CompArm>(&assignment.target)) {
		// The song's compressor knobs edit settings, not params (`modEncoderActionForNonExistentParam`).
		const project::ChainAddress song_chain{.kind = project::ChainAddress::Kind::SONG};
		const bind::TrackFacts* const tracks = port_of(ports_, &Ports::tracks);
		const effects::EffectStates* fx = tracks != nullptr ? tracks->chain_effects(song_chain) : nullptr;
		if (fx != nullptr) {
			const CompTurn turn = comp_turn(*comp, *fx, offset);
			if (sink != nullptr && sink->write_effect(song_chain, turn.path, turn.value)) {
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

	// `View::modEncoderAction_existentParam` (`origin/main:src/deluge/gui/views/view.cpp`). A knob on a param its owner
	// does not hold yet reads 0: the write makes it.
	const deluge::project::PluginParam param = bind::plugin_param_of(assignment.plugin, *address);
	const deluge::plugin::ParamInfo* info = bind::info_of(param);
	const bool at_region = focus_->region.pos != 0xFFFFFFFF;
	const std::optional<int32_t> value =
	    facts != nullptr ? facts->param_value_at(*address, focus_->region.pos) : std::nullopt;
	ModTurn turn;
	turn.param = param;
	turn.old_knob_pos = ui::controls::knob_pos_of(*address, info, value.value_or(0));
	turn.new_knob_pos = ui::controls::turn(ui::controls::knob_kind(*address), turn.old_knob_pos, offset);

	// The popup, unless the presentation shows the value its own way.
	if (!out.suppress_value_popup(turn)) {
		show_popup(turn, now_ms, out);
	}
	if (turn.new_knob_pos == turn.old_knob_pos) {
		return;
	}

	const auto* sound = std::get_if<bind::SoundFocus>(&focus_->owner);
	const auto allows_tails = [&] {
		return facts != nullptr && sound != nullptr && sound->clip
		       && facts->allows_note_tails(sound->slot, sound->clip);
	};
	const bool tails_before = allows_tails();
	const int32_t new_value = ui::controls::value_of_knob(*address, info, turn.new_knob_pos);
	const int32_t pos = at_region ? static_cast<int32_t>(focus_->region.pos) : 0;
	if (sink == nullptr
	    || !sink->write_param(*address, new_value, pos, static_cast<int32_t>(focus_->region.length),
	                          ui::controls::Resolution::KNOB)) {
		return; // refused: nothing changed, so nothing is fed back
	}
	out.value_written();
	turn.value_changed = true;
	turn.note_tails_changed = sound != nullptr && sound->clip && tails_before != allows_tails();

	turn.should_indicate_middle_value = ui::controls::indicates_middle(*address, info, owner_stutter_quantized(),
	                                                                   facts != nullptr && facts->stutter_active());

	send_midi_follow_feedback(TurnedParam{turn.param, *address, turn.new_knob_pos}, false);
	if (turn.note_tails_changed) {
		out.note_tails_changed();
	}
	potentially_make_it_harder_to_turn_knob(which_encoder, turn, out);
}

void ModKnobs::show_popup(const ModTurn& turn, uint32_t now_ms, KnobPresentation& out) {
	// The two patch sources, then the popup itself.
	PatchSource source1 = PatchSource::NONE;
	PatchSource source2 = PatchSource::NONE;
	if (bind::is_cable(turn.param)) {
		const deluge::synth::CableTarget descriptor{deluge::instruments::classic::cable_word_of(turn.param.target.id)};
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
	if (const bind::SoundFacts* const sound = port_of(ports_, &Ports::sound)) {
		context.envelopes = services::owner_envelope_modes(*focus_, *sound);
	}
	context.stutter_quantized = owner_stutter_quantized();
	if (const auto* midi = std::get_if<bind::MidiFocus>(&focus_->owner); midi != nullptr && midi->clip) {
		context.midi_clip = true;
		const project::DocOutput* output = model_->output_structure.find(midi->track.output_id);
		if (output != nullptr && turn.param.target.space == ParamSpace::MIDI_CC) {
			if (const auto label = output->cc_labels.find(static_cast<uint8_t>(turn.param.target.id.value));
			    label != output->cc_labels.end()) {
				context.cc_label = label->second;
			}
		}
	}
	const ui::controls::Facts* const facts = port_of(ports_, &Ports::controls);
	context.stuttering = facts != nullptr && facts->stutter_active();
	out.show_value_popup(turn, source1, source2, context, now_ms);
}

void ModKnobs::potentially_make_it_harder_to_turn_knob(int32_t which_encoder, const ModTurn& turn,
                                                       KnobPresentation& out) {
	// `View::potentiallyMakeItHarderToTurnKnob`.
	if (which_encoder < 0 || which_encoder >= board_.slots) {
		return;
	}
	int32_t knob_pos = turn.new_knob_pos;
	if (bind::is_cable(turn.param)) {
		// A patch cable is -128..+128; fold it into the indicator's range so "the
		// middle" is the same number the indicator blinks at.
		knob_pos = ui::controls::fold_cable(knob_pos + plugin::kMaxKnobPosition / 2) - plugin::kMaxKnobPosition / 2;
	}

	if (knob_pos == 0 && turn.should_indicate_middle_value) {
		// `indicator_leds::blinkKnobIndicator(whichModEncoder, isBipolar)`. `main`'s
		// `pretendModKnobsUntouchedForAWhile` here writes `timeModEncoderLastTurned`, which nothing reads, so it has no
		// port.
		out.blink_indicator(which_encoder, bind::is_bipolar(turn.param));
	}
	else {
		out.stop_blink_indicator(which_encoder); // `stopBlinkingKnobIndicator`
	}
}

// -- The buttons -------------------------------------------------------------------------------

void ModKnobs::section_button(int32_t which_button, bool on, KnobPresentation& out) {
	// `View::modButtonAction` (`origin/main:src/deluge/gui/views/view.cpp`).

	// The automation editor ignores a mod button, except mod button 0 on the arranger's, which keeps
	// the VU meter toggleable there.
	if (view_.in_automation_editor && !(view_.automation_on_arranger && which_button == 0)) {
		return;
	}
	if (bind::has_no_knobs(*focus_) || !bound()) {
		return;
	}
	ui::controls::Sink* const sink = port_of(ports_, &Ports::controls_sink);
	const ui::controls::Facts* const facts = port_of(ports_, &Ports::controls);

	// A MIDI track choosing its knob's CC: a mod button's press changes nothing, and its release ends the choice.
	if (selecting_cc_) {
		if (on) {
			return;
		}
		if (sink != nullptr) {
			(void)sink->midi_cc_select(*focus_, selecting_knob_, false);
		}
		selecting_cc_ = false;
		out.cancel_popup();
		return;
	}

	// The button's rules (`View::modButtonAction` in `origin/main:src/deluge/gui/views/view.cpp`,
	// main's `ModControllable::modButtonAction`): the VU meter's toggle and the mode change come before the stutter's
	// end, which the popup reads, and a press the rules ignore does nothing.
	const ui::controls::SectionResult section = ui::controls::resolve_section(
	    *focus_, static_cast<uint8_t>(mode()), which_button, on,
	    {.root_is_performance = view_.root_is_performance,
	     .mod_button_ui_mode_in_range = view_.mod_button_ui_mode_in_range,
	     .root_is_clip_minder = view_.root_is_clip_minder,
	     .keeps_stutter = ui::controls::section_keeps_stutter(*model_, *focus_, static_cast<uint32_t>(which_button),
	                                                          facts != nullptr ? bind::app_page_sources(*facts, board_)
	                                                                           : bind::app_turn_sources(board_))});
	if (on && !section.taken) {
		return;
	}
	if (section.toggle_vu_meter) {
		focus_->vu_meter = !focus_->vu_meter;
	}
	if (on && section.taken && !view_.root_is_clip_minder) {
		out.vu_sidebar_changed(); // the VU meter is offered only outside a clip minder
	}
	if (section.change_mode && sink != nullptr) {
		(void)sink->knob_mode(*ui::controls::knob_mode_owner(*focus_), static_cast<uint8_t>(which_button));
	}
	if (section.end_stutter && sink != nullptr) {
		(void)sink->stutter(*focus_, false);
	}
	if (!section.taken) {
		out.cancel_popup();
		return;
	}
	if (const std::string text = mod_button_popup(*model_, *focus_, which_button, push_inputs(false)); !text.empty()) {
		out.popup_text(text);
	}
	out.mode_changed();
}

void ModKnobs::push(int32_t which_encoder, bool on, const PushContext& ctx, KnobPresentation& out) {
	// `View::modEncoderButtonAction` (`origin/main:src/deluge/gui/views/view.cpp`). Legacy's tail always reaches
	// `dealtWith`. LEARN refuses a press only: a release still ends what its press began (a MIDI CC choice).
	if ((ctx.learn && on) || bind::has_no_knobs(*focus_) || !bound()) {
		return;
	}
	ui::controls::Sink* const sink = port_of(ports_, &Ports::controls_sink);

	// `View::modEncoderButtonAction_deleteAutomation` (`origin/main:src/deluge/gui/views/view.cpp`).
	if (ctx.shift && on) {
		const std::optional<ParamAddress> address = knob_address(which_encoder);
		const ui::controls::Facts* const facts = port_of(ports_, &Ports::controls);
		if (address && facts != nullptr && facts->param_value(*address).has_value() && sink != nullptr
		    && sink->delete_knob_automation(*address)) {
			out.popup(deluge::l10n::String::STRING_FOR_AUTOMATION_DELETED);
		}
		return;
	}

	// `View::modEncoderButtonAction_changeModControllable` (`origin/main:src/deluge/gui/views/view.cpp`): the push's
	// table, sent to the sink.
	const KnobPush push = knob_push(*model_, *focus_, which_encoder, on, push_inputs(ctx.mode_held));
	if (push.midi_cc_select) {
		// The instrument takes it from a clip minder with no UI mode active; a release ends it.
		selecting_cc_ = sink != nullptr && sink->midi_cc_select(*focus_, which_encoder, on) && on;
		selecting_knob_ = which_encoder;
	}
	// A push that edited the instrument marks it, on a clip.
	bind::WriteSink* const writes = port_of(ports_, &Ports::writes);
	const bool applied = writes != nullptr && sink != nullptr && apply_push(*writes, *sink, *focus_, push);
	if (applied && push.edited && is_clip_context(*focus_)) {
		(void)sink->instrument_edited(*focus_);
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
	if (model_ == nullptr || std::holds_alternative<std::monostate>(focus_->owner)) {
		return std::nullopt;
	}
	const bind::KnobAssignment assignment =
	    playing_assignment(*model_, *focus_, which_encoder, board_, port_of(ports_, &Ports::sound));
	if (const auto* address = std::get_if<ParamAddress>(&assignment.target)) {
		return *address;
	}
	return std::nullopt;
}

// -- The LEDs and the indicators -----------------------------------------------------------------

ModKnobs::IndicatorLevel ModKnobs::indicator_level(const ModIndicatorInfo& info, bool stuttering) {
	// `View::setKnobIndicatorLevel`'s arithmetic (`origin/main:src/deluge/gui/views/view.cpp`), over reads
	// the reads `indicator_info` made.
	IndicatorLevel out{};

	if (info.has_auto_param) {
		int32_t knob_pos = info.knob_pos;
		// The same "never drag a position that is already below the limit up"
		// clamp `calculateKnobPosForModEncoderTurn` uses, with the same two limits.
		const int32_t lower_limit = bind::is_cable(info.param) ? std::min(-192, knob_pos) : std::min(-64, knob_pos);
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
		knob_pos += plugin::kMaxKnobPosition / 2;

		if (bind::is_cable(info.param)) {
			knob_pos = ui::controls::fold_cable(knob_pos);
		}
		out.declared = true;
		// **The `uint8_t` is legacy's, not a narrowing this port introduces**, and the argument is made here at the
		// code rather than only on `IndicatorLevel::level`. `View::setKnobIndicatorLevel`
		// (`origin/main:src/deluge/gui/views/view.cpp`) carries `int32_t knobPos` right to its last line and hands it
		// to `indicator_leds::setKnobIndicatorLevel(uint8_t whichKnob, uint8_t level, bool)`, so the value is truncated
		// by an implicit conversion at exactly this point. The `>> 5` and `& 31` of
		// `indicator_leds::actuallySetKnobIndicatorLevel` run on that already-truncated parameter, not on the `int32_t`
		// -- there is no wider legacy arithmetic to be faithful to. An explicit cast where legacy has an implicit one
		// is the same value and a visible one.
		out.level = static_cast<uint8_t>(knob_pos);
		out.bipolar = info.bipolar;
		return out;
	}

	if (info.non_existent_param) {
		out.declared = true;
		// Same implicit-narrowing point as above.
		out.level = static_cast<uint8_t>(info.non_existent_knob_pos + plugin::kMaxKnobPosition / 2);
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
	// @warning **Legacy defect (the fifth entry), recorded in `src/ui/scenes/README.md`'s "Defects this port found in
	//          shipping firmware".** `View::setKnobIndicatorLevel` (`origin/main:src/deluge/gui/views/view.cpp`)
	//          declares `int32_t knobPos;` with no initialiser (`View::setKnobIndicatorLevel`) and assigns it on three
	//          of its four paths -- an `autoParam`'s, a non-existent param's and a patch cable's default -- leaving it
	//          **uninitialised** on this one. It is then passed to `indicator_leds::setKnobIndicatorLevel`, which
	//          truncates it to `uint8_t` and lights the gold knob's four-segment column from it: undefined behaviour
	//          with a visible symptom. There is no behaviour to preserve, so nothing is declared here and the knob
	//          keeps whatever it was last showing -- the shell's own rule for an undeclared indicator (`Shell::flush`)
	//          and the only defined thing to do. Not annotated in `gui/views/view.cpp` itself; see the sibling @warning
	//          above.
	return out;
}

bool ModKnobs::indicator_blinking(int32_t which_encoder) const {
	if (!guest_.blink_on(which_encoder)) {
		return false;
	}
	return (now_ms_ - guest_.blink_start_ms(which_encoder)) < deluge::ui::widgets::core::kKnobIndicatorBlinkMs;
}

ModIndicatorInfo ModKnobs::indicator_info(int32_t which_encoder) const {
	// The reads `View::setKnobIndicatorLevel` (`origin/main:src/deluge/gui/views/view.cpp`) makes, from the records and
	// the facts.
	ModIndicatorInfo out{};
	if (model_ == nullptr) {
		return out; // no song: nothing to read, so nothing is declared
	}
	const ui::controls::Facts* const facts = port_of(ports_, &Ports::controls);
	const bind::TrackFacts* const tracks = port_of(ports_, &Ports::tracks);
	const bind::KnobAssignment assignment =
	    playing_assignment(*model_, *focus_, which_encoder, board_, port_of(ports_, &Ports::sound));
	if (const auto* comp = std::get_if<bind::CompArm>(&assignment.target)) {
		const effects::EffectStates* fx =
		    tracks != nullptr ? tracks->chain_effects(project::ChainAddress{.kind = project::ChainAddress::Kind::SONG})
		                      : nullptr;
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
	const std::optional<int32_t> value =
	    facts != nullptr ? facts->param_value_at(*address, focus_->region.pos) : std::nullopt;
	// A MIDI clip's CC or expression lane it has not made yet shows as one at 0 (legacy makes it to show it); a cable
	// the sound does not hold yet shows at its midpoint.
	const bool lane = address->space != ParamSpace::ENGINE;
	if (!value && !lane) {
		out.patch_cable_default = true;
		return out;
	}
	out.param = bind::plugin_param_of(assignment.plugin, *address);
	const deluge::plugin::ParamInfo* info = bind::info_of(out.param);
	out.has_auto_param = true;
	out.knob_pos = ui::controls::knob_pos_of(*address, info, value.value_or(0));
	out.bipolar = ui::controls::bipolar(*address, info);
	if (info != nullptr && info->file_name == "stutterRate") {
		out.quantized_stutter = owner_stutter_quantized();
	}
	return out;
}

void ModKnobs::declare_indicators(deluge::ui::widgets::core::LedFrame& frame) {
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
			(void)frame.indicator(static_cast<deluge::ui::widgets::core::IndicatorId>(k),
			                      deluge::ui::widgets::core::knob_indicator_levels(0, false));
		}
		return;
	}
	const ui::controls::Facts* const facts = port_of(ports_, &Ports::controls);
	const bool stuttering = facts != nullptr && facts->stutter_active();
	for (int32_t k = 0; k < board_.slots; k++) {
		if (indicator_blinking(k)) {
			// A blinking knob is driven by its blink's phase.
			(void)frame.indicator(static_cast<deluge::ui::widgets::core::IndicatorId>(k),
			                      deluge::ui::widgets::core::knob_indicator_blink(now_ms_ - guest_.blink_start_ms(k),
			                                                                      guest_.blink_bipolar(k)));
			continue;
		}
		const IndicatorLevel level = indicator_level(indicator_info(k), stuttering);
		if (level.declared) {
			(void)frame.indicator(static_cast<deluge::ui::widgets::core::IndicatorId>(k),
			                      deluge::ui::widgets::core::knob_indicator_levels(level.level, level.bipolar));
		}
	}
}

void ModKnobs::leds(deluge::ui::widgets::core::LedFrame& frame) {
	// `View::setModLedStates`' final block, "Sort out actual mod LEDs". The
	// three LED groups above it (AFFECT_ENTIRE, CLIP_VIEW, SESSION_VIEW) are root-view questions
	// and are not this service's -- see the class doc.
	const bool its_the_song = !is_clip_context(*focus_); // `itsTheSong`
	const int32_t mod_knob_mode = mode();

	constexpr deluge::ui::widgets::core::LedState kOff{deluge::ui::widgets::core::LedState::Kind::Off, 0, 0};
	constexpr deluge::ui::widgets::core::LedState kOn{deluge::ui::widgets::core::LedState::Kind::On, 0, 0};
	// `indicator_leds::blinkLed(led)`'s defaults (`hid/led/indicator_leds.h`): `numBlinks` 255 (i.e. forever) on
	// the normal blink every `blinkingType` 0 LED shares, MenuScene's BACK and LEARN among them.
	constexpr deluge::ui::widgets::core::LedState kBlink =
	    deluge::ui::widgets::core::LedState::shared(deluge::ui::widgets::core::BlinkRate::Normal);

	for (int32_t i = 0; i < board_.sections; i++) {
		const bool on = (i == mod_knob_mode);
		deluge::ui::widgets::core::LedState state = kOff;
		if (its_the_song && on && mod_knob_mode == 0 && focus_->vu_meter) {
			state = kBlink; // the volume mod LED blinks while the VU meter is up
		}
		else if (view_.in_automation_editor) {
			state = kOff; // every mod LED off in the automation editor
		}
		else {
			state = on ? kOn : kOff;
		}
		(void)frame.set(static_cast<deluge::ui::widgets::core::LedId>(static_cast<uint8_t>(ButtonId::Mod0) + i), state);
	}

	declare_indicators(frame);
}

void ModKnobs::send_midi_follow_feedback(const std::optional<TurnedParam>& param, bool is_automation) {
	// `View::sendMidiFollowFeedback`.
	bind::MidiFeedback* const feedback = port_of(ports_, &Ports::midi_feedback);
	if (feedback == nullptr || !feedback->enabled()) {
		return;
	}
	if (!is_clip_context(*focus_)) { // don't send feedback for song params
		return;
	}
	if (param) {
		// In a global effectable's context only its own params are followed, elsewhere a sound's.
		const deluge::project::OwnerPlugin owner = feedback->global_effectable_context()
		                                               ? deluge::project::OwnerPlugin::GLOBAL_EFFECTABLE
		                                               : deluge::project::OwnerPlugin::INSTRUMENT;
		const std::optional<deluge::project::ParamTarget> target =
		    deluge::ui::controls::follow_target(owner, param->param, deluge::instruments::classic::has_engine_param);
		if (const std::optional<uint8_t> cc_number = target ? feedback->cc_of(owner, *target) : std::nullopt) {
			feedback->send(*cc_number, param->knob_pos);
		}
	}
	else {
		feedback->send_all(is_automation);
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
		const bind::Transport* const transport = port_of(ports_, &Ports::transport);
		if (transport != nullptr && transport->clocks_active() && feedback_automation_on()) {
			send_midi_follow_feedback(std::nullopt, true);
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
			const uint32_t to_next_half = deluge::ui::widgets::core::kKnobIndicatorBlinkHalfMs
			                              - (elapsed % deluge::ui::widgets::core::kKnobIndicatorBlinkHalfMs);
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

} // namespace deluge::ui::scenes::services
