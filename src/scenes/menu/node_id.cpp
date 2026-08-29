#include "scenes/menu/node_id.h"

#include <cstdint>

namespace deluge::scenes::menu {

namespace {

/// @brief The registry's own hash table: open-addressed, `kMaxNodes * 2` slots hashed on the
///        node's own pointer, linear-probed on collision.
///
/// The brief allows either a linear-probe scan or an open-addressed table; this is the latter --
/// `kMaxNodes` is 1024, so a linear scan over every assigned entry to answer `node_id` would be
/// up to 1024 pointer comparisons per lookup, versus O(1) expected here with a load factor capped
/// at 50%. The table is rebuilt from empty on every `assign_node_ids` call (never incrementally
/// updated), so an empty slot encountered while probing is a genuine "not present" -- no tombstone
/// bookkeeping is needed.
constexpr size_t kTableSlots = kMaxNodes * 2;

struct Slot {
	const Node* node = nullptr;
	uint16_t id = 0;
};

Slot g_table[kTableSlots];
/// @brief `g_by_id[id - 1]` is the node `assign_node_ids` gave `id`, for `id` in `[1, kMaxNodes]`.
const Node* g_by_id[kMaxNodes];

/// @brief A 64-bit avalanche mix (splitmix64's finalizer) on the node's address, folded into a
///        `kTableSlots`-wide index. `kTableSlots` is a power of two, so a mask stands in for `%`.
size_t hash_slot(const Node* n) {
	auto v = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(n));
	v ^= v >> 33;
	v *= 0xff51afd7ed558ccdULL;
	v ^= v >> 33;
	v *= 0xc4ceb9fe1a85ec53ULL;
	v ^= v >> 33;
	return static_cast<size_t>(v) & (kTableSlots - 1);
}

/// @brief Finds `n`'s existing slot, or (if absent) the first empty slot it would be inserted
///        into. Returns `kTableSlots` only if the table is somehow completely full, which cannot
///        happen in practice: at most `kMaxNodes` entries are ever live against `kTableSlots ==
///        kMaxNodes * 2` slots, a 50% load factor.
size_t probe_slot(const Node* n) {
	size_t idx = hash_slot(n);
	for (size_t probes = 0; probes < kTableSlots; probes++) {
		if (g_table[idx].node == nullptr || g_table[idx].node == n) {
			return idx;
		}
		idx = (idx + 1) & (kTableSlots - 1);
	}
	return kTableSlots;
}

/// @brief Depth-first pre-order walk: `n` gets the next id before any of its children do, and
///        children are visited in `Node::children`'s own declaration order.
void visit(const Node& n, uint16_t& next_id) {
	if (next_id > kMaxNodes) {
		return;
	}
	size_t slot = probe_slot(&n);
	if (slot == kTableSlots) {
		return; // table full -- cannot happen, see probe_slot's note; fail closed rather than UB.
	}
	if (g_table[slot].node == &n) {
		return; // already assigned this call (a node shared by more than one parent/root).
	}
	uint16_t id = next_id++;
	g_table[slot] = Slot{&n, id};
	g_by_id[id - 1] = &n;
	for (const Node* child : n.children) {
		if (child == nullptr) {
			continue;
		}
		visit(*child, next_id);
		if (next_id > kMaxNodes) {
			return;
		}
	}
}

} // namespace

uint16_t assign_node_ids(std::span<const Node* const> roots) {
	for (auto& slot : g_table) {
		slot = Slot{};
	}
	for (auto& entry : g_by_id) {
		entry = nullptr;
	}

	uint16_t next_id = 1;
	for (const Node* root : roots) {
		if (root == nullptr) {
			continue;
		}
		if (next_id > kMaxNodes) {
			break;
		}
		visit(*root, next_id);
	}
	return static_cast<uint16_t>(next_id - 1);
}

uint16_t node_id(const Node& n) {
	size_t slot = probe_slot(&n);
	if (slot == kTableSlots || g_table[slot].node != &n) {
		return 0;
	}
	return g_table[slot].id;
}

const Node* node_by_id(uint16_t id) {
	if (id == 0 || id > kMaxNodes) {
		return nullptr;
	}
	return g_by_id[id - 1];
}

} // namespace deluge::scenes::menu
