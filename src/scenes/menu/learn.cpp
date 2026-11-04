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

#include "scenes/menu/learn.h"

#include "model/project_bridge/controls_bridge.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/menu_core.h"

#include "deluge-project/song.h"

#include "gui/l10n/strings.h"
#include "notify/dirty_set.h"
#include "notify/notice_queue.h"

#include <algorithm>

namespace deluge::scenes::menu {

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
		deluge::notify::dirty().raise(deluge::notify::Dirty::Oled);
	}
	else {
		deluge::notify::notice_string(static_cast<uint16_t>(what));
	}
}

/// @brief Deliver one MIDI learn message to the learn node: its own hook for a plain item, its members for a bespoke
///        one. @return Whether it learned.
bool learn_into(const MenuContext& ctx, const MenuState& st, const UiEvent& event) {
	const Node* at = MenuLearn::node(ctx, st);
	if (at == nullptr || event.cable == nullptr) {
		return false;
	}
	bool learned = false;
	if (at->learn != nullptr) {
		learned = at->learn(ctx, event);
	}
	else if (const CustomItem* item = custom_of(at)) {
		switch (event.kind) {
		case UiEvent::Kind::MidiNoteOn:
			learned = item->learn_note_on(*event.cable, event.channel, event.data1);
			break;
		case UiEvent::Kind::MidiCc:
			learned = item->learn_cc(*event.cable, event.channel, event.data1, event.data2);
			break;
		case UiEvent::Kind::MidiProgramChange:
			learned = item->learn_program_change(*event.cable, event.channel, event.data1);
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

UiEvent midi_event(UiEvent::Kind kind, ::MIDICable& cable, int32_t channel, int32_t data1, int32_t data2) {
	return UiEvent{.kind = kind,
	               .channel = static_cast<uint8_t>(channel),
	               .data1 = static_cast<uint8_t>(data1),
	               .data2 = static_cast<uint8_t>(data2),
	               .cable = &cable};
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
			    .param = deluge::bridge::controlTargetOf(deluge::modulation::knobTargetOf(from_item),
			                                             deluge::modulation::params::Kind::UNPATCHED_SOUND)};
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

bool MenuLearn::note_on(const MenuContext& ctx, const MenuState& st, ::MIDICable& cable, int32_t channel,
                        int32_t note) const {
	return learn_into(ctx, st, midi_event(UiEvent::Kind::MidiNoteOn, cable, channel, note, 0));
}

bool MenuLearn::cc(const MenuContext& ctx, const MenuState& st, ::MIDICable& cable, int32_t channel, int32_t cc,
                   int32_t value) const {
	return learn_into(ctx, st, midi_event(UiEvent::Kind::MidiCc, cable, channel, cc, value));
}

bool MenuLearn::program_change(const MenuContext& ctx, const MenuState& st, ::MIDICable& cable, int32_t channel,
                               int32_t program) const {
	return learn_into(ctx, st, midi_event(UiEvent::Kind::MidiProgramChange, cable, channel, program, 0));
}

bool MenuLearn::offer(const MenuContext& ctx, const MenuState& st, const UiEvent& event, const UiContext& ui) const {
	if (!mode_ || event.cable == nullptr) {
		return false;
	}
	switch (event.kind) {
	case UiEvent::Kind::MidiNoteOn:
		(void)learn_into(ctx, st, event);
		return true;
	case UiEvent::Kind::MidiProgramChange:
	case UiEvent::Kind::MidiCc:
		if (!ui.shift) {
			(void)learn_into(ctx, st, event);
		}
		return true;
	case UiEvent::Kind::MidiPitchBend:
		return !ui.shift;
	default:
		return false;
	}
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
		forgot = item->unlearn();
	}
	if (forgot) {
		announce(st, *at, deluge::l10n::String::STRING_FOR_UNLEARNED);
	}
	// `MenuItemWithCCLearning::unlearnAction` (`menu_item_with_cc_learning.cpp:25-41`): a param's learned knob goes,
	// with a popup when there was one.
	const std::optional<deluge::project::OwnerParamTarget> learned = target(ctx, st);
	if (learned && ctx.model != nullptr && ctx.sink != nullptr && ctx.model->mappings.find(*learned) != nullptr
	    && ctx.sink->learn(*learned, std::nullopt, std::nullopt)) {
		deluge::notify::notice_string(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_UNLEARNED));
	}
}

bool MenuLearn::observe(const MenuContext& ctx, const MenuState& st, const UiEvent& event, const UiContext& ui) {
	// A press with no UI mode on (a held pad is legacy's auditioning or notes-pressed mode, a kit-wide AFFECT ENTIRE
	// hold its own) refuses an item that does not allow learn with a popup, unlearns with Shift held, else starts
	// learn mode; the release ends it.
	if (event.kind != UiEvent::Kind::Button || event.button != ButtonId::Learn) {
		return false;
	}
	const bool pad_held = std::ranges::any_of(ui.held_pads.cols, [](uint32_t col) { return col != 0; });
	mode_ = false;
	const bool restart_blink = !event.on && wants_blink(st);
	if (event.on && !pad_held && !ctx.addresses.affect_entire) {
		if (!allows_mode(ctx, st)) {
			deluge::notify::notice_string(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_CANT_LEARN));
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

void MenuLearn::leds(const MenuState& st, deluge::ui::LedFrame& f, deluge::ui::LedState shared_blink) {
	// `SoundEditor::beginScreen`'s tail (`sound_editor.cpp:949-954`): every screen change asks the
	// item whether it wants the LEARN LED, and blinks or extinguishes it accordingly. Declared on
	// every tick rather than only on a change, because that is what an `LedFrame` is -- the full
	// state of the LEDs this scene owns, redeclared each time. It is `blinkLed`'s default blink, BACK's.
	// While learning, `blinkLed(LEARN, 255, 1)`'s fast blink (`sound_editor.cpp:744`), restarted at the press.
	if (restart_) {
		restart_ = false;
		f.restart(deluge::ui::BlinkRate::Fast);
	}
	const auto learn = static_cast<deluge::ui::LedId>(ButtonId::Learn);
	if (mode_) {
		(void)f.set(learn, deluge::ui::LedState::shared(deluge::ui::BlinkRate::Fast));
	}
	else {
		(void)f.set(learn,
		            wants_blink(st) ? shared_blink : deluge::ui::LedState{deluge::ui::LedState::Kind::Off, 0, 0});
	}
}

} // namespace deluge::scenes::menu
