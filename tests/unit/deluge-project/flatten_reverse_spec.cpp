// flattenReverse mirrors a lane's nodes around its loop, and on exhausted memory leaves them exactly as they were.

#include "deluge-project/param_automation.h"
#include "deluge-project/param_edit.h"
#include "model_hooks_for_tests.h"

#include <cppspec.hpp>
#include <cstdint>
#include <utility>
#include <vector>

using namespace deluge::project;
namespace test = deluge::project::test;

namespace {
constexpr int32_t kLoop = 96;

/// Nodes at 0, 30 and 60, values 1, 2 and 3, the one at 30 interpolated.
DocAutoParam lane() {
	DocAutoParam p;
	int32_t value = 1;
	for (int32_t pos : {0, 30, 60}) {
		int32_t i = p.nodes.insertSorted(pos).value_or(-1);
		p.nodes[i].value = value++;
		p.nodes[i].interpolated = pos == 30;
	}
	return p;
}

std::vector<std::pair<int32_t, int32_t>> posAndValue(const DocAutoParam& p) {
	std::vector<std::pair<int32_t, int32_t>> out;
	for (const ParamNode& node : p.nodes) {
		out.emplace_back(node.pos, node.value);
	}
	return out;
}
} // namespace

// clang-format off
describe flatten_reverse("flattenReverse", ${
	it("mirrors the nodes' positions around the loop", _ {
		DocAutoParam p = lane();
		expect(flattenReverse(p, kLoop)).to_have_value();
		std::vector<int32_t> positions;
		for (const ParamNode& node : p.nodes) {
			positions.push_back(node.pos);
		}
		expect(positions).to_equal(std::vector<int32_t>{0, 36, 66});
	});

	it("reports exhausted memory and leaves the nodes exactly as they were", _ {
		DocAutoParam p = lane();
		const auto before = posAndValue(p);
		test::counts.fail_next_allocation = true;
		auto r = flattenReverse(p, kLoop);
		expect(r).not_().to_have_value();
		expect(r.error()).to_equal(AllocError::OUT_OF_MEMORY);
		expect(posAndValue(p)).to_equal(before);
		expect(test::counts.depth).to_equal(0);
	});

	it("does nothing to a lane with no nodes, or for a zero length", _ {
		DocAutoParam empty;
		test::counts.fail_next_allocation = true; // would fail if it allocated
		expect(flattenReverse(empty, kLoop)).to_have_value();
		test::counts.fail_next_allocation = false;
		DocAutoParam p = lane();
		expect(flattenReverse(p, 0)).to_have_value();
		expect(posAndValue(p)).to_equal(posAndValue(lane()));
	});
});

CPPSPEC_SPEC(flatten_reverse)
