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
/// @brief The five action items. See `actions.h` and `scenes/menu/action.h`.

#include "scenes/menu/nodes/actions.h"

#include "scenes/l10n_bridge.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/profile.h"

#include "ui/oled/list.h"
#include "ui/oled/text.h"
#include "ui/oled/values.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;

size_t depth_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.depth : 0;
}

/// @brief The cursor `PerformEditorItem` starts on: the performance view's mode as the host answers it, in the
///        `PerformanceEditingMode` numbering the page's options follow. Disabled where the context binds no host.
int16_t view_mode_cursor(const MenuContext& ctx) {
	using bind::PerformanceEditing;
	static_assert(static_cast<int>(PerformanceEditing::Disabled) == static_cast<int>(PerformanceEditingMode::DISABLED));
	static_assert(static_cast<int>(PerformanceEditing::Value) == static_cast<int>(PerformanceEditingMode::VALUE));
	static_assert(static_cast<int>(PerformanceEditing::Param) == static_cast<int>(PerformanceEditingMode::PARAM));
	const PerformanceEditing mode =
	    ctx.ports.ui != nullptr ? ctx.ports.ui->performance_editing_mode() : PerformanceEditing::Disabled;
	return static_cast<int16_t>(mode);
}

/// @brief The node on screen, so an item -- which is handed no `Node` of its own -- can draw its own
///        title. The same helper `dynamic.cpp` and `nodes/settings.cpp` keep.
void draw_title_for(deluge::ui::oled::OledFrame& f, const MenuState& state) {
	const Node* node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                           : std::string_view{});
}

} // namespace

// ---------------------------------------------------------------------------------------------
// `ActionItem` -- the four pure verbs

void ActionItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	// **Never reached on the shipped path, and that is the fidelity result, not an oversight.** `after_render` runs
	// `enter_child` -> `enter_leaf` -> `on_focus` and then the action drain's `ascend()` within the *same* call, so the
	// item is focused for zero renders -- which is exactly legacy, where `shouldEnterSubmenu() == false` means
	// `SoundEditor` calls `selectButtonPress()` and never navigates in at all (`SoundEditor::buttonAction` in
	// `origin/main:src/deluge/gui/ui/sound_editor.cpp`). (`ContextMenuItem::render` in `nodes/context_menu_items.cpp`)
	// *is* drawn for one tick, because its request is a scene push the router applies a tick later.)
	//
	// It still has to draw something: a caller that opens this node *as a root* -- `open()` directly,
	// or a `SceneId::arg` deep link -- lands on it at depth 0, where `ascend()` is a no-op, and then
	// this is the whole screen. The item's own title, exactly what `PendingCustom::render` draws.
	draw_title_for(f, state);
}

deluge::ui::shell::Result ActionItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                             const deluge::ui::shell::UiContext& /*ui_ctx*/, const MenuContext& /*ctx*/,
                                             MenuState& /*state*/) const {
	// Focused for one tick only, and nothing on this screen is interactive. Nothing to consume.
	return {};
}

void ActionItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	// The whole body of all four legacy classes: one statement in `selectButtonPress`, then
	// `NO_NAVIGATION`. `CustomItem::on_focus` IS `beginSession` (`node.h`), and a descent into one of
	// these items is what the press that legacy answered directly turns into here (`actions.h`).
	state.walk.pending_action = MenuActionRequest{.action = action_, .arg = 0};
}

// ---------------------------------------------------------------------------------------------
// `PerformEditorItem` -- `performance_session_view::EditingMode`

void PerformEditorItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `EditingMode::readCurrentValue`: the cursor starts on the mode the performance view is in. `on_focus` is
	// `Selection::beginSession`, which is what reads it, so the read happens here rather than every frame, and a turn
	// is not undone by the next render.
	const size_t d = depth_of(state);
	state.list[d] = deluge::ui::ListState{};
	state.list[d].cursor = view_mode_cursor(ctx);
	state.scroll[d] = deluge::ui::ScrollText{};
}

void PerformEditorItem::on_reread(const MenuContext& ctx, MenuState& state) const {
	// `EditingMode::readCurrentValue`: the cursor is the mode the performance view is in now.
	state.list[depth_of(state)].cursor = view_mode_cursor(ctx);
}

std::array<std::string_view, 3> PerformEditorItem::option_names() {
	static_assert(static_cast<int32_t>(PerformanceEditingMode::DISABLED) == 0);
	static_assert(static_cast<int32_t>(PerformanceEditingMode::VALUE) == 1);
	static_assert(static_cast<int32_t>(PerformanceEditingMode::PARAM) == 2);
	static_assert(std::size(kPerformEditingModeNames) == 3);
	return {
	    deluge::scenes::text_lookup(kPerformEditingModeNames[0]),
	    deluge::scenes::text_lookup(kPerformEditingModeNames[1]),
	    deluge::scenes::text_lookup(kPerformEditingModeNames[2]),
	};
}

void PerformEditorItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	draw_title_for(f, state);

	const std::array<std::string_view, 3> names = option_names();

	const size_t d = depth_of(state);
	int32_t index = state.list[d].cursor;
	state.tick.render_resp =
	    deluge::ui::oled::enum_value(f, deluge::ui::oled::menu_rect(f), index, names, state.list[d], state.scroll[d]);

	if (!state.tick.render_resp.entered) {
		return;
	}

	// `EditingMode::selectButtonPress`
	// (`origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h`). `currentMode` is whatever
	// `EditingMode::writeCurrentValue` last stored from the cursor, so the cursor is the mode.
	const auto mode = static_cast<PerformanceEditingMode>(
	    std::clamp<int32_t>(index, 0, static_cast<int32_t>(PerformanceEditingMode::PARAM)));

	if (mode == PerformanceEditingMode::DISABLED) {
		// `DISABLED` is main's `return nullptr`, i.e. go up a level. `render_resp.entered` is left set, and
		// `MenuScene::after_render`'s leaf arm ascends. No request is filed: legacy does not touch
		// `performanceView` on this arm either.
		return;
	}

	// The request carries the mode; the dispatcher writes the performance view's two bools from it
	// (`openPerformanceView`, `gui/ui/shell_action_dispatch.cpp`).
	state.walk.pending_action =
	    MenuActionRequest{.action = MenuAction::OpenPerformanceView, .arg = static_cast<uint16_t>(mode)};

	// The other two modes are main's `return NO_NAVIGATION`. Clearing `entered` is how a `CustomItem` says "I consumed
	// this press" (`actions.h`); leaving it set would ascend out of a page legacy stays on.
	state.tick.render_resp.entered = false;
}

deluge::ui::shell::Result PerformEditorItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                    const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                    const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	// `enum_value` claims the turn and the select during the render, as every other list page here
	// does. Nothing left to consume.
	return {};
}

} // namespace deluge::scenes::menu::nodes
