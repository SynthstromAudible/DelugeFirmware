// tests/spec_storage/deferred_load_spec.cpp
#include "storage/deferred_load.h"

#include "cppspec.hpp"

using deluge::storage::DeferredLoad;

// clang-format off
describe deferred_load("deluge::storage::DeferredLoad", $ {
	it("dispatches immediately when idle, and the attempt names the requested target", _ {
		DeferredLoad<int> dl;
		expect(dl.request(1)).to_be_true();
		expect(dl.attempt().target).to_equal(1);
		expect(dl.is_current(dl.attempt())).to_be_true();
	});

	it("arms a re-ask when the work deferred", _ {
		DeferredLoad<int> dl;
		dl.request(1);
		auto next = dl.complete(dl.attempt(), /*deferred=*/true);
		expect(next.has_value()).to_be_false();
		expect(dl.pending()).to_be_true();
		expect(dl.take_pending().value_or(-1)).to_equal(1);
		expect(dl.pending()).to_be_false();
	});

	it("arms nothing when the work settled", _ {
		DeferredLoad<int> dl;
		dl.request(1);
		(void)dl.complete(dl.attempt(), /*deferred=*/false);
		expect(dl.pending()).to_be_false();
		expect(dl.in_flight()).to_be_false();
	});

	it("re-dispatches the latest target on completion, dropping the older deferral", _ {
		DeferredLoad<int> dl;
		dl.request(1);
		const auto first = dl.attempt();
		dl.request(2); // queued behind the running op
		auto next = dl.complete(first, /*deferred=*/true);
		expect(next.has_value()).to_be_true();
		expect(next.value_or(-1)).to_equal(2);
		// The newer target supersedes the older one's deferral rather than both being live.
		expect(dl.pending()).to_be_false();
	});

	it("does NOT invalidate an in-flight attempt when a newer target is requested", _ {
		// Scrolling file to file must let the running op finish and hand off, exactly as
		// LatestWins alone did. Only cancel() moves the generation.
		DeferredLoad<int> dl;
		dl.request(1);
		const auto first = dl.attempt();
		dl.request(2);
		expect(dl.is_current(first)).to_be_true();
	});

	it("cancel() makes the in-flight attempt stale", _ {
		DeferredLoad<int> dl;
		dl.request(1);
		const auto attempt = dl.attempt();
		dl.cancel();
		expect(dl.is_current(attempt)).to_be_false();
	});

	it("an attempt READ after the cancellation is stale too", _ {
		// The op reads its attempt when it starts running, which on a queued dispatch is already
		// after the cancellation it has to notice. Stamping the attempt with the generation in
		// force AT THAT MOMENT rather than when the target became current makes every cancelled
		// op look current, and the cancellation does nothing at all.
		DeferredLoad<int> dl;
		dl.request(1);
		dl.cancel();
		expect(dl.is_current(dl.attempt())).to_be_false();
	});

	it("cancel() drops a pending re-ask", _ {
		DeferredLoad<int> dl;
		dl.request(1);
		(void)dl.complete(dl.attempt(), /*deferred=*/true);
		expect(dl.pending()).to_be_true();
		dl.cancel();
		expect(dl.pending()).to_be_false();
	});

	it("a cancelled attempt that deferred re-arms NOTHING", _ {
		// The whole defect: a deferral recorded after the cancellation is re-driven every tick
		// until it lands, and then rendered over whatever the user moved to.
		DeferredLoad<int> dl;
		dl.request(1);
		const auto attempt = dl.attempt();
		dl.cancel();
		auto next = dl.complete(attempt, /*deferred=*/true);
		expect(next.has_value()).to_be_false();
		expect(dl.pending()).to_be_false();
	});

	it("cancel() KEEPS the single-flight guard until the outstanding op reports back", _ {
		DeferredLoad<int> dl;
		dl.request(1);
		const auto attempt = dl.attempt();
		dl.cancel();
		// An op is genuinely still out there: a second dispatch must not run alongside it.
		expect(dl.in_flight()).to_be_true();
		expect(dl.request(2)).to_be_false();
	});

	it("cancel() drops the queued target rather than re-dispatching it afterwards", _ {
		DeferredLoad<int> dl;
		dl.request(1);
		const auto first = dl.attempt();
		dl.request(2);  // queued
		dl.cancel();    // nothing is wanted any more, 2 included
		auto next = dl.complete(first, /*deferred=*/false);
		expect(next.has_value()).to_be_false();
		expect(dl.in_flight()).to_be_false();
	});

	it("the guard is released by the stale op reporting back, not by cancel()", _ {
		DeferredLoad<int> dl;
		dl.request(1);
		const auto attempt = dl.attempt();
		dl.cancel();
		(void)dl.complete(attempt, /*deferred=*/true);
		expect(dl.in_flight()).to_be_false();
		expect(dl.request(9)).to_be_true(); // not wedged
	});

	it("a request made after cancel() is dispatched by the stale op's completion", _ {
		DeferredLoad<int> dl;
		dl.request(1);
		const auto stale = dl.attempt();
		dl.cancel();
		expect(dl.request(2)).to_be_false(); // queued behind the op still out there
		auto next = dl.complete(stale, /*deferred=*/true);
		expect(next.has_value()).to_be_true();
		expect(next.value_or(-1)).to_equal(2);
		// And that newer target IS wanted: cancel() moved the generation before it was requested.
		expect(dl.is_current(dl.attempt())).to_be_true();
	});

	it("request() supersedes a pending re-ask", _ {
		DeferredLoad<int> dl;
		dl.request(1);
		(void)dl.complete(dl.attempt(), /*deferred=*/true);
		expect(dl.request(2)).to_be_true();
		expect(dl.pending()).to_be_false();
	});

	it("release_dispatch() un-wedges the guard and asks again for the dropped target", _ {
		DeferredLoad<int> dl;
		dl.request(7);
		dl.release_dispatch(); // the op was never queued, so it will never complete()
		expect(dl.in_flight()).to_be_false();
		expect(dl.pending()).to_be_true();
		expect(dl.take_pending().value_or(-1)).to_equal(7);
	});
});
// clang-format on

CPPSPEC_SPEC(deferred_load)
