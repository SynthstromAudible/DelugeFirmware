#include "deluge-project/backed_params.h"
#include "deluge-project/clip_params.h"
#include "deluge-project/param_automation.h"
#include "deluge-project/param_edit.h"
#include "model_hooks_for_tests.h"

#include <cppspec.hpp>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace deluge::project;
namespace test = deluge::project::test;

namespace {
constexpr int32_t kLoop = 96;

/// Four nodes over a 96-tick loop, the middle two interpolated.
DocAutoParam lane() {
	DocAutoParam p;
	p.current_value = 7;
	int32_t value = 1 << 24;
	for (int32_t pos : {0, 24, 48, 72}) {
		int32_t i = p.nodes.insertSorted(pos).value_or(-1);
		p.nodes[i].value = value;
		p.nodes[i].interpolated = pos == 24 || pos == 48;
		value += 1 << 26;
	}
	return p;
}

/// Every node-editing kernel in param_edit.h, each run on a fresh lane.
std::vector<std::pair<std::string, std::function<void(DocAutoParam&)>>> kernels() {
	return {
	    {"setNodeAtPos", [](DocAutoParam& p) { (void)setNodeAtPos(p, 30, 5, true); }},
	    {"insertTime", [](DocAutoParam& p) { insertTime(p, 30, 12); }},
	    {"shiftHorizontally", [](DocAutoParam& p) { shiftHorizontally(p, 10, kLoop); }},
	    {"shiftValues", [](DocAutoParam& p) { shiftValues(p, 99); }},
	    {"generateRepeats", [](DocAutoParam& p) { generateRepeats(p, kLoop, kLoop * 2, false); }},
	    {"homogenizeRegion",
	     [](DocAutoParam& p) { (void)homogenizeRegion(p, 20, 30, 5, false, false, kLoop, false, false); }},
	    {"deleteTime", [](DocAutoParam& p) { deleteTime(p, 20, 30, kLoop); }},
	    {"moveRegionHorizontally", [](DocAutoParam& p) { moveRegionHorizontally(p, 20, 10, 3, kLoop); }},
	    {"deleteNodesWithinRegion", [](DocAutoParam& p) { deleteNodesWithinRegion(p, 20, 30, kLoop); }},
	    {"transposeCCValuesToChannelPressure", [](DocAutoParam& p) { transposeCCValuesToChannelPressure(p); }},
	    {"flattenReverse", [](DocAutoParam& p) { flattenReverse(p, kLoop); }},
	    {"trimToLength", [](DocAutoParam& p) { trimToLength(p, 50, kLoop); }},
	    {"appendParam", [](DocAutoParam& p) { appendParam(p, lane(), kLoop, 0, false); }},
	    {"nudgeNonInterpolatingNodesAtPos", [](DocAutoParam& p) { nudgeNonInterpolatingNodesAtPos(p, 72, 2, kLoop); }},
	    {"setValueForRegion",
	     [](DocAutoParam& p) { (void)setValueForRegion(p, 20, 30, 5, kLoop, false, false, false, true, 10, false); }},
	    {"pasteRegion",
	     [](DocAutoParam& p) {
		     CopiedAutomation copied;
		     copyRegion(lane(), 0, 48, kLoop, false, copied);
		     pasteRegion(p, 48, 96, 1.0f, kLoop, false, copied);
	     }},
	    {"stealNodes", [](DocAutoParam& p) { stealNodes(p, 20, 30, kLoop); }},
	    {"insertStolenNodes",
	     [](DocAutoParam& p) {
		     StolenNodes stolen{ParamNode{.pos = 5, .value = 3, .interpolated = false}};
		     insertStolenNodes(p, 20, 30, kLoop, stolen);
	     }},
	    {"makeInterpolationGoodAgain", [](DocAutoParam& p) { (void)makeInterpolationGoodAgain(p, kLoop, 4); }},
	    {"recordLiveValue",
	     [](DocAutoParam& p) { (void)recordLiveValue(p, 30, 5, 6, false, true, kLoop, false, false); }},
	};
}
} // namespace

// clang-format off
describe param_edit_section("the automation edit kernels, against the render", ${
	it("runs each kernel inside one guard, so every node op it makes is inside a compound edit", _ {
		for (auto& [name, kernel] : kernels()) {
			DocAutoParam p = lane();
			test::counts = {};
			kernel(p);
			expect(test::counts.kernels).to_be_greater_than(0, name + " made no compound edit");
			expect(test::counts.lone_node_edits).to_equal(0, name + " made a node op outside its compound edit");
			expect(test::counts.unguarded_edits).to_equal(0, name);
			expect(test::counts.depth).to_equal(0, name);
		}
	});

	it("inserts a clip's store under a guard, and only when it is new", _ {
		ClipParams stores;
		test::counts = {};
		(void)stores.getOrCreate(3);
		expect(test::counts.store_edits).to_equal(1);
		expect(test::counts.unguarded_edits).to_equal(0);
		test::counts = {};
		(void)stores.getOrCreate(3);
		expect(test::counts.store_edits).to_equal(0);
		stores.remove(3);
		expect(test::counts.store_edits).to_equal(1);
		expect(test::counts.unguarded_edits).to_equal(0);
		expect(stores.find(3)).to_be_null();
	});

	it("inserts and drops an owner's stores under a guard", _ {
		BackedParams stores;
		const OwnerId drum = OwnerId::drum(4);
		test::counts = {};
		(void)stores.getOrCreate(drum, std::nullopt);
		(void)stores.getOrCreate(drum, 9);
		expect(test::counts.store_edits).to_equal(2);
		test::counts = {};
		(void)stores.getOrCreate(drum, 9);
		expect(test::counts.store_edits).to_equal(0);
		stores.remove(drum, 9);
		stores.removeOwner(drum);
		expect(test::counts.store_edits).to_equal(2);
		expect(test::counts.unguarded_edits).to_equal(0);
		expect(stores.find(drum, std::nullopt)).to_be_null();
		expect(test::counts.depth).to_equal(0);
	});
});

CPPSPEC_SPEC(param_edit_section)
