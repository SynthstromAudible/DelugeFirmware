// tests/spec_storage/latest_wins_spec.cpp
#include "storage/latest_wins.h"

#include "cppspec.hpp"

using deluge::storage::LatestWins;

// clang-format off
describe latest_wins("deluge::storage::LatestWins", $ {
	it("dispatches immediately when idle", _ {
		LatestWins<int> lw;
		expect(lw.request(1)).to_be_true();   // idle → dispatch now
		expect(lw.in_flight()).to_be_true();
		expect(lw.current()).to_equal(1);
	});

	it("coalesces a request made while in flight (does not dispatch)", _ {
		LatestWins<int> lw;
		lw.request(1);
		expect(lw.request(2)).to_be_false();  // in flight → queued, no dispatch
	});

	it("re-dispatches the LATEST queued target on completion", _ {
		LatestWins<int> lw;
		lw.request(1);          // dispatch 1
		lw.request(2);          // queued
		lw.request(3);          // queued (supersedes 2)
		auto next = lw.complete();
		expect(next.has_value()).to_be_true();
		expect(next.value()).to_equal(3);     // latest wins, not 2
		expect(lw.current()).to_equal(3);
		expect(lw.in_flight()).to_be_true();  // still in flight for the re-dispatch
	});

	it("goes idle on completion when nothing was queued", _ {
		LatestWins<int> lw;
		lw.request(1);
		auto next = lw.complete();
		expect(next.has_value()).to_be_false();
		expect(lw.in_flight()).to_be_false();
	});

	it("reset() releases the in-flight guard so a later request dispatches again", _ {
		LatestWins<int> lw;
		lw.request(1);          // in flight
		lw.request(2);          // queued
		lw.reset();             // dispatch was dropped — release, discard the queued target
		expect(lw.in_flight()).to_be_false();
		expect(lw.request(9)).to_be_true(); // not wedged: dispatches again
	});

	it("clear_queued() drops the queued target but KEEPS the in-flight guard", _ {
		LatestWins<int> lw;
		lw.request(1);          // in flight
		lw.request(2);          // queued
		lw.clear_queued();       // 2 is no longer wanted; the op running 1 is still out there
		expect(lw.in_flight()).to_be_true();
		expect(lw.request(3)).to_be_false(); // still single-flight: 3 queues, does not dispatch
		auto next = lw.complete();
		expect(next.has_value()).to_be_true();
		expect(next.value()).to_equal(3);    // 2 was dropped, not merely superseded
	});

	it("converges: after the re-dispatch completes with nothing new, it idles", _ {
		LatestWins<int> lw;
		lw.request(1);
		lw.request(2);          // queued
		lw.complete();          // re-dispatch 2
		auto next = lw.complete();
		expect(next.has_value()).to_be_false();
		expect(lw.in_flight()).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(latest_wins)
