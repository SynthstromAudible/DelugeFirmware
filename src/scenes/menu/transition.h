#pragma once

/// @file
/// @brief The menu's record of one change of the value node it edits, and what caused it, for the host to act on.
///
/// Legacy's sound editor tells the automation view about each such change as it happens
/// (`SoundEditor::handlePotentialParamMenuChange`, and the shortcut pads' and CLIP's own `buttonAction` calls to the
/// item); what the view does depends on the cause -- a shortcut retargets even onto the same param, a column move only
/// when the column changed. `MenuScene` files one record per tick and the host drains it after the tick, as it drains
/// the `MenuAction` outbox.

#include <cstdint>

namespace deluge::scenes::menu {

struct Node;

/// @brief What moved the menu.
enum class TransitionCause : uint8_t {
	Select,       ///< A select press's release (legacy's SELECT arm).
	Back,         ///< A BACK press that went up a level.
	ColumnButton, ///< A horizontal page's instrument, CROSS SCREEN or SCALE button moving its column.
	Shortcut,     ///< A shortcut pad's jump.
};

/// @brief One change of the menu's value node (`MenuScene::focused_value`): the node before the event, the node after
///        it (null when the menu is not on a value), and the cause.
struct MenuTransition {
	const Node* from = nullptr;
	const Node* to = nullptr;
	TransitionCause cause = TransitionCause::Select;
};

} // namespace deluge::scenes::menu
