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
/// `SoundEditor`'s three delivery members and `unlearnAction`, forwarded to the focused item exactly as legacy's
/// `getCurrentMenuItem()->learnX(...)` did (`sound_editor.cpp:516`, `:1550-1570`). The note-on and program-change
/// halves reach here through `ShellHostUI`'s two `UI` overrides (`gui/ui/shell_guest_services.h`); `cc` has no `UI`
/// virtual to override. See `node.h`'s note on the declarations.

#include "scenes/menu/learn.h"

#include "model/project_bridge/controls_bridge.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/menu_core.h"

#include "deluge-project/song.h"

#include "gui/l10n/strings.h"
#include "notify/notice_queue.h"

#include <algorithm>

namespace deluge::scenes::menu {

using deluge::ui::shell::ButtonId;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

std::optional<deluge::project::OwnerParamTarget> MenuLearn::target(const MenuContext& ctx, const MenuState& st) const {
	const Node* node = focused(st);
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
	const Node* node = focused(st);
	if (node != nullptr && node->kind == Kind::Submenu) {
		node = selected_child(ctx, st, *node);
	}
	if (node == nullptr || node->kind == Kind::Submenu) {
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

bool MenuLearn::note_on(const MenuState& st, ::MIDICable& cable, int32_t channel, int32_t note) const {
	const CustomItem* item = focused_custom(st);
	return item != nullptr && item->learn_note_on(cable, channel, note);
}

bool MenuLearn::cc(const MenuState& st, ::MIDICable& cable, int32_t channel, int32_t cc, int32_t value) const {
	const CustomItem* item = focused_custom(st);
	return item != nullptr && item->learn_cc(cable, channel, cc, value);
}

bool MenuLearn::program_change(const MenuState& st, ::MIDICable& cable, int32_t channel, int32_t program) const {
	const CustomItem* item = focused_custom(st);
	return item != nullptr && item->learn_program_change(cable, channel, program);
}

void MenuLearn::unlearn(const MenuContext& ctx, const MenuState& st) const {
	// Unconditional, like legacy's own call: `MenuItem::unlearnAction`'s base is an empty body, so
	// an item with nothing to forget forgets nothing and no caller has to ask first.
	if (const CustomItem* item = focused_custom(st); item != nullptr) {
		item->unlearn();
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
