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
});
// clang-format on

DELUGE_SPEC(scenes_node_id_spec, scenes_node_id)
