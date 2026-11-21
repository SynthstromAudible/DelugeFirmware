#include "deluge-project/ordered_pos_vector.h"
#include "deluge-project/param_node.h"
#include "model_hooks_for_tests.h"
#include "support/printers/project_result_printer.h"

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
		expect(test::counts.allocations).to_be_greater_than(0);
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

	it("brackets a resize with the host's guard", _ {
		OrderedPosVector<ParamNode> nodes;
		(void)nodes.insertSorted(5);
		test::counts = {};
		nodes.resize(64);
		expect(test::counts.edits).to_equal(1);
		expect(test::counts.unguarded_edits).to_equal(0);
		expect(test::counts.exits).to_equal(test::counts.enters);
	});

	it("reports exhausted memory as an AllocError, leaving the contents as they were", _ {
		OrderedPosVector<ParamNode> nodes;
		(void)nodes.insertSorted(3);
		test::counts.fail_next_allocation = true;
		auto r = nodes.reserveExtra(1000);
		expect(r).not_().to_have_value();
		expect(r.error()).to_equal(AllocError::OUT_OF_MEMORY);
		expect(keys_of(nodes)).to_equal(std::vector<int32_t>{3});
		expect(test::counts.depth).to_equal(0);
	});

	it("tryAssign copies, and on exhausted memory leaves the destination as it was", _ {
		OrderedPosVector<ParamNode> src;
		(void)src.insertSorted(1);
		(void)src.insertSorted(9);
		OrderedPosVector<ParamNode> dst;
		(void)dst.insertSorted(4);
		test::counts.fail_next_allocation = true;
		expect(dst.tryAssign(src)).not_().to_have_value();
		expect(keys_of(dst)).to_equal(std::vector<int32_t>{4});
		expect(dst.tryAssign(src)).to_have_value();
		expect(keys_of(dst)).to_equal(std::vector<int32_t>{1, 9});
		expect(test::counts.depth).to_equal(0);
	});

	it("generateRepeats reports exhausted memory and leaves the elements as they were", _ {
		OrderedPosVector<ParamNode> nodes;
		(void)nodes.insertSorted(0);
		(void)nodes.insertSorted(10);
		test::counts.fail_next_allocation = true;
		expect(nodes.generateRepeats(96, 384)).to_be_false();
		expect(keys_of(nodes)).to_equal(std::vector<int32_t>{0, 10});
	});

	it("generateRepeats tiles the elements before the wrap point out to the end", _ {
		OrderedPosVector<ParamNode> nodes;
		(void)nodes.insertSorted(0);
		(void)nodes.insertSorted(10);
		(void)nodes.insertSorted(100); // at or past the wrap point: dropped
		expect(nodes.generateRepeats(96, 200)).to_be_true();
		expect(keys_of(nodes)).to_equal(std::vector<int32_t>{0, 10, 96, 106, 192});
	});

	it("insertAt reports exhausted memory without touching the elements", _ {
		OrderedPosVector<ParamNode> nodes;
		(void)nodes.insertSorted(5);
		test::counts.fail_next_allocation = true;
		expect(nodes.insertAt(0, 3)).not_().to_have_value();
		expect(keys_of(nodes)).to_equal(std::vector<int32_t>{5});
	});
});

CPPSPEC_SPEC(ordered_pos_vector)
