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

#include "scenes/bind/knob_assignment.h"
#include "scenes/bind/knob_math.h"
#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/registry.h"
#include "scenes/services/knob_push.h"

#include "shell/input_lowering.h"

#include "engines/deluge/cables.h"
#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"
#include "gui/l10n/strings.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "model/project_bridge/param_ids.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "notify/notice_queue.h"
#include "playback/playback_handler.h"

#include <algorithm>
#include <cstdint>

namespace deluge::scenes::services {

using deluge::modulation::params::Kind;
using deluge::project::ParamAddress;
using deluge::project::ParamSpace;
using deluge::ui::EncoderRole;
using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::UiEvent;

namespace {

/// @brief Which `EncoderRole` mod encoder @p which_encoder is: 0 is `mod0`, the lower (`hid/encoders.h:80-81`).
EncoderRole role_of(int32_t which_encoder) {
	return which_encoder == 0 ? EncoderRole::GoldLower : EncoderRole::GoldUpper;
}

/// @brief The descriptor of the param @p assignment names, from its owner's engine table; null for a cable, a MIDI
///        CC or an expression dimension.
const deluge::engine::ParamInfo* info_of(const ParamAddress& address, Kind kind) {
	if (address.space != ParamSpace::ENGINE || deluge::instrument::is_cable_depth(address.id)) {
		return nullptr;
	}
	return kind == Kind::UNPATCHED_GLOBAL ? deluge::engine::find_param(deluge::global_effectable::kParams, address.id)
	                                      : deluge::engine::find_param(deluge::instrument::kParams, address.id);
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

/// @brief Whose gold-knob mode @p focus's knobs follow: the song's, or a track's (a kit row's kit's).
std::optional<deluge::project::KnobModeOwner> mode_owner(const bind::KnobFocus& focus) {
	if (std::holds_alternative<bind::SongFocus>(focus.owner)) {
		return deluge::project::SongAddress{};
	}
	if (const auto* sound = std::get_if<bind::SoundFocus>(&focus.owner)) {
		if (sound->kit) {
			return deluge::project::TrackAddress{*sound->kit};
		}
		return deluge::project::TrackAddress{sound->slot.id};
	}
	if (const auto* chain = std::get_if<bind::ChainFocus>(&focus.owner)) {
		return deluge::project::TrackAddress{chain->chain.id};
	}
	if (const auto* midi = std::get_if<bind::MidiFocus>(&focus.owner)) {
		return midi->track;
	}
	return std::nullopt;
}

/// @brief Show @p text as a popup that stays until a mod button's release cancels it (`display->popupText`).
void popup_text(const std::string& text) {
	deluge::notify::Notice n;
	n.kind = deluge::notify::Notice::Kind::Text;
	n.severity = deluge::notify::Notice::Severity::Warn; // stays until something removes it
	const size_t length = std::min(text.size(), sizeof(n.text) - 1);
	std::copy_n(text.data(), length, n.text);
	n.text[length] = '\0';
	(void)deluge::notify::notices().push(n);
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
	return false;
}

// -- What the knobs point at -------------------------------------------------------------------

bool ModKnobs::bound() const {
	return song_ != nullptr && menu::host_binds(*song_);
}

const effects::EffectStates* ModKnobs::owner_effects() const {
	const std::optional<EffectOwner> owner = effect_owner(*focus_);
	if (!owner) {
		return nullptr;
	}
	const bind::RuntimeFacts& facts = menu::host_runtime_facts();
	if (const auto* slot = std::get_if<project::SlotAddress>(&*owner)) {
		const instrument::SoundState* state = facts.sound_state(*slot);
		return state != nullptr ? &state->effects : nullptr;
	}
	return facts.chain_effects(std::get<project::ChainAddress>(*owner));
}

bool ModKnobs::owner_stutter_quantized() const {
	const effects::EffectStates* fx = owner_effects();
	const effects::EffectStates* song =
	    menu::host_runtime_facts().chain_effects(project::ChainAddress{.kind = project::ChainAddress::Kind::SONG});
	return fx != nullptr && song != nullptr && stutter_quantized(*fx, *song);
}

void ModKnobs::set_focus(const bind::KnobFocus& focus, bool send_midi_feedback) {
	pretend_knobs_untouched(); // `:1871`, `:1903`
	// The mod region, the compressor view and the VU meter are the knobs' own view state, which a new focus keeps.
	focus_->owner = focus.owner;
	if (focus_changed_) {
		focus_changed_();
	}
	selecting_cc_ = false;
	if (send_midi_feedback) {
		send_midi_follow_feedback(Kind::NONE, 0, kNoSelection, false, false); // `:1896` / `:1920`
	}
}

void ModKnobs::set_mod_region(uint32_t pos, uint32_t length, int32_t note_row_id) {
	focus_->region = {.pos = pos, .length = length, .note_row_id = note_row_id}; // `:1925-1926`
	if (focus_changed_) {
		focus_changed_();
	}
	pretend_knobs_untouched(); // `:1929`
	// `:1931-1935` -- holding a note on a stopped clip grabs its params' values from there.
	if (length != 0 && is_clip_context(*focus_) && !playbackHandler.isEitherClockActive() && bound()) {
		(void)menu::host_write_sink().grab_region(*focus_, pos);
	}
	// `:1946-1947` -- unless midi follow feedback automation is disabled.
	if (midiEngine.midiFollowFeedbackAutomation != MIDIFollowFeedbackAutomationMode::DISABLED) {
		send_midi_follow_feedback(Kind::NONE, 0, kNoSelection, false, true);
	}
}

int32_t ModKnobs::mode() const {
	// `View::getModKnobMode`.
	if (song_ == nullptr || std::holds_alternative<std::monostate>(focus_->owner)) {
		return -1;
	}
	return bind::knob_mode(song_->project, *focus_);
}

void ModKnobs::pretend_knobs_untouched() {
	// `View::pretendModKnobsUntouchedForAWhile`, both knobs at once.
	lowering_.pretend_knob_untouched(EncoderRole::GoldLower, now_ms_);
	lowering_.pretend_knob_untouched(EncoderRole::GoldUpper, now_ms_);
}

void ModKnobs::notify_param_automation_occurred(const ParamAddress& owner, bool update_mod_levels) {
	// `View::notifyParamAutomationOccurred`, for the knobs' owner's params.
	const std::optional<ParamAddress> ours = bind::knob_owner(*focus_);
	if (!ours || !(ours->owner == owner.owner) || ours->clip_id != owner.clip_id) {
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

	// A mod button: `Buttons::buttonAction`'s mod-button loop ends in `goto dealtWith`, so it is always claimed.
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

	// A gold knob's push: `buttonAction`'s tail (`hid/buttons.cpp:225-231`).
	if (event.kind == UiEvent::Kind::EncoderButton) {
		if (event.button == ButtonId::ModEnc0 || event.button == ButtonId::ModEnc1) {
			const int32_t current = mode();
			const bool mode_held =
			    current >= 0 && ctx.held.has(static_cast<ButtonId>(static_cast<uint8_t>(ButtonId::Mod0) + current));
			return mod_encoder_button_action(event.button == ButtonId::ModEnc0 ? 0 : 1, event.on, ctx.shift,
			                                 ctx.held.has(ButtonId::Learn), mode_held);
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

	// `hid/encoder_input.cpp:192`: `offset * encoder.calcNextKnobSpeed(offset)`, narrowed back to `int8_t` as legacy
	// narrows it (truncation toward zero).
	// A host's lowered turn is accelerated already.
	const double speed = event.lowered ? 1.0 : lowering_.calc_next_knob_speed(event.encoder, event.delta, ctx.now_ms);
	const int32_t offset = static_cast<int8_t>(static_cast<double>(event.delta) * speed);
	return mod_encoder_action(which_encoder, offset, ctx.now_ms);
}

Result ModKnobs::mod_encoder_action(int32_t which_encoder, int32_t offset, uint32_t now_ms) {
	// `View::modEncoderAction`. Legacy consumes a gold-knob turn unconditionally.
	if (!bound() || std::holds_alternative<std::monostate>(focus_->owner)) {
		Result result;
		result.handled = true;
		return result;
	}
	Result result = turn_knob(which_encoder, offset, now_ms);
	// `:840` -- every turn on a clip marks its instrument edited, whatever it wrote.
	if (is_clip_context(*focus_)) {
		(void)menu::host_write_sink().instrument_edited(*focus_);
	}
	return result;
}

Result ModKnobs::turn_knob(int32_t which_encoder, int32_t offset, uint32_t now_ms) {
	Result result;
	result.handled = true;
	const bind::RuntimeFacts& facts = menu::host_runtime_facts();
	bind::WriteSink& sink = menu::host_write_sink();
	const bind::KnobAssignment assignment = bind::knob_assignment(song_->project, *focus_, which_encoder);

	if (const auto* comp = std::get_if<bind::CompArm>(&assignment.target)) {
		// The song's compressor knobs edit settings, not params (`modEncoderActionForNonExistentParam`).
		const project::ChainAddress song_chain{.kind = project::ChainAddress::Kind::SONG};
		const effects::EffectStates* fx = facts.chain_effects(song_chain);
		if (fx != nullptr) {
			const CompTurn turn = comp_turn(*comp, *fx, offset);
			if (sink.write_effect(song_chain, turn.path, turn.value)) {
				deluge::notify::notice_notification(turn.title, turn.value_text);
			}
		}
		return result;
	}
	const auto* address = std::get_if<ParamAddress>(&assignment.target);
	if (address == nullptr) {
		return result;
	}

	// `modEncoderAction_existentParam` (`:853-948`). A knob on a param its owner does not hold yet reads 0: the write
	// makes it.
	const deluge::engine::ParamInfo* info = info_of(*address, assignment.kind);
	const bool at_region = focus_->region.pos != 0xFFFFFFFF;
	const std::optional<int32_t> value = facts.param_value_at(*address, focus_->region.pos);
	ModTurn turn;
	turn.kind = assignment.kind;
	turn.param_id = legacy_id(*address, assignment.kind);
	turn.old_knob_pos = bind::knob_pos_of(*address, info, value.value_or(0));
	turn.new_knob_pos = bind::turn(bind::knob_kind(*address), turn.old_knob_pos, offset);

	// `:868-899` -- the popup, and the tests that suppress it.
	if (!popup_suppressed(turn) && show_popup(turn, now_ms)) {
		(void)result.actions.push_back(
		    Action{.kind = Action::Kind::Repaint, .surface = deluge::ui::shell::SurfaceRole::Main});
	}
	if (turn.new_knob_pos == turn.old_knob_pos) {
		return result; // `:902-904`
	}

	const auto* sound = std::get_if<bind::SoundFocus>(&focus_->owner);
	const bool tails_before = sound != nullptr && sound->clip && facts.allows_note_tails(sound->slot);
	const int32_t new_value = bind::value_of_knob(*address, info, turn.new_knob_pos);
	const int32_t pos = at_region ? static_cast<int32_t>(focus_->region.pos) : 0;
	if (!sink.write_param(*address, new_value, pos, static_cast<int32_t>(focus_->region.length))) {
		return result; // refused: nothing changed, so nothing is fed back
	}
	turn.value_changed = true;
	turn.note_tails_changed =
	    sound != nullptr && sound->clip && tails_before != facts.allows_note_tails(sound->slot); // `:929`

	turn.should_indicate_middle_value =
	    bind::indicates_middle(*address, info, owner_stutter_quantized(), facts.stutter_active());

	send_midi_follow_feedback(turn.kind, turn.param_id, turn.new_knob_pos, true, false); // `:908`
	if (turn.note_tails_changed) {
		(void)result.actions.push_back(Action{.kind = Action::Kind::RepaintAll}); // `:929-933`
	}
	potentially_make_it_harder_to_turn_knob(which_encoder, turn); // `:939`
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
	// `focused_value()`, not `focused()`: legacy's same-param test forwards through a horizontal
	// page to the focused value column (`Submenu::shouldForwardButtons`,
	// `gui/menu_item/submenu.cpp:181-185`) -- the page node itself has no `bind.param` to compare
	// against, so `focused()` alone would never suppress the popup on a horizontal menu at all.
	const menu::Node* focused = menu_scene().focused_value();
	if (focused == nullptr || !focused->bind.param.valid()) {
		return false;
	}
	return focused->bind.param.kind == turn.kind && static_cast<int32_t>(focused->bind.param.index) == turn.param_id;
}

bool ModKnobs::show_popup(const ModTurn& turn, uint32_t now_ms) {
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

	// The popup's `show` reports whether it redrew; a second detent on an open popup moves its text, and only the
	// caller's `Repaint` draws it.
	const bool committed = popup_.show(turn.kind, turn.param_id, turn.new_knob_pos, source1, source2, now_ms, context);

	// The popup is a scene: pushed when not stacked yet (it is input-transparent, so one may sit above it).
	for (const deluge::ui::shell::Scene* scene : stack_.scenes()) {
		if (scene->kind() == overlay::kParamPopupSceneKind) {
			return committed;
		}
	}
	(void)stack_.push(SceneId{.kind = overlay::kParamPopupSceneKind});
	return committed;
}

void ModKnobs::potentially_make_it_harder_to_turn_knob(int32_t which_encoder, const ModTurn& turn) {
	// `View::potentiallyMakeItHarderToTurnKnob`.
	if (which_encoder < 0 || which_encoder >= kKnobCount) {
		return;
	}
	int32_t knob_pos = turn.new_knob_pos;
	if (turn.kind == Kind::PATCH_CABLE) {
		// `:1063-1065` -- a patch cable is -128..+128; fold it into the indicator's range so "the
		// middle" is the same number the indicator blinks at.
		knob_pos = bind::fold_cable(knob_pos + kKnobPosOffset) - kKnobPosOffset;
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
	// `View::modButtonAction`.

	// `:1500-1506` -- the automation editor ignores a mod button, except mod button 0 on the arranger's, which keeps
	// the VU meter toggleable there.
	if (view_.in_automation_editor && !(view_.automation_on_arranger && which_button == 0)) {
		return;
	}
	pretend_knobs_untouched();                                               // `:1508`
	if (std::holds_alternative<std::monostate>(focus_->owner) || !bound()) { // `:1510`
		return;
	}
	bind::WriteSink& sink = menu::host_write_sink();

	// A MIDI track choosing its knob's CC: any mod button ends that, and does nothing else.
	if (selecting_cc_) {
		(void)sink.midi_cc_select(*focus_, selecting_knob_, false);
		selecting_cc_ = false;
		deluge::notify::notice_kind(deluge::notify::Notice::Kind::CancelPopup);
		return;
	}

	if (on) {
		// `:1512` -- a mod button is taken only in the modes it is offered in, or on performance view.
		if (!view_.mod_button_ui_mode_in_range && !view_.root_is_performance) {
			return;
		}
		// `:1513-1529` -- pressing the selected button 0 toggles the VU meter, outside a clip minder.
		if (!view_.root_is_clip_minder && mode() == which_button && which_button == 0) {
			focus_->vu_meter = !focus_->vu_meter;
		}
		// `:1530-1532` -- the mode changes before the button's own action, which reads it.
		if (const std::optional<project::KnobModeOwner> owner = mode_owner(*focus_)) {
			(void)sink.knob_mode(*owner, static_cast<uint8_t>(which_button));
		}
	}

	// `ModControllable::modButtonAction` (`:1534-1535`, `:1541-1544`): the end of a stutter, then the button's popup
	// on a press and its removal on a release. A clip's chain leaves the stutter on its compressor button; the song's
	// chain leaves it running on performance view.
	const bool chain = std::holds_alternative<bind::ChainFocus>(focus_->owner);
	const bool song = std::holds_alternative<bind::SongFocus>(focus_->owner);
	const bool ends_stutter = std::holds_alternative<bind::SoundFocus>(focus_->owner)
	                          || (chain && which_button != 4 && !view_.root_is_performance)
	                          || (song && !view_.root_is_performance);
	if (ends_stutter) {
		(void)sink.stutter(*focus_, false);
	}
	if (!on) {
		deluge::notify::notice_kind(deluge::notify::Notice::Kind::CancelPopup);
		return;
	}
	PushInputs inputs;
	const bind::RuntimeFacts& facts = menu::host_runtime_facts();
	if (const effects::EffectStates* fx = owner_effects()) {
		inputs.effects = *fx;
	}
	if (const auto* sound = std::get_if<bind::SoundFocus>(&focus_->owner)) {
		const instrument::SoundState* state = facts.sound_state(sound->slot);
		inputs.fm = state != nullptr && state->synth_mode == SynthMode::FM;
	}
	inputs.alt_delay = runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::AltGoldenKnobDelayParams);
	inputs.clip_context = is_clip_context(*focus_);
	if (const std::string text = mod_button_popup(song_->project, *focus_, which_button, inputs); !text.empty()) {
		popup_text(text);
	}
}

Result ModKnobs::mod_encoder_button_action(int32_t which_encoder, bool on, bool shift, bool learn, bool mode_held) {
	// `View::modEncoderButtonAction`. Legacy's tail always reaches `dealtWith`.
	Result result;
	result.handled = true;
	if (learn || std::holds_alternative<std::monostate>(focus_->owner) || !bound()) {
		return result; // `:1336-1340`
	}
	bind::WriteSink& sink = menu::host_write_sink();

	if (shift && on) { // `:1342-1344` -- delete the knob's automation
		const bind::KnobAssignment assignment = bind::knob_assignment(song_->project, *focus_, which_encoder);
		const auto* address = std::get_if<ParamAddress>(&assignment.target);
		if (address != nullptr && menu::host_runtime_facts().param_value(*address).has_value()
		    && sink.delete_knob_automation(*address)) {
			(void)result.actions.push_back(
			    Action{.kind = Action::Kind::Popup,
			           .surface = deluge::ui::shell::SurfaceRole::Main,
			           .popup_string = static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_AUTOMATION_DELETED)});
		}
		return result;
	}

	// `modEncoderButtonAction_changeModControllable` (`:1363-1377`): the push's table, sent to the sink.
	const bind::RuntimeFacts& facts = menu::host_runtime_facts();
	PushInputs inputs;
	if (const effects::EffectStates* fx = owner_effects()) {
		inputs.effects = *fx;
	}
	if (const auto* sound = std::get_if<bind::SoundFocus>(&focus_->owner)) {
		const instrument::SoundState* state = facts.sound_state(sound->slot);
		inputs.fm = state != nullptr && state->synth_mode == SynthMode::FM;
	}
	inputs.alt_delay = runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::AltGoldenKnobDelayParams);
	inputs.button_held = mode_held;
	inputs.clip_context = is_clip_context(*focus_);
	const KnobPush push = knob_push(song_->project, *focus_, which_encoder, on, inputs);
	if (push.midi_cc_select) {
		// The instrument takes it from a clip minder with no UI mode active; a release ends it.
		selecting_cc_ = sink.midi_cc_select(*focus_, which_encoder, on) && on;
		selecting_knob_ = which_encoder;
	}
	// `:1371` -- a push that edited the instrument marks it, on a clip.
	if (apply_push(sink, *focus_, push) && push.edited && is_clip_context(*focus_)) {
		(void)sink.instrument_edited(*focus_);
	}
	if (push.comp) {
		focus_->comp = *push.comp;
	}
	if (!push.popup.empty()) {
		popup_text(push.popup);
	}
	else if (!push.notice.empty()) {
		deluge::notify::notice_text(push.notice);
	}
	if (&stack_.input_top() == static_cast<const deluge::ui::shell::Scene*>(&menu_scene())) {
		menu_scene().read_value_again(); // `:1374-1376`
	}
	return result;
}

// -- The LEDs and the indicators -----------------------------------------------------------------

ModKnobs::IndicatorLevel ModKnobs::indicator_level(const ModIndicatorInfo& info, bool stuttering) {
	// `View::setKnobIndicatorLevel`'s arithmetic, over reads
	// the reads `indicator_info` made.
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
			knob_pos = bind::fold_cable(knob_pos);
		}
		out.declared = true;
		// **The `uint8_t` is legacy's, not a narrowing this port introduces**, and the E4 final
		// review asked for the argument at the code rather than only on `IndicatorLevel::level`.
		// `View::setKnobIndicatorLevel` carries `int32_t knobPos` right to its last line and hands
		// it to `indicator_leds::setKnobIndicatorLevel(uint8_t whichKnob, uint8_t level, bool)`
		// (`View::setKnobIndicatorLevel` calling `indicator_leds::setKnobIndicatorLevel`), so the value is
		// truncated by an implicit conversion at exactly this point. The `>> 5` and `& 31` the
		// review cites (`hid/led/indicator_leds.cpp:196-206`) run on that already-truncated
		// parameter, not on the `int32_t` -- there is no wider legacy arithmetic to be faithful to.
		// An explicit cast where legacy has an implicit one is the same value and a visible one.
		out.level = static_cast<uint8_t>(knob_pos);
		out.bipolar = info.bipolar;
		return out;
	}

	if (info.non_existent_param) { // `:1454-1458`
		out.declared = true;
		// Same implicit-narrowing point as above.
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
	//          `int32_t knobPos;` with no initialiser (`View::setKnobIndicatorLevel`) and assigns it on three of
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

ModIndicatorInfo ModKnobs::indicator_info(int32_t which_encoder) const {
	// The reads `View::setKnobIndicatorLevel` makes, from the records and the facts.
	ModIndicatorInfo out{};
	const bind::RuntimeFacts& facts = menu::host_runtime_facts();
	const bind::KnobAssignment assignment = bind::knob_assignment(song_->project, *focus_, which_encoder);
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
	const deluge::engine::ParamInfo* info = info_of(*address, assignment.kind);
	out.has_auto_param = true;
	out.kind = assignment.kind;
	out.param_id = legacy_id(*address, assignment.kind);
	out.knob_pos = bind::knob_pos_of(*address, info, value.value_or(0));
	out.bipolar = bind::bipolar(*address, info);
	if (info != nullptr && info->file_name == "stutterRate") {
		out.quantized_stutter = owner_stutter_quantized();
	}
	return out;
}

void ModKnobs::declare_indicators(deluge::ui::LedFrame& frame) {
	// `View::setKnobIndicatorLevels`.
	// `:1380-1382` -- no root UI: an empty scene stack.
	if (stack_.depth() == 0) {
		return;
	}
	if (view_.in_automation_editor) {
		return; // `:1385-1388` -- the automation editor's own display owns the columns
	}
	if (std::holds_alternative<std::monostate>(focus_->owner) || !bound()) {
		// `:1398-1400` `indicator_leds::clearKnobIndicatorLevels()`.
		for (int32_t k = 0; k < kKnobCount; k++) {
			(void)frame.indicator(static_cast<deluge::ui::IndicatorId>(k), deluge::ui::knob_indicator_levels(0, false));
		}
		return;
	}
	const bool stuttering = menu::host_runtime_facts().stutter_active();
	for (int32_t k = 0; k < kKnobCount; k++) { // `:1390-1396`
		if (indicator_blinking(k)) {
			// A blinking knob is driven by its blink's phase.
			(void)frame.indicator(
			    static_cast<deluge::ui::IndicatorId>(k),
			    deluge::ui::knob_indicator_blink(now_ms_ - blink_indicator_start_ms_[k], blink_indicator_bipolar_[k]));
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
	const bool its_the_song = !is_clip_context(*focus_); // `:1559`
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
		if (its_the_song && on && mod_knob_mode == 0 && focus_->vu_meter) {
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
	// `View::sendMidiFollowFeedback`.
	if (!midiFollow.isFeedbackEnabled()) {
		return;
	}
	if (!is_clip_context(*focus_)) { // `:1716` -- don't send feedback for song params
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
