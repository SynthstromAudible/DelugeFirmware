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
/// @brief The menu's MIDI learn. See `learn.h`.
///
/// `SoundEditor`'s learn answers and `unlearnAction`, forwarded to the focused item as legacy's
/// `getCurrentMenuItem()->learnX(...)` does. MIDI reaches `offer` as a learn offer routed to the menu by `ShellHostUI`.

#include "ui/scenes/menu/learn.h"

#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/menu_core.h"

#include "instruments/classic/cables.h"
#include "project/song.h"

#include "base/notify/dirty_set.h"
#include "base/notify/notice_queue.h"
#include "gui/l10n/strings.h"

#include <algorithm>

namespace deluge::ui::scenes::menu {

using deluge::ui::shell::ButtonId;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

/// @brief The learn node's `CustomItem`, or null when it is not a `Kind::Custom` leaf.
const CustomItem* custom_of(const Node* node) {
	return node != nullptr && node->kind == Kind::Custom ? node->custom : nullptr;
}

/// @brief Tell the user what a learn did, as legacy's items do: the item on its own screen redraws, and one reached
///        through its page (`soundEditor.getCurrentMenuItem() != this`) pops @p what instead.
void announce(const MenuState& st, const Node& learned, deluge::l10n::String what) {
	if (&learned == focused(st)) {
		deluge::base::notify::dirty().raise(deluge::base::notify::Dirty::Oled);
	}
	else {
		deluge::base::notify::notice_string(static_cast<uint16_t>(what));
	}
}

/// @brief Deliver one MIDI learn message to the learn node: its own hook for a plain item, its members for a bespoke
///        one. @return Whether it learned.
bool learn_into(const MenuContext& ctx, const MenuState& st, const UiEvent& event) {
	const Node* at = MenuLearn::node(ctx, st);
	if (at == nullptr || !event.device) {
		return false;
	}
	bool learned = false;
	if (at->learn != nullptr) {
		learned = at->learn(ctx, event);
	}
	else if (const CustomItem* item = custom_of(at)) {
		switch (event.kind) {
		case UiEvent::Kind::MidiNoteOn:
			learned = item->learn_note_on(ctx, event.device, event.channel, event.data1);
			break;
		case UiEvent::Kind::MidiCc:
			learned = item->learn_cc(ctx, event.device, event.channel, event.data1, event.data2);
			break;
		case UiEvent::Kind::MidiProgramChange:
			learned = item->learn_program_change(ctx, event.device, event.channel, event.data1);
			break;
		default:
			break;
		}
	}
	if (learned) {
		announce(st, *at, deluge::l10n::String::STRING_FOR_LEARNED);
	}
	return learned;
}

} // namespace

const Node* MenuLearn::node(const MenuContext& ctx, const MenuState& st) {
	const Node* at = focused(st);
	if (at != nullptr && at->kind == Kind::Submenu) {
		at = selected_child(ctx, st, *at);
	}
	return at != nullptr && at->kind != Kind::Submenu ? at : nullptr;
}

std::optional<deluge::project::OwnerParamTarget> MenuLearn::target(const MenuContext& ctx, const MenuState& st) const {
	const Node* node = MenuLearn::node(ctx, st);
	if (node == nullptr) {
		return std::nullopt;
	}
	// A bespoke item whose descriptor is not a `Binding::param` answers first -- the three
	// patch-cable-strength pages, whose `getLearningThing()` is assembled from the navigation state
	// (`node.h`'s `CustomItem::learn_descriptor`). Every other item leaves it null and falls through
	// to the `ParamRef` answer, which is what all 53 `PatchedParam` nodes and every `UnpatchedParam`
	// node use.
	if (node->kind == Kind::Custom && node->custom != nullptr) {
		const deluge::synth::CableTarget from_item = node->custom->learn_descriptor(ctx, st);
		const std::optional<deluge::project::ControlsAddress> owner = bindings::controls_owner(ctx);
		if (!from_item.is_null() && owner) {
			return deluge::project::OwnerParamTarget{
			    .owner = *owner,
			    .param = deluge::project::engine_target(deluge::instruments::classic::knob_param_of(from_item).value)};
		}
	}
	return bindings::learn_target(*node, ctx);
}

bool MenuLearn::allows_mode(const MenuContext& ctx, const MenuState& st) const {
	const Node* node = MenuLearn::node(ctx, st);
	if (node == nullptr) {
		return false;
	}
	if (node->allows_learn_mode || bindings::learn_target(*node, ctx).has_value()) {
		return true;
	}
	return node->kind == Kind::Custom && node->custom != nullptr && !node->custom->learn_descriptor(ctx, st).is_null();
}

bool MenuLearn::wants_blink(const MenuState& st) const {
	const CustomItem* item = focused_custom(st);
	return item != nullptr && item->wants_learn_blink();
}

bool MenuLearn::offer(const MenuContext& ctx, const MenuState& st, const UiEvent& event, const UiContext& ui) const {
	if (!mode_ || !event.device) {
		return false;
	}
	// A param item learns a CC or a pitch bend as a MIDI knob (`MenuItem::learnCC` is `learnKnob`); any other item
	// learns a CC itself.
	const bool param = target(ctx, st).has_value();
	switch (event.kind) {
	case UiEvent::Kind::MidiNoteOn:
		(void)learn_into(ctx, st, event);
		return true;
	case UiEvent::Kind::MidiProgramChange:
		if (!ui.shift) {
			(void)learn_into(ctx, st, event);
		}
		return true;
	case UiEvent::Kind::MidiCc:
		if (!ui.shift) {
			(void)(param ? knob(ctx, st, event.device, event.data1, event.channel) : learn_into(ctx, st, event));
		}
		return true;
	case UiEvent::Kind::MidiPitchBend:
		if (ui.shift) {
			return false;
		}
		if (param) {
			(void)knob(ctx, st, event.device, 128, event.channel);
		}
		return true;
	default:
		return false;
	}
}

bool MenuLearn::knob(const MenuContext& ctx, const MenuState& st, deluge::midi::DeviceId device, int32_t which_knob,
                     int32_t midi_channel) const {
	const std::optional<deluge::project::OwnerParamTarget> param = target(ctx, st);
	if (!param || ctx.ports.learn == nullptr
	    || !ctx.ports.learn->learn_knob(*param, device, which_knob, midi_channel)) {
		return false;
	}
	deluge::base::notify::notice_string(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_LEARNED));
	deluge::base::notify::dirty().raise(deluge::base::notify::Dirty::ModTarget); // `view.setKnobIndicatorLevels()`
	return true;
}

bool MenuLearn::gold_turn(const MenuContext& ctx, const MenuState& st, const UiEvent& event) const {
	if (!mode_ || event.kind != UiEvent::Kind::Encoder
	    || (event.encoder != deluge::ui::widgets::core::EncoderRole::GoldLower
	        && event.encoder != deluge::ui::widgets::core::EncoderRole::GoldUpper)) {
		return false;
	}
	if (!(ctx.is_kit && ctx.is_kit_global_fx)) {
		(void)knob(ctx, st, {}, event.encoder == deluge::ui::widgets::core::EncoderRole::GoldUpper ? 1 : 0, 255);
	}
	return true;
}

void MenuLearn::unlearn(const MenuContext& ctx, const MenuState& st) const {
	// Unconditional, like legacy's own call: `MenuItem::unlearnAction`'s base is an empty body, so
	// an item with nothing to forget forgets nothing and no caller has to ask first.
	const Node* at = node(ctx, st);
	bool forgot = false;
	if (at != nullptr && at->unlearn != nullptr) {
		forgot = at->unlearn(ctx);
	}
	else if (const CustomItem* item = custom_of(at)) {
		forgot = item->unlearn(ctx);
	}
	if (forgot) {
		announce(st, *at, deluge::l10n::String::STRING_FOR_UNLEARNED);
	}
	// `MenuItemWithCCLearning::unlearnAction`: a param's learned knob goes,
	// with a popup when there was one.
	const std::optional<deluge::project::OwnerParamTarget> learned = target(ctx, st);
	if (learned && ctx.model != nullptr && ctx.ports.learn != nullptr && ctx.model->mappings.find(*learned) != nullptr
	    && ctx.ports.learn->learn(*learned, std::nullopt, std::nullopt)) {
		deluge::base::notify::notice_string(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_UNLEARNED));
	}
}

bool MenuLearn::observe(const MenuContext& ctx, const MenuState& st, const UiEvent& event, const UiContext& ui,
                        std::optional<bool> host_allows) {
	// A press with no UI mode on refuses an item that does not allow learn with a popup, unlearns with Shift held,
	// else starts learn mode; the release ends it.
	if (event.kind != UiEvent::Kind::Button || event.button != ButtonId::Learn) {
		return false;
	}
	const bool pad_held = std::ranges::any_of(ui.held_pads.cols, [](uint32_t col) { return col != 0; });
	mode_ = false;
	const bool restart_blink = !event.on && wants_blink(st);
	// With a host, its answer alone: `main`'s test is `!currentUIMode`, and a pad fed to this shell (a shortcut or a
	// patching source) sets no mode there. Without one, the held pads and the AFFECT ENTIRE hold stand in for it.
	const bool no_mode = host_allows.has_value() ? *host_allows : (!pad_held && !ctx.addresses.affect_entire);
	if (event.on && no_mode) {
		if (!allows_mode(ctx, st)) {
			deluge::base::notify::notice_string(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_CANT_LEARN));
		}
		else if (ui.shift) {
			unlearn(ctx, st);
		}
		else {
			mode_ = true;
			restart_ = true; // `blinkLed(LEARN, 255, 1)` starts lit at the press
		}
	}
	return restart_blink;
}

void MenuLearn::leds(const MenuState& st, deluge::ui::widgets::core::LedFrame& f,
                     deluge::ui::widgets::core::LedState shared_blink) {
	// `SoundEditor::beginScreen`'s tail (`SoundEditor::updatePadLightsFor`): every screen change asks the
	// item whether it wants the LEARN LED, and blinks or extinguishes it accordingly. Declared on
	// every tick rather than only on a change, because that is what an `LedFrame` is -- the full
	// state of the LEDs this scene owns, redeclared each time. It is `blinkLed`'s default blink, BACK's.
	// While learning, `blinkLed(LEARN, 255, 1)`'s fast blink (`SoundEditor::buttonAction`'s LEARN arm), restarted at
	// the press.
	if (restart_) {
		restart_ = false;
		f.restart(deluge::ui::widgets::core::BlinkRate::Fast);
	}
	const auto learn = static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Learn);
	if (mode_) {
		(void)f.set(learn, deluge::ui::widgets::core::LedState::shared(deluge::ui::widgets::core::BlinkRate::Fast));
	}
	else {
		(void)f.set(learn, wants_blink(st) ? shared_blink
		                                   : deluge::ui::widgets::core::LedState{
		                                         deluge::ui::widgets::core::LedState::Kind::Off, 0, 0});
	}
}

} // namespace deluge::ui::scenes::menu
