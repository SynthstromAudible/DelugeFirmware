/// @file
/// @brief See `fake_shortcuts.h`: the host suite's model-free stand-in for the real shortcut
///        tables.

#include "fake_shortcuts.h"

#include "scenes/menu/node.h"

namespace deluge::scenes::menu {

namespace {

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

std::span<const Node* const> shortcut_layer(MenuContext::Root /*root*/, bool /*second_layer*/) {
	return g_table;
}

const Node* shortcut_node(MenuContext::Root /*root*/, bool /*second_layer*/, uint8_t x, uint8_t y) {
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
