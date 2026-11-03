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
/// @brief The menu's value session. See `value_session.h`.

#include "scenes/menu/value_session.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/note_bridge.h"

#include "notify/dirty_set.h"

namespace deluge::scenes::menu {

void ValueSession::begin(MenuCore core, const Node* value) {
	value_ = value;
	if (value == &nodes::sequence_direction && !core.ctx.is_kit) {
		core.ctx.selected_note_row = note_bridge() != nullptr && note_bridge()->row_auditioning();
	}
}

void ValueSession::enter_leaf(MenuCore core, const Node& node, FocusCause cause) const {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	switch (node.kind) {
	case Kind::Enum:
		st.enum_index = bindings::read(node, ctx);
		st.enum_list = deluge::ui::ListState{};
		st.enum_scroll = deluge::ui::ScrollText{};
		break;
	case Kind::Toggle:
		st.edit_bool = bindings::read(node, ctx) != 0;
		st.scroll[st.depth] = deluge::ui::ScrollText{};
		break;
	case Kind::Decimal:
		st.edit_int = bindings::read(node, ctx);
		st.decimal_pos = 0;
		st.cursor_blink = deluge::ui::Blink{};
		break;
	case Kind::Range: {
		// `Range::beginSession` (`gui/menu_item/range.cpp:30-33`) is `editingRangeEdge = OFF`, and
		// `IntegerRange::beginSession` (`integer_range.cpp:26-31`) then takes LEFT when the two ends
		// differ. `KeyRange` has no override, so it stops at OFF -- which is exactly what
		// `RangeSpec::focus_left_when_split` says.
		st.edit_int = bindings::read(node, ctx); // the lower edge
		st.edit_upper = st.edit_int;
		st.edit_edge = deluge::ui::oled::RangeEdge::Off;
		if (const RangeSpec* spec = node.bind.range_spec; spec != nullptr) {
			if (spec->get_upper != nullptr) {
				st.edit_upper = spec->get_upper(ctx);
			}
			if (spec->focus_left_when_split && st.edit_int != st.edit_upper) {
				st.edit_edge = deluge::ui::oled::RangeEdge::Left;
			}
		}
		break;
	}
	case Kind::Custom:
		// `CustomItem::on_focus` -- legacy's `MenuItem::beginSession`. No `bind` to read either way;
		// unlike the other kinds, a custom item's own state (if it has any) lives on the item, not
		// in `MenuState`'s shared edit storage.
		//
		// **Except for an item whose `on_focus` is really `selectButtonPress`** (`node.h`'s
		// `CustomItem::on_focus_is_press`). Legacy never navigates into such an item at all
		// (`shouldEnterSubmenu() == false`); the port descends for one tick and acts from
		// `on_focus`, which is right for a press on this row and wrong for every other way in. A
		// `NoRowPress` focus has none -- `restore_state` rebuilding a saved path, a `SceneId` deep
		// link, the shortcut-pad jump -- and running the press there fired
		// `MidiDeviceLinkedItem`'s unlink arm (clearing a linked definition file the user never
		// touched) and re-filed an `ActionItem`'s verb. Legacy's own guard is the same test spelled
		// against its own state: `Toggle::selectButtonPress` returns without toggling when
		// `soundEditor.getCurrentMenuItem() == this` (`gui/menu_item/toggle.h:35-37`).
		//
		// Scoped by the item rather than applied to every `Kind::Custom`, because a blanket skip
		// would regress a shipped, specced ruling: `EditNameItem` and the nine `nodes/sample.h`
		// items *must* re-open their scene on a restore -- see `restore_state` below, which argues
		// it with legacy agreeing (`EditName::beginSession` opens `RenameUI` unconditionally).
		if (node.custom != nullptr && (cause == FocusCause::RowPress || !node.custom->on_focus_is_press())) {
			node.custom->on_focus(ctx, st);
		}
		break;
	default:
		st.edit_int = bindings::read(node, ctx);
		break;
	}
}

void ValueSession::commit(MenuCore core, const Node& node) const {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	// `Binding::adjust` (spec E-note-family §5.2), ahead of the `Kind` switch: an offset-writing
	// node commits through the hook whatever its `Kind` is (velocity/probability/iterance are
	// `Kind::Int`), so this has to run before the switch would otherwise reach `bindings::write`.
	// `after_render` already commits before it raises the horizontal notification, so the
	// notification below reads the post-commit value through `bindings::read` -- legacy's own
	// order (`selectEncoderAction` then `displayNotification`, `horizontal_menu.cpp:326-328`).
	if (node.bind.adjust != nullptr) {
		bindings::adjust(node, ctx, st.tick.edit_offset);
		st.tick.edit_offset = 0;
		return;
	}
	switch (node.kind) {
	case Kind::Custom:
		// `CustomItem::commit` -- `writeCurrentValue` for an item whose value has no `Binding`
		// behind it (the three patch-cable-strength pages). `bindings::write` would be a no-op for
		// such a node, so the item performs its own write here, in the one place a write may happen.
		if (node.custom != nullptr) {
			node.custom->commit(ctx, st);
		}
		break;
	case Kind::Enum:
		bindings::write(node, ctx, st.enum_index);
		// A target's list can change under the write (a stored choice that no longer applies leaves it once the
		// write moves off it): the cursor follows the value, not the old position.
		if (node.bind.target.valid()) {
			st.enum_index = bindings::read(node, ctx);
		}
		break;
	case Kind::Toggle:
		bindings::write(node, ctx, st.edit_bool ? 1 : 0);
		break;
	case Kind::Range:
		// Both ends, every commit. Legacy writes both too -- `IntegerRange`/`KeyRange` edit `lower`
		// and `upper` in place and have no `writeCurrentValue` at all, because the members *are* the
		// storage (now `FlashStorage::defaultRanges`, `storage/flash_storage.h`). A commit that wrote
		// only the moved end would leave the other one behind whenever `RangeClamp::Push` dragged it.
		bindings::write(node, ctx, st.edit_int);
		if (const RangeSpec* spec = node.bind.range_spec; spec != nullptr && spec->set_upper != nullptr) {
			spec->set_upper(ctx, st.edit_upper);
		}
		break;
	default:
		bindings::write(node, ctx, st.edit_int);
		break;
	}
}

void ValueSession::read_again(MenuCore core) const {
	const Node* node = focused(core.st);
	if (node != nullptr) {
		enter_leaf(core, *node, FocusCause::NoRowPress);
		// `Value::readValueAgain` redraws (`value.h:77-80`); the shell draws a raised fact on its next tick.
		deluge::notify::raise(deluge::notify::Dirty::Oled);
	}
}

} // namespace deluge::scenes::menu
