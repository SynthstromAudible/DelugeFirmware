#pragma once

/// @file
/// @brief What a menu unit is handed for one call -- the scene's context and state -- and the read-only tree queries
///        the units share.

#include "scenes/menu/context.h"
#include "scenes/menu/menu_state.h"
#include "scenes/menu/node.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::scenes::menu {

/// @brief The scene's `MenuContext` and `MenuState` for one call into a unit that writes them. The scene owns both;
///        a unit keeps neither.
struct MenuCore {
	MenuContext& ctx;
	MenuState& st;
};

/// @brief The node at the top of @p st's path, or `nullptr` past the deepest level.
const Node* focused(const MenuState& st);

/// @brief Fill @p out with @p node's children that `relevant` admits, resolving `dynamic_children` first.
/// @param ctx  The menu context the relevance is read in.
/// @param node The submenu.
/// @param out  Storage for the children.
/// @return How many were written.
size_t visible_children(const MenuContext& ctx, const Node& node, std::span<const Node*> out);

/// @brief @p node's @p index'th visible child, or `nullptr`.
const Node* child_at(const MenuContext& ctx, const Node& node, int32_t index);

/// @brief The child the current layout has focus on at @p st's depth, or `nullptr`.
const Node* selected_child(const MenuContext& ctx, const MenuState& st, const Node& node);

/// @brief The node the encoders edit: a focused leaf, a horizontal page's focused value column, else `nullptr`.
const Node* focused_value(const MenuContext& ctx, const MenuState& st);

/// @brief `getCurrentMenuItem()->usesAffectEntire()`: a leaf's own answer (`node_uses_affect_entire`); on a horizontal
///        page the focused column's; on a vertical list the cursor row's when that row does not enter
///        (`flips_in_place`), else false (`Submenu::usesAffectEntire`).
bool uses_affect_entire(const MenuContext& ctx, const MenuState& st);

/// @brief The focused node's `CustomItem`, or `nullptr` when it is not a `Kind::Custom` leaf.
const CustomItem* focused_custom(const MenuState& st);

} // namespace deluge::scenes::menu
