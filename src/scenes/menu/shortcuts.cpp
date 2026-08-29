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
/// @brief **Stub body (task-5 ruling 1).** See `shortcuts.h` for what this is and what replaces
///        it.
///
/// Task 5 needs `shortcut_node` to *link*, because `MenuScene::handle` calls it; it cannot need
/// the real tables, because those are built out of Tasks 6-8's nodes. So the whole of this file
/// is one installable table plus three lookups over it, and every `MenuContext::Root` resolves
/// through the same one -- there is nothing here for a root to select between yet.
///
/// **Task 9 replaces everything below** with the seven real tables
/// (`gui/ui/sound_editor.cpp:1111-1250`), deletes `set_shortcut_table_for_test` and its
/// declaration in the header, and re-asserts Task 5's own spec cases against the real data.

#include "scenes/menu/shortcuts.h"

#include "scenes/menu/node.h"

namespace deluge::scenes::menu {

namespace {

/// The one installed table. A span over caller-owned storage; the only writer is the test hook.
std::span<const Node* const> g_table{};
uint8_t g_cols = 0;
uint8_t g_rows = 0;

const Node* cell_at(uint8_t x, uint8_t y) {
	if (g_cols == 0 || g_rows == 0 || x >= g_cols || y >= g_rows) {
		return nullptr;
	}
	const size_t i = static_cast<size_t>(y) * g_cols + x;
	return i < g_table.size() ? g_table[i] : nullptr;
}

} // namespace

const Node* shortcut_node(MenuContext::Root /*root*/, bool /*shift*/, uint8_t x, uint8_t y) {
	// `root` and `shift` are ignored while the table is a single installed one: Task 9's tables
	// are per-root, and `shift` is what picks between `ShortcutTable::layer0` and `layer1`
	// (`shell/profile.h:87`). Both parameters are already in the signature so that adding the
	// tables is a change to this file alone.
	return cell_at(x, y);
}

ShortcutPos shortcut_pos(MenuContext::Root /*root*/, const Node& node) {
	for (uint8_t y = 0; y < g_rows; y++) {
		for (uint8_t x = 0; x < g_cols; x++) {
			if (cell_at(x, y) == &node) {
				return ShortcutPos{x, y, true};
			}
		}
	}
	return ShortcutPos{};
}

std::span<const ShortcutPos> source_shortcut_pads(MenuContext::Root /*root*/, const Node& /*node*/) {
	return {};
}

void set_shortcut_table_for_test(std::span<const Node* const> table, uint8_t cols, uint8_t rows) {
	g_table = table;
	g_cols = cols;
	g_rows = rows;
}

} // namespace deluge::scenes::menu
