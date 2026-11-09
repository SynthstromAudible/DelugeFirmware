#include "deluge-project/lane_view.h"
#include "deluge-project/param_edit.h"
#include "model_hooks_for_tests.h"

#include <cppspec.hpp>
#include <cstdint>
#include <random>
#include <vector>

using deluge::project::DocAutoParam;
using deluge::project::getValueAtPos;
using deluge::project::LaneView;
using deluge::project::ParamNode;

namespace {
/// A lane with nodes at @p positions, each node's value its position times 1000, interpolated or stepped.
DocAutoParam lane_with(std::initializer_list<int32_t> positions, bool interpolated) {
	DocAutoParam lane;
	lane.current_value = -7;
	for (int32_t pos : positions) {
		int32_t i = lane.nodes.insertSorted(pos).value_or(-1);
		lane.nodes[i].value = pos * 1000;
		lane.nodes[i].interpolated = interpolated;
	}
	return lane;
}
} // namespace

// clang-format off
describe lane_view("a lane read through a view", ${
	it("plays its base when it has no nodes", _ {
		DocAutoParam lane;
		lane.current_value = 42;
		expect(getValueAtPos(LaneView::of(lane), 10, 96)).to_equal(42);
	});

	it("holds the left node's value between stepped nodes", _ {
		DocAutoParam lane = lane_with({0, 24, 48}, false);
		expect(getValueAtPos(LaneView::of(lane), 30, 96)).to_equal(24000);
		expect(getValueAtPos(LaneView::of(lane), 60, 96)).to_equal(48000);
	});

	it("is the node's value on a node", _ {
		DocAutoParam lane = lane_with({0, 24, 48}, true);
		expect(getValueAtPos(LaneView::of(lane), 24, 96)).to_equal(24000);
	});

	it("interpolates towards an interpolated node", _ {
		DocAutoParam lane = lane_with({0, 40}, true);
		// a quarter of the way from 0 to 40
		expect(getValueAtPos(LaneView::of(lane), 10, 96)).to_equal(10000);
	});

	it("wraps past the last node to the first", _ {
		DocAutoParam lane = lane_with({8, 40}, false);
		// before the first node the left neighbour is the last one, across the loop point
		expect(getValueAtPos(LaneView::of(lane), 4, 96)).to_equal(40000);
	});

	it("finds the first node at or after a position as the model's container does", _ {
		std::mt19937 random(5);
		for (int32_t round = 0; round < 200; round++) {
			DocAutoParam lane;
			const auto count = static_cast<int32_t>(random() % 12);
			for (int32_t n = 0; n < count; n++) {
				(void)lane.nodes.insertSorted(static_cast<int32_t>(random() % 192));
			}
			const LaneView view = LaneView::of(lane);
			for (int32_t key = -1; key <= 193; key++) {
				expect(view.first_at_or_after(key)).to_equal(lane.nodes.firstAtOrAfter(key));
			}
		}
	});
});

CPPSPEC_SPEC(lane_view)
