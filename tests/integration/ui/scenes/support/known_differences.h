#pragma once

/// @file
/// @brief Where a node's own answer differs from what upstream `main`'s menu item answers, pinned by identity and by
///        count so a difference that is fixed, or that grows, shows.
///
/// The reference rows say what `main` answers, except the rows `tests/golden/README.md` names as differing on purpose;
/// these are the questions the port answers differently today. A spec that
/// compares an answer to its row excuses exactly the nodes these predicates name, requires the excused difference to be
/// there (the port's answer is the one stated here, the row is main's), and requires the number of excused nodes to be
/// the count below. Fixing the port, or breaking another node the same way, turns the example red.

#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/nodes.h"

#include <cstddef>

namespace deluge::ui::scenes::spec {

/// @name The shortcut layouts' excusals
/// Where a shortcut cell's port node cannot be compared with main's reference row by plain equality, pinned by identity
/// and by count so a fix, or a new excused cell, turns the example red.
/// @{

/// @brief Nodes whose port name is a placeholder the menu substitutes an index into at render time (`*` stands for the
///        digit: the two oscillator levels, the two sample recorders and the two FM modulator levels), where main's
///        reference row holds the resolved name. The cell is compared by presence of both names.
inline bool shortcut_name_is_placeholder(const menu::Node& node) {
	namespace nodes = deluge::ui::scenes::menu::nodes;
	return &node == &nodes::source0_volume || &node == &nodes::source1_volume || &node == &nodes::sample0_recorder
	       || &node == &nodes::sample1_recorder || &node == &nodes::modulator0_volume
	       || &node == &nodes::modulator1_volume;
}

/// @brief How many shortcut cells hold one of those nodes, over the seven reference tables (the song table once).
inline constexpr size_t kShortcutPlaceholderNameCells = 6;

/// @brief How many shortcut cells hold a node `tables3::has_dynamic_name` names (the filter morph items), whose name
///        main resolves from the live filter mode.
inline constexpr size_t kShortcutDynamicNameCells = 28;

/// @brief How many sound-root entries have no param pad, so plan no source blink.
inline constexpr size_t kPadlessShortcutNodes = 109;

/// @brief Landing cells whose page the port shares with another root's tree (main has one page per root family), so the
///        landing rows name the page by its paths in the landing root's own tree.
inline constexpr size_t kLandingSharedPageCells = 158;

/// @brief Landing cells whose page is in no path of the landing root's own tree, named `outside:<paths>` in the rows:
///        the song and performance roots' pad (7, 5).
inline constexpr size_t kLandingOutsideRootCells = 2;

/// @brief Landing cells whose page is in no tree, so the rows name it: the sample recorder page.
inline constexpr size_t kLandingUnlistedPageCells = 4;

/// @}

} // namespace deluge::ui::scenes::spec
