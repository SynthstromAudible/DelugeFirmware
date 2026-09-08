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
/// @brief The five action items and the performance-editing-mode mirror. See `actions.h` and
///        `scenes/menu/action.h`.

#include "scenes/menu/nodes/actions.h"

#include "scenes/l10n_bridge.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/profile.h"

#include "ui/oled/list.h"
#include "ui/oled/text.h"
#include "ui/oled/values.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace deluge::scenes::menu {

namespace {
/// @brief The mirror `action.h` documents. Seeded to legacy's own start-of-day state:
///        `PerformanceView::defaultEditingMode` starts `false` in its constructor
///        (`gui/views/performance_view.cpp:239`) and `focusRegained` never touches it, which
///        `EditingMode::readCurrentValue` maps to
///        `DISABLED` (`performance_session_view/editing_mode.h:36-38`).
PerformanceEditingMode g_perform_mode = PerformanceEditingMode::DISABLED;
} // namespace

PerformanceEditingMode performance_editing_mode() {
	return g_perform_mode;
}

void set_performance_editing_mode(PerformanceEditingMode mode) {
	g_perform_mode = mode;
}

} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;

size_t depth_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.depth : 0;
}

/// @brief The node on screen, so an item -- which is handed no `Node` of its own -- can draw its own
///        title. The same helper `dynamic.cpp` and `settings.cpp` keep.
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
	// **Never reached on the shipped path, and that is the fidelity result, not an oversight.**
	// `after_render` runs `enter_child` -> `enter_leaf` -> `on_focus` and then the action drain's
	// `ascend()` within the *same* call, so the item is focused for zero renders -- which is exactly
	// legacy, where `shouldEnterSubmenu() == false` means `SoundEditor` calls `selectButtonPress()`
	// and never navigates in at all (`gui/ui/sound_editor.cpp:420-423`). (`ResetItem::render`
	// (`nodes/settings.cpp:1105`) *is* drawn for one tick, because its request is a scene push the
	// router applies a tick later.)
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
	state.pending_action = MenuActionRequest{.action = action_, .arg = 0};
}

// ---------------------------------------------------------------------------------------------
// `PerformEditorItem` -- `performance_session_view::EditingMode`

void PerformEditorItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	// `EditingMode::readCurrentValue` (`editing_mode.h:35-46`): the cursor starts on whatever mode
	// the performance view is in. `Selection::beginSession` is what calls it, and `on_focus` is
	// `beginSession` -- so the read happens here rather than every frame, which is also what stops a
	// turn from being undone by the next render.
	const size_t d = depth_of(state);
	state.list[d] = deluge::ui::ListState{};
	state.list[d].cursor = static_cast<int16_t>(performance_editing_mode());
	state.scroll[d] = deluge::ui::ScrollText{};
}

void PerformEditorItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	draw_title_for(f, state);

	// `EditingMode::getOptions` (`editing_mode.h:99-107`), in its own order -- the order IS the
	// `PerformanceEditingMode` numbering, which is what makes the cursor the value.
	static_assert(static_cast<int32_t>(PerformanceEditingMode::DISABLED) == 0);
	static_assert(static_cast<int32_t>(PerformanceEditingMode::VALUE) == 1);
	static_assert(static_cast<int32_t>(PerformanceEditingMode::PARAM) == 2);
	const std::string_view names[] = {
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_DISABLED)),
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_PERFORM_EDIT_VALUE)),
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_PERFORM_EDIT_PARAM)),
	};

	const size_t d = depth_of(state);
	int32_t index = state.list[d].cursor;
	state.render_resp =
	    deluge::ui::oled::enum_value(f, deluge::ui::oled::menu_rect(f), index, names, state.list[d], state.scroll[d]);

	if (!state.render_resp.entered) {
		return;
	}

	// `EditingMode::selectButtonPress` (`editing_mode.h:50-97`). `currentMode` is whatever
	// `writeCurrentValue` last stored from the cursor (`:47`), so the cursor is the mode.
	const auto mode = static_cast<PerformanceEditingMode>(
	    std::clamp<int32_t>(index, 0, static_cast<int32_t>(PerformanceEditingMode::PARAM)));

	if (mode == PerformanceEditingMode::DISABLED) {
		// `:50-52` -- `return nullptr`, i.e. go up a level. `render_resp.entered` is left set, and
		// `MenuScene::after_render`'s leaf arm ascends. No request is filed: legacy does not touch
		// `performanceView` on this arm either.
		return;
	}

	// `:55-60` -- the two bools the mirror stands in for, written on the press and nowhere else.
	set_performance_editing_mode(mode);
	state.pending_action =
	    MenuActionRequest{.action = MenuAction::OpenPerformanceView, .arg = static_cast<uint16_t>(mode)};

	// `:73` -- `return NO_NAVIGATION`. Clearing `entered` is how a `CustomItem` says "I consumed
	// this press" (`actions.h`); leaving it set would ascend out of a page legacy stays on.
	state.render_resp.entered = false;
}

deluge::ui::shell::Result PerformEditorItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                    const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                    const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	// `enum_value` claims the turn and the select during the render, as every other list page here
	// does. Nothing left to consume.
	return {};
}

} // namespace deluge::scenes::menu::nodes
