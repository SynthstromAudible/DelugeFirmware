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
/// @brief The menu's shortcut pads. See `pads.h`.

#include "scenes/menu/pads.h"

#include "scenes/menu/chains.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/walk.h"

#include "gui/l10n/strings.h"
#include "notify/notice_queue.h"

#include "scenes/menu/shortcut_blink.h"
#include "scenes/menu/shortcuts.h"

namespace deluge::scenes::menu {

using deluge::ui::shell::Action;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

void MenuPads::render_grid(MenuCore core, deluge::ui::grid::GridFrame& f) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	// Reads no input at all. That is what keeps "a scene reads any one input in at most one render
	// pass per tick" (`shell/README.md`) true for a scene drawn on two surfaces: the OLED pass owns
	// turn/select/back/edit, and this pass owns nothing. It paints no pixel either: the shortcut
	// blinks are pad flashes, as legacy's `blinkShortcut` plays them (`sound_editor.cpp:1205-1245`).
	const Node* node = pad_subject(ctx, st);
	if (node == nullptr) {
		return;
	}
	const uint32_t now = f.now_ms();
	const BlinkPlan plan = plan_blinks(ctx, st, *node);
	if (st.blink_node != node || st.blink_plan != plan) {
		st.blink_node = node;
		st.blink_plan = plan;
		st.blink_anchor_ms = now;
		st.blink_fired_step = UINT32_MAX;
	}
	if (!plan.param.valid && !plan.any_source()) {
		return;
	}
	// Legacy steps its counter on a timer and flashes once per step; a frame lands anywhere inside
	// a step, so the step is derived from the clock and flashed the first time a frame sees it.
	const uint32_t step = blink_step(st.blink_anchor_ms, now);
	if (step != st.blink_fired_step) {
		fire_blink_step(f, plan, step);
		st.blink_fired_step = step;
	}
	// `next_timer()` answers one fixed interval for the whole scene, but the steps are unequal
	// (20 ms, then 180 ms), so this arms the grid's gate for the next step's edge itself -- the
	// grid's alone, since a blink changes no OLED pixel.
	f.grid_gate().request_repaint_after(ms_to_next_step(st.blink_anchor_ms, now), now);
}

std::optional<Result> MenuPads::handle_pad(MenuCore core, const UiEvent& event, const UiContext& ui) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	// The shortcut-pad jump. A `Pad` event is never masked (`ui/core/frame_context.h`, rule
	// 4), so it always arrives here. Legacy gates the non-auditioning case on
	// `Buttons::isShiftButtonPressed()` (`sound_editor.cpp:1169`); the auditioning half is a UI
	// mode this layer may not read, so shift is the whole gate.
	if (event.kind == UiEvent::Kind::Pad && event.on && ui.shift) {
		// Legacy's second-layer toggle (`sound_editor.cpp:1364-1379`), which is a *double press of
		// the focused item's own shortcut pad* -- not shift, which is only the gate above. Pressing
		// any other pad clears it, and a patch-cable item never arms it.
		//
		// Scoped to the sound arm, because that is the only branch legacy touches
		// `secondLayerShortcutsToggled` in (the `else` at `:1303`) and the only pair of roots with a
		// second layer at all. On any other root the flag is left exactly as it was, rather than
		// being cleared by a press legacy never looked at.
		const bool sound_arm = ctx.root == MenuContext::Root::Sound || ctx.root == MenuContext::Root::Drum;
		if (sound_arm) {
			const Node* current = pad_subject(ctx, st);
			const ShortcutPos here = current != nullptr ? shortcut_pos(ctx.root, *current) : ShortcutPos{};
			const bool same_pad = here.valid && here.x == event.x && here.y == event.y;
			// `getCurrentMenuItem()->getParamKind()`: on a horizontal page that is the focused
			// column's own when it is not a submenu (`Submenu::getParamKind`, `shouldForwardButtons`),
			// which is `focused_value()`.
			const Node* value = focused_value(ctx, st);
			const bool is_patch_cable =
			    value != nullptr && value->bind.param.kind == deluge::modulation::params::Kind::PATCH_CABLE;
			st.second_layer = (same_pad && !is_patch_cable) ? !st.second_layer : false;
		}

		const Node* target = shortcut_node(ctx.root, sound_arm && st.second_layer, event.x, event.y);
		if (target != nullptr && !shortcut_applies(*target, ctx)) {
			deluge::notify::notice_string(
			    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_PARAMETER_NOT_APPLICABLE));
			Result r;
			r.handled = true;
			return r;
		}
		// A node the registry never numbered cannot be named in a `SceneId`, and `on_enter` would
		// refuse the id and roll the move back -- so the jump is simply not offered.
		const uint16_t id = target != nullptr ? arm_landing(ctx, *target) : 0;
		if (id != 0) {
			// `setup()` -> `beginScreen` -> `endScreen` ends the page being left (`sound_editor.cpp:977-982`), and
			// `HorizontalMenu::endSession` cancels its notification (`horizontal_menu.cpp:576-591`).
			if (on_horizontal_page(ctx, st)) {
				deluge::notify::notice_kind(deluge::notify::Notice::Kind::CancelNotification);
			}
			// `Replace`, not an in-place re-target: `SceneId::arg` is how the shell spells which
			// node is open (`Shell::state_lines()` -> `app16/<id>`), and a path the scene moved
			// behind the stack's back would make that line lie. `on_enter` does the re-target on
			// the way back in, and `save_state`/`restore_state` carry what should survive.
			Result r;
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::Replace, .id = SceneId{SceneKind::FirstApp, id}});
			return r;
		}
		return Result::unhandled();
	}
	return std::nullopt;
}

uint16_t MenuPads::arm_landing(const MenuContext& ctx, const Node& target) {
	landing_column_ = nullptr;
	if (const Node* page = shortcut_landing_page(ctx, target); page != nullptr && node_id(*page) != 0) {
		landing_column_ = &target;
		return node_id(*page);
	}
	return node_id(target);
}

deluge::ui::grid::GreyoutMask greyout_mask(const MenuContext& ctx, const MenuState& st) {
	// `SoundEditor::getGreyoutColsAndRows` (`sound_editor.cpp:441-484`). The two arms that branch on
	// the root UI's type arrive as `MenuContext::root_greyout`. `doGreyout == false` is spelled as an
	// all-zero mask, which `greyout()` already reads as "grey nothing".
	// Same reason as `leds()`: with no path there is no menu on screen, so greying the grid under
	// it would leave the base view dimmed with nothing over it.
	if (focused(st) == nullptr) {
		return {};
	}
	// `:450-458`: the KEYBOARD_SCREEN and AUTOMATION arms, which branch on the root UI's type.
	if (ctx.root_greyout == MenuContext::RootGreyout::Nothing) {
		return {};
	}
	if (ctx.root_greyout == MenuContext::RootGreyout::SpareSidebar) {
		return {0xFFFFFFFCu, 0};
	}
	if (ctx.is_performance) {
		return {}; // `:248` PERFORMANCE
	}
	// `:274-277` INSTRUMENT_CLIP's `inNoteEditor()` sub-arm: grey the sidebar only.
	if (ctx.root == MenuContext::Root::NoteEditor) {
		return {0x03, 0};
	}
	// `:278-281` INSTRUMENT_CLIP's `inNoteRowEditor()` sub-arm: don't grey out the note row editor.
	if (ctx.root == MenuContext::Root::NoteRowEditor) {
		return {};
	}
	if (ctx.addresses.clip && !ctx.is_song_view && !ctx.is_audio_clip) {
		return {0xFFFFFFFEu, 0}; // `:236` / `:283` INSTRUMENT_CLIP, the plain sound-editor arm
	}
	// `:252` default -- which is where an AUDIO_CLIP root UI lands too: legacy's switch has no
	// `UIType::AUDIO_CLIP` case, so an audio clip greys everything including the sidebar. Testing
	// `!is_audio_clip` above is what keeps it out of the INSTRUMENT_CLIP arm.
	return {0xFFFFFFFFu, 0};
}

std::optional<uint32_t> pad_next_timer(const MenuContext& ctx, const MenuState& st) {
	const Node* node = pad_subject(ctx, st);
	if (node == nullptr) {
		return std::nullopt;
	}
	const BlinkPlan plan = plan_blinks(ctx, st, *node);
	if (plan.any_source()) {
		return kSourceBlinkMs;
	}
	if (plan.param.valid) {
		return kShortcutBlinkMs;
	}
	return std::nullopt;
}

} // namespace deluge::scenes::menu
