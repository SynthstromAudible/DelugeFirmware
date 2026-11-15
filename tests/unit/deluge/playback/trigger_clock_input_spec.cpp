#include "playback/trigger_clock_input.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

using deluge::playback::TriggerClockEdges;

// clang-format off
describe trigger_clock_edges_group("trigger clock edges", ${
	it("an empty ring pops nothing", _ {
		TriggerClockEdges edges;
		expect(edges.pop().has_value()).to_be_false();
	});

	it("edges pop in arrival order", _ {
		TriggerClockEdges edges;
		edges.push(10);
		edges.push(20);
		expect(edges.pop().value()).to_equal(10u);
		expect(edges.pop().value()).to_equal(20u);
		expect(edges.pop().has_value()).to_be_false();
	});

	it("two pushes before a pop both arrive", _ {
		TriggerClockEdges edges;
		edges.push(7);
		edges.push(7);
		expect(edges.pop().value()).to_equal(7u);
		expect(edges.pop().value()).to_equal(7u);
		expect(edges.dropped()).to_equal(0u);
	});

	it("a full ring drops the newest and counts it", _ {
		TriggerClockEdges edges;
		for (uint32_t i = 0; i < TriggerClockEdges::kCapacity + 2; i++) {
			edges.push(i);
		}
		expect(edges.dropped()).to_equal(2u);
		for (uint32_t i = 0; i < TriggerClockEdges::kCapacity; i++) {
			expect(edges.pop().value()).to_equal(i);
		}
		expect(edges.pop().has_value()).to_be_false();
	});

	it("the ring keeps working past index wrap", _ {
		TriggerClockEdges edges;
		for (uint32_t i = 0; i < 1000; i++) {
			edges.push(i);
			expect(edges.pop().value()).to_equal(i);
		}
	});
});

DELUGE_SPEC(trigger_clock_input_spec, trigger_clock_edges_group)
