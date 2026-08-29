#pragma once

/// @brief The stable node-id registry (ruling Q7): `SceneId::arg` is a stable node id, not a
///        pointer -- `MenuScene::on_enter` resolves it through `node_by_id`, `validate_tree`
///        (Task 6) calls `assign_node_ids` before its checks, and `deluge_menu_dump` (Task 10)
///        emits the ids it produces.
#include "scenes/menu/node.h"

#include <cstdint>
#include <span>

namespace deluge::scenes::menu {

/// @brief Walk `roots` depth-first (pre-order: a node before its children, children in
///        declaration order) and give every reachable node an id, 1..n (0 is reserved for "no
///        node"). Idempotent: a second call over the same roots re-derives the same ids, since the
///        walk order is fully determined by the tree's own shape.
///
/// Only *static* children (`Node::children`) are walked -- a `dynamic_children`/`dynamic_names`
/// node has none, so whatever it produces at render time is never reachable here and is never
/// addressable by id (see `node.h`'s note on `Node::children`).
///
/// @param roots The tree's root nodes, in declaration order.
/// @return The number of nodes actually assigned an id. Normally this is every node reachable from
///         `roots`; if that count would exceed `kMaxNodes`, assignment stops as soon as the cap is
///         reached (the registry has no room for more) and this returns `kMaxNodes` -- fewer than
///         the tree's true reachable count, which is the caller's signal that the tree overflowed
///         the registry. This is deliberately not a silent partial success: `validate_tree`
///         (Task 6) is expected to compare this return value against its own full reachable-node
///         walk and fail loudly on a mismatch.
uint16_t assign_node_ids(std::span<const Node* const> roots);

/// @brief The id the last `assign_node_ids` call gave `n`, or 0 if it was never assigned one.
uint16_t node_id(const Node& n);

/// @brief The node with id `id`, or `nullptr` for 0 or an id nothing was ever assigned.
const Node* node_by_id(uint16_t id);

} // namespace deluge::scenes::menu
