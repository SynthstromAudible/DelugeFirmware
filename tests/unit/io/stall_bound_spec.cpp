// tests/unit/io/stall_bound_spec.cpp
#include "io/stall_bound.h"

#include "cppspec.hpp"

#include <cstdint>

using deluge::io::kChannelStallUs;
using deluge::io::StallBound;
using deluge::runtime::kNever;

namespace {
uint64_t g_now = 0;
uint64_t fakeClock() {
	return g_now;
}
} // namespace

// clang-format off
describe stall_bound("a stall bound gives up on a channel after a span of time with no progress", $ {

	it("defaults to the channel bound", _ {
		expect(StallBound{}.limitUs()).to_equal(kChannelStallUs);
		expect(kChannelStallUs).to_equal(uint64_t{3'600'000});
	});

	it("expires once its limit has passed since the first unproductive tick, not before", _ {
		g_now = 5000;
		StallBound bound{1000, &fakeClock};
		expect(bound.tick()).to_equal(false);
		expect(bound.elapsedUs()).to_equal(uint64_t{0});
		g_now = 5999;
		expect(bound.tick()).to_equal(false);
		expect(bound.expired()).to_equal(false);
		expect(bound.elapsedUs()).to_equal(uint64_t{999});
		g_now = 6000;
		expect(bound.tick()).to_equal(true);
		expect(bound.expired()).to_equal(true);
		g_now = 9000;
		expect(bound.tick()).to_equal(true);
	});

	// expired() is what the last tick found: time passing with no tick does not expire it.
	it("is not expired by time alone, only by a tick", _ {
		g_now = 0;
		StallBound bound{1000, &fakeClock};
		(void)bound.tick();
		g_now = 50'000;
		expect(bound.expired()).to_equal(false);
		expect(bound.tick()).to_equal(true);
	});

	it("a changed progress mark restarts the span", _ {
		g_now = 0;
		StallBound bound{1000, &fakeClock};
		expect(bound.tick(7U)).to_equal(false);
		g_now = 900;
		expect(bound.tick(7U)).to_equal(false);
		g_now = 950;
		expect(bound.tick(8U)).to_equal(false); // progress: the span starts again here
		expect(bound.elapsedUs()).to_equal(uint64_t{0});
		g_now = 1500;
		expect(bound.tick(8U)).to_equal(false); // 550 since the progress, though 1500 since the first tick
		g_now = 1950;
		expect(bound.tick(8U)).to_equal(true);
	});

	it("a still progress mark expires like tick()", _ {
		g_now = 0;
		StallBound bound{1000, &fakeClock};
		expect(bound.tick(3U)).to_equal(false);
		g_now = 1000;
		expect(bound.tick(3U)).to_equal(true);
	});

	// Marks are compared for equality only.
	it("a wrapped mark is still progress", _ {
		g_now = 0;
		StallBound bound{1000, &fakeClock};
		(void)bound.tick(UINT32_MAX);
		g_now = 999;
		(void)bound.tick(0U);
		g_now = 1500;
		expect(bound.tick(0U)).to_equal(false);
	});

	it("the first tick after a reset starts the span whatever the mark", _ {
		g_now = 0;
		StallBound bound{1000, &fakeClock};
		(void)bound.tick(1U);
		bound.reset();
		g_now = 100;
		expect(bound.tick(2U)).to_equal(false);
		g_now = 1100;
		expect(bound.tick(2U)).to_equal(true);
	});

	it("reset starts a new span", _ {
		g_now = 0;
		StallBound bound{1000, &fakeClock};
		(void)bound.tick();
		g_now = 1000;
		expect(bound.tick()).to_equal(true);
		bound.reset();
		expect(bound.expired()).to_equal(false);
		expect(bound.elapsedUs()).to_equal(uint64_t{0});
		g_now = 1500;
		expect(bound.tick()).to_equal(false);
		g_now = 2499;
		expect(bound.tick()).to_equal(false);
		g_now = 2500;
		expect(bound.tick()).to_equal(true);
	});

	// The stall_bound_watchdog check: a caller that runs only when woken asks for a run at this time.
	it("deadlineUs is the first tick's time plus the limit, and kNever before any tick", _ {
		g_now = 40'000;
		StallBound bound{1000, &fakeClock};
		expect(bound.deadlineUs()).to_equal(kNever);
		(void)bound.tick();
		expect(bound.deadlineUs()).to_equal(uint64_t{41'000});
		g_now = 40'500;
		(void)bound.tick();
		expect(bound.deadlineUs()).to_equal(uint64_t{41'000});
		g_now = 40'700;
		(void)bound.tick(1U);
		(void)bound.tick(2U); // progress: the deadline moves with the span's new start
		expect(bound.deadlineUs()).to_equal(uint64_t{41'700});
		bound.reset();
		expect(bound.deadlineUs()).to_equal(kNever);
	});
});
// clang-format on

CPPSPEC_SPEC(stall_bound)
