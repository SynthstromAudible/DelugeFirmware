// appendParam appends a repeat of a lane's nodes, and with no memory for them leaves the destination as it was.

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

DocAutoParam laneAt(std::initializer_list<int32_t> positions) {
	DocAutoParam p;
	int32_t value = 1;
	for (int32_t pos : positions) {
		int32_t i = p.nodes.insertSorted(pos).value_or(-1);
		p.nodes[i].value = value++;
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
describe append_param("appendParam", ${
	it("appends the source's nodes a repeat later", _ {
		DocAutoParam dest = laneAt({0, 48});
		const DocAutoParam src = laneAt({0, 24});
		expect(appendParam(dest, src, kLoop, 0, false)).to_have_value();
		std::vector<int32_t> positions;
		for (const ParamNode& node : dest.nodes) {
			positions.push_back(node.pos);
		}
		expect(positions).to_equal(std::vector<int32_t>{0, 48, 96, 120});
	});

	it("reports exhausted memory and leaves the destination exactly as it was", _ {
		DocAutoParam dest = laneAt({0, 48});
		const DocAutoParam src = laneAt({0, 24, 72});
		const auto before = posAndValue(dest);
		test::counts.fail_next_allocation = true;
		auto r = appendParam(dest, src, kLoop, 0, false);
		expect(r).not_().to_have_value();
		expect(r.error()).to_equal(AllocError::OUT_OF_MEMORY);
		expect(posAndValue(dest)).to_equal(before);
		expect(test::counts.depth).to_equal(0);
	});

	it("allocates nothing when the caller reserved appendParamRoom()", _ {
		DocAutoParam dest = laneAt({0});
		const DocAutoParam src = laneAt({0, 24, 72});
		expect(dest.nodes.reserveExtra(static_cast<int32_t>(appendParamRoom(src.nodes.size())))).to_have_value();
		test::counts.fail_next_allocation = true; // would fail if it allocated
		expect(appendParam(dest, src, kLoop, 0, true)).to_have_value();
		test::counts.fail_next_allocation = false;
		expect(dest.nodes.size()).to_equal(4u);
	});
});

CPPSPEC_SPEC(append_param)
