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
/// @brief `EditNameItem`'s three members. See `nodes/edit_name.h`.

#include "ui/scenes/menu/nodes/edit_name.h"

#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/rename/rename_scene.h"

#include "ui/shell/profile.h"
#include "ui/widgets/oled/text.h"

namespace deluge::ui::scenes::menu::nodes {

void EditNameItem::render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	// A shortcut pad opens the menu on this item, which is then the menu's root: the rename window is all
	// that is on screen, as `SoundEditor::renderOLED`'s early return at depth 0 leaves it. From a page's
	// list (depth > 0) the title shows beneath the transparent rename scene.
	if (state.depth == 0) {
		return;
	}
	// Below the root, drawn for at most the one tick between `on_focus` asking for the push and the router applying
	// it, so what matters is only that it is never a blank frame: the item's own title, exactly what
	// `PendingCustom::render` draws. (Legacy never draws this item at all -- `beginSession` opens
	// the rename UI before `SoundEditor` gets to render anything.)
	const Node* node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::widgets::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                                    : std::string_view{});
}

deluge::ui::shell::Result EditNameItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                               const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                               const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	// The rename scene owns every input while it is up, and this item is only ever focused for the
	// tick before it appears. Nothing to consume.
	return {};
}

void EditNameItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `EditName::beginSession`: on an instrument clip, every audition ends before the editor opens.
	if (auto* v = ctx.ports.view_actions) {
		v->end_audition(bind::AuditionEnd::ClipViewForInstrumentClip);
	}
	// `EditName::beginSession` (`origin/main:src/deluge/gui/menu_item/edit_name.cpp`): point the editor at the model
	// and open it. `open()` is the `renameClipUI.clip = clip` / `renameOutputUI.output = ...`
	// assignment, generalised -- the target table reads whichever member of the context it needs.
	rename::rename_scene().open(ctx);
	state.walk.pending_scene = deluge::ui::shell::SceneId{rename::kRenameSceneKind, target_};
}

} // namespace deluge::ui::scenes::menu::nodes
