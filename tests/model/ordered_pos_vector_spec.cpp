#include "deluge-project/ordered_pos_vector.h"
#include "deluge-project/param_node.h"
#include "model_hooks_for_tests.h"

#include <cppspec.hpp>
#include <utility>
#include <vector>

using deluge::project::AllocError;
using deluge::project::OrderedPosVector;
using deluge::project::ParamNode;
namespace test = deluge::project::test;

namespace {
std::vector<int32_t> keys_of(const OrderedPosVector<ParamNode>& nodes) {
	std::vector<int32_t> keys;
	for (const ParamNode& n : nodes) {
		keys.push_back(n.pos);
	}
	return keys;
}
} // namespace

// clang-format off
describe ordered_pos_vector("the model's sorted container", ${
	before_each([] { test::counts = {}; });

	it("keeps elements sorted by key", _ {
		OrderedPosVector<ParamNode> nodes;
		for (int32_t pos : {40, 10, 30, 20}) {
			(void)nodes.insertSorted(pos);
		}
		expect(keys_of(nodes)).to_equal(std::vector<int32_t>{10, 20, 30, 40});
	});

	it("allocates and frees through the host's hooks", _ {
		{
			OrderedPosVector<ParamNode> nodes;
			(void)nodes.insertSorted(1);
			(void)nodes.reserveExtra(16);
		}
		expect(test::counts.allocations > 0).to_equal(true);
		expect(test::counts.frees).to_equal(test::counts.allocations);
	});

	it("brackets every structural edit with the host's guard, and makes none outside it", _ {
		OrderedPosVector<ParamNode> nodes;
		(void)nodes.insertSorted(5);
		(void)nodes.insertSorted(9);
		(void)nodes.insertSorted(20);
		(void)nodes.reserveExtra(8);
		nodes.erase(nodes.begin());
		nodes.repositionElement(0, 0);
		nodes.deleteAtKey(9);
		OrderedPosVector<ParamNode> other;
		other = nodes;
		other = std::move(nodes);
		nodes.swap(other);
		nodes.shiftHorizontal(1, 96);
		(void)nodes.generateRepeats(96, 192);
		nodes.clear();
		nodes.init();
		expect(test::counts.edits).to_equal(14);
		expect(test::counts.enters).to_equal(test::counts.edits);
		expect(test::counts.exits).to_equal(test::counts.enters);
		expect(test::counts.unguarded_edits).to_equal(0);
		expect(test::counts.depth).to_equal(0);
	});

	it("reports exhausted memory as an AllocError, leaving the contents as they were", _ {
		OrderedPosVector<ParamNode> nodes;
		(void)nodes.insertSorted(3);
		test::counts.fail_next_allocation = true;
		auto r = nodes.reserveExtra(1000);
		expect(r.has_value()).to_equal(false);
		expect(r.error() == AllocError::OUT_OF_MEMORY).to_equal(true);
		expect(keys_of(nodes)).to_equal(std::vector<int32_t>{3});
		expect(test::counts.depth).to_equal(0);
	});
});

CPPSPEC_SPEC(ordered_pos_vector)
