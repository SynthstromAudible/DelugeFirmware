#include "util/spsc_ring.h"

#include "cppspec.hpp"
#include <cstddef>
#include <vector>

using deluge::util::SpscRing;

namespace {

/// @brief Simple event payload for the spec: distinguishable, default-
///        constructible, copy/move-assignable -- exactly what `SpscRing<E, N>`
///        requires of `E`.
struct Event {
	int value = -1;

	bool operator==(const Event& other) const { return value == other.value; }
};

} // namespace

// clang-format off
describe spsc_ring("SpscRing<E, N>", $ {
	it("compiles and default-constructs for a power-of-two capacity", _{
		SpscRing<Event, 4> ring;
		expect(ring.capacity()).to_equal(std::size_t{4});
		expect(ring.empty()).to_be_true();
	});

	it("try_pop fails on an empty ring", _{
		SpscRing<Event, 4> ring;
		Event out;
		expect(ring.try_pop(out)).to_be_false();
	});

	it("delivers pushed elements in FIFO order", _{
		SpscRing<Event, 4> ring;
		expect(ring.try_push(Event{1})).to_be_true();
		expect(ring.try_push(Event{2})).to_be_true();
		expect(ring.try_push(Event{3})).to_be_true();

		Event out;
		expect(ring.try_pop(out)).to_be_true();
		expect(out).to_equal(Event{1});
		expect(ring.try_pop(out)).to_be_true();
		expect(out).to_equal(Event{2});
		expect(ring.try_pop(out)).to_be_true();
		expect(out).to_equal(Event{3});
		expect(ring.try_pop(out)).to_be_false();
	});

	it("try_push fails once the ring is full, without disturbing existing contents", _{
		SpscRing<Event, 4> ring;
		expect(ring.try_push(Event{1})).to_be_true();
		expect(ring.try_push(Event{2})).to_be_true();
		expect(ring.try_push(Event{3})).to_be_true();
		expect(ring.try_push(Event{4})).to_be_true();
		// Full: capacity 4, 4 elements queued.
		expect(ring.try_push(Event{5})).to_be_false();

		Event out;
		expect(ring.try_pop(out)).to_be_true();
		expect(out).to_equal(Event{1});
	});

	it("frees a slot for reuse after try_pop, allowing push to succeed again", _{
		SpscRing<Event, 4> ring;
		expect(ring.try_push(Event{1})).to_be_true();
		expect(ring.try_push(Event{2})).to_be_true();
		expect(ring.try_push(Event{3})).to_be_true();
		expect(ring.try_push(Event{4})).to_be_true();
		expect(ring.try_push(Event{5})).to_be_false();

		Event out;
		expect(ring.try_pop(out)).to_be_true();
		expect(ring.try_push(Event{5})).to_be_true();
	});

	it("handles wrap-around across the power-of-two index boundary", _{
		SpscRing<Event, 4> ring;
		// Fill, drain some, refill past the point where the internal index
		// wraps modulo capacity -- exercises the `& (N - 1)` masking on both
		// head_ and tail_ crossing their first wrap.
		expect(ring.try_push(Event{1})).to_be_true();
		expect(ring.try_push(Event{2})).to_be_true();
		expect(ring.try_push(Event{3})).to_be_true();

		Event out;
		expect(ring.try_pop(out)).to_be_true();
		expect(out).to_equal(Event{1});
		expect(ring.try_pop(out)).to_be_true();
		expect(out).to_equal(Event{2});

		// tail_ index is now 3, head_ is 2; push three more so tail_ crosses 4
		// (wraps back to slot 0 under masking).
		expect(ring.try_push(Event{4})).to_be_true();
		expect(ring.try_push(Event{5})).to_be_true();
		expect(ring.try_push(Event{6})).to_be_true();
		// Ring holds {3, 4, 5, 6} now -- full again.
		expect(ring.try_push(Event{7})).to_be_false();

		expect(ring.try_pop(out)).to_be_true();
		expect(out).to_equal(Event{3});
		expect(ring.try_pop(out)).to_be_true();
		expect(out).to_equal(Event{4});
		expect(ring.try_pop(out)).to_be_true();
		expect(out).to_equal(Event{5});
		expect(ring.try_pop(out)).to_be_true();
		expect(out).to_equal(Event{6});
		expect(ring.try_pop(out)).to_be_false();
	});

	context("drain(fn, max)", _{
		it("returns 0 and calls fn zero times on an empty ring", _{
			SpscRing<Event, 4> ring;
			std::vector<int> seen;
			std::size_t n = ring.drain([&](Event&& e) { seen.push_back(e.value); }, 10);
			expect(n).to_equal(std::size_t{0});
			expect(seen.size()).to_equal(std::size_t{0});
		});

		it("drains at most max elements, leaving the rest queued", _{
			SpscRing<Event, 4> ring;
			expect(ring.try_push(Event{1})).to_be_true();
			expect(ring.try_push(Event{2})).to_be_true();
			expect(ring.try_push(Event{3})).to_be_true();
			expect(ring.try_push(Event{4})).to_be_true();

			std::vector<int> seen;
			std::size_t n = ring.drain([&](Event&& e) { seen.push_back(e.value); }, 2);
			expect(n).to_equal(std::size_t{2});
			expect(seen.size()).to_equal(std::size_t{2});
			expect(seen[0]).to_equal(1);
			expect(seen[1]).to_equal(2);

			// The remaining 2 elements are still queued, in order.
			Event out;
			expect(ring.try_pop(out)).to_be_true();
			expect(out).to_equal(Event{3});
			expect(ring.try_pop(out)).to_be_true();
			expect(out).to_equal(Event{4});
		});

		it("drains everything and returns the count when max exceeds the queued count", _{
			SpscRing<Event, 4> ring;
			expect(ring.try_push(Event{1})).to_be_true();
			expect(ring.try_push(Event{2})).to_be_true();

			std::vector<int> seen;
			std::size_t n = ring.drain([&](Event&& e) { seen.push_back(e.value); }, 10);
			expect(n).to_equal(std::size_t{2});
			expect(seen.size()).to_equal(std::size_t{2});
			expect(ring.empty()).to_be_true();
		});

		it("frees slots it drains, allowing push to refill afterward", _{
			SpscRing<Event, 4> ring;
			expect(ring.try_push(Event{1})).to_be_true();
			expect(ring.try_push(Event{2})).to_be_true();
			expect(ring.try_push(Event{3})).to_be_true();
			expect(ring.try_push(Event{4})).to_be_true();

			std::vector<int> seen;
			ring.drain([&](Event&& e) { seen.push_back(e.value); }, 4);
			expect(ring.empty()).to_be_true();

			expect(ring.try_push(Event{5})).to_be_true();
			expect(ring.try_push(Event{6})).to_be_true();
			expect(ring.try_push(Event{7})).to_be_true();
			expect(ring.try_push(Event{8})).to_be_true();
			expect(ring.try_push(Event{9})).to_be_false();
		});
	});
});

CPPSPEC_SPEC(spsc_ring)
