#include "modulation/params/param.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "ui/oled/meters.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"

using namespace deluge::scenes::menu;
namespace params = deluge::modulation::params;

// clang-format off

// A three-node fake tree: root -> {child_a, child_b}, in declaration order.
constexpr Node node_id_spec_child_a = param(101, ParamRef{params::Kind::PATCHED, 1}, Hint::Knob);
constexpr Node node_id_spec_child_b = param(102, ParamRef{params::Kind::PATCHED, 2}, Hint::Knob);
constexpr const Node* node_id_spec_kids[] = {&node_id_spec_child_a, &node_id_spec_child_b};
constexpr Node node_id_spec_root = submenu(100, node_id_spec_kids);
constexpr const Node* node_id_spec_roots[] = {&node_id_spec_root};

// A node that is never passed to assign_node_ids -- stays unassigned throughout.
constexpr Node node_id_spec_stray = param(999, ParamRef{params::Kind::PATCHED, 9}, Hint::Knob);

// The chain-target walk. `chain_only` is named by NO children span -- the shape legacy's
// `arpOctaveModeToNoteModeMenu` and `cvSubmenu` have -- so the only edge that reaches it is
// `chain_root`'s `chain_targets`. The hook is never called by this walk (a walk cannot supply a
// live MenuContext), which is exactly why the static span has to exist beside it.
constexpr Node node_id_spec_chain_only = param(201, ParamRef{params::Kind::PATCHED, 3}, Hint::Knob);
constexpr const Node* node_id_spec_chain_targets[] = {&node_id_spec_chain_only};
const Node* node_id_spec_chain_fn(const MenuContext&, const MenuState&) {
	return &node_id_spec_chain_only;
}
constexpr Node node_id_spec_chain_leaf = with_select_chain(param(200, ParamRef{params::Kind::PATCHED, 4}, Hint::Knob),
                                                           node_id_spec_chain_fn, node_id_spec_chain_targets);
constexpr const Node* node_id_spec_chain_kids[] = {&node_id_spec_chain_leaf};
constexpr Node node_id_spec_chain_root = submenu(202, node_id_spec_chain_kids);
constexpr const Node* node_id_spec_chain_roots[] = {&node_id_spec_chain_root};

describe scenes_node_id("scenes::menu node-id registry", $ {
	it("assigns every reachable node an id, depth-first in declaration order", _ {
		uint16_t assigned = assign_node_ids(node_id_spec_roots);
		expect(assigned == uint16_t(3)).to_be_true();
		expect(node_id(node_id_spec_root) == uint16_t(1)).to_be_true();
		expect(node_id(node_id_spec_child_a) == uint16_t(2)).to_be_true();
		expect(node_id(node_id_spec_child_b) == uint16_t(3)).to_be_true();
	});
	it("node_by_id(node_id(x)) == &x for every assigned node", _ {
		assign_node_ids(node_id_spec_roots);
		expect(node_by_id(node_id(node_id_spec_root)) == &node_id_spec_root).to_be_true();
		expect(node_by_id(node_id(node_id_spec_child_a)) == &node_id_spec_child_a).to_be_true();
		expect(node_by_id(node_id(node_id_spec_child_b)) == &node_id_spec_child_b).to_be_true();
	});
	it("an unassigned node has id 0, and id 0 maps to nullptr", _ {
		assign_node_ids(node_id_spec_roots);
		expect(node_id(node_id_spec_stray) == uint16_t(0)).to_be_true();
		expect(node_by_id(0) == nullptr).to_be_true();
	});
	it("a second call over the same roots yields identical ids", _ {
		assign_node_ids(node_id_spec_roots);
		uint16_t root_id_1 = node_id(node_id_spec_root);
		uint16_t a_id_1 = node_id(node_id_spec_child_a);
		uint16_t b_id_1 = node_id(node_id_spec_child_b);

		assign_node_ids(node_id_spec_roots);
		expect(node_id(node_id_spec_root) == root_id_1).to_be_true();
		expect(node_id(node_id_spec_child_a) == a_id_1).to_be_true();
		expect(node_id(node_id_spec_child_b) == b_id_1).to_be_true();
	});
	it("stops at kMaxNodes on an oversize tree and reports the count assigned, not the true reachable count", _ {
		// A flat chain of kMaxNodes + 2 nodes (chain[i]'s only child is chain[i + 1]) -- no real
		// menu structure needed, just enough nodes that the reachable count exceeds the registry.
		static Node chain[kMaxNodes + 2]{};
		static const Node* kid[kMaxNodes + 2][1]{};
		for (size_t i = 0; i < kMaxNodes + 2; i++) {
			chain[i] = param(uint16_t(3000 + i), ParamRef{params::Kind::PATCHED, 0}, Hint::Knob);
		}
		for (size_t i = 0; i + 1 < kMaxNodes + 2; i++) {
			kid[i][0] = &chain[i + 1];
			chain[i].children = kid[i];
		}
		const Node* roots[] = {&chain[0]};

		uint16_t assigned = assign_node_ids(roots);
		expect(assigned == uint16_t(kMaxNodes)).to_be_true();
		expect(node_id(chain[kMaxNodes + 1]) == uint16_t(0)).to_be_true();
		expect(node_by_id(uint16_t(kMaxNodes + 1)) == nullptr).to_be_true();
	});
	it("numbers a chain target no children span names, after the static subtree", _ {
		uint16_t assigned = assign_node_ids(node_id_spec_chain_roots);
		expect(assigned == uint16_t(3)).to_be_true();
		expect(node_id(node_id_spec_chain_root) == uint16_t(1)).to_be_true();
		expect(node_id(node_id_spec_chain_leaf) == uint16_t(2)).to_be_true();
		// The point of the whole mechanism: without the `chain_targets` walk this is 0, and
		// `SceneId::arg` could never address the page.
		expect(node_id(node_id_spec_chain_only) == uint16_t(3)).to_be_true();
		expect(node_by_id(3) == &node_id_spec_chain_only).to_be_true();
	});
	it("terminates on a chain cycle instead of recursing forever", _ {
		// `chain_targets` can loop where `children` cannot: two leaves may name each other. Nothing
		// in the shipped tree does, so this is the only place the walk's cycle handling is
		// exercised at all. Built at runtime -- a `constexpr` pair cannot refer to each other.
		static Node a = param(300, ParamRef{params::Kind::PATCHED, 5}, Hint::Knob);
		static Node b = param(301, ParamRef{params::Kind::PATCHED, 6}, Hint::Knob);
		static const Node* a_targets[] = {&b};
		static const Node* b_targets[] = {&a};
		a.select_chain = node_id_spec_chain_fn;
		a.chain_targets = a_targets;
		b.select_chain = node_id_spec_chain_fn;
		b.chain_targets = b_targets;
		const Node* roots[] = {&a};

		// Returns at all -- the assertion the count carries is secondary to not hanging.
		uint16_t assigned = assign_node_ids(roots);
		expect(assigned == uint16_t(2)).to_be_true();
		expect(node_id(a) == uint16_t(1)).to_be_true();
		expect(node_id(b) == uint16_t(2)).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(scenes_node_id_spec, scenes_node_id)
