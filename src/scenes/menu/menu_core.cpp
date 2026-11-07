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
/// @brief The menu's read-only tree queries. See `menu_core.h`.

#include "scenes/menu/menu_core.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/chains.h"

namespace deluge::scenes::menu {

const Node* focused(const MenuState& st) {
	return st.depth < kMenuDepth ? st.path[st.depth] : nullptr;
}

size_t visible_children(const MenuContext& ctx, const Node& node, std::span<const Node*> out) {
	return bindings::listed_children(
	    node, ctx, out, renders_horizontal(node, ctx) ? bindings::ListFor::Columns : bindings::ListFor::Rows);
}

const Node* child_at(const MenuContext& ctx, const Node& node, int32_t index) {
	const Node* kids[kMaxMenuChildren];
	const size_t n = visible_children(ctx, node, kids);
	if (index < 0 || static_cast<size_t>(index) >= n) {
		return nullptr;
	}
	return kids[index];
}

const Node* selected_child(const MenuContext& ctx, const MenuState& st, const Node& node) {
	if (node.kind != Kind::Submenu) {
		return nullptr;
	}
	return renders_horizontal(node, ctx) ? child_at(ctx, node, st.columns[st.depth].focused)
	                                     : child_at(ctx, node, st.list[st.depth].cursor);
}

const Node* focused_value(const MenuContext& ctx, const MenuState& st) {
	const Node* node = focused(st);
	if (node == nullptr) {
		return nullptr;
	}
	if (node->kind != Kind::Submenu) {
		return node; // a leaf IS what the encoders edit
	}
	if (!renders_horizontal(*node, ctx)) {
		return nullptr; // a vertical page's row is not itself a value -- `shouldForwardButtons`
		                // (`submenu.cpp:181-185`) only forwards on the HORIZONTAL rendering style
	}
	const Node* column = selected_child(ctx, st, *node);
	return (column != nullptr && column->kind != Kind::Submenu) ? column : nullptr;
}

const CustomItem* focused_custom(const MenuState& st) {
	const Node* node = focused(st);
	// A `Kind::Custom` node is the only kind that can carry a learn hook, and `Node::custom` is only
	// ever read for one (`render_oled`, `enter_leaf`, `handle` all guard the same way).
	return node != nullptr && node->kind == Kind::Custom ? node->custom : nullptr;
}

bool uses_affect_entire(const MenuContext& ctx, const MenuState& st) {
	const Node* node = focused(st);
	if (node == nullptr) {
		return false;
	}
	if (node->kind != Kind::Submenu) {
		return node_uses_affect_entire(*node);
	}
	const Node* child = selected_child(ctx, st, *node);
	if (child == nullptr) {
		return false;
	}
	if (renders_horizontal(*node, ctx)) {
		return node_uses_affect_entire(*child);
	}
	// `!shouldEnterSubmenu()`: a Toggle, or a toggle choice that flips on its row (`flips_in_place`).
	return flips_in_place(*child) && node_uses_affect_entire(*child);
}

} // namespace deluge::scenes::menu
