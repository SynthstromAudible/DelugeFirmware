// ThreadSanitizer stress harness for deluge::util::Published<T> and
// deluge::util::SpscRing<E, N> -- the empirical correctness gate for the
// "publish spine" primitives. published.h / spsc_ring.h carry the
// analytical memory-model arguments for why these are race-free;
// this binary is a standalone reproduction of the real [task]/[audio] split
// under ThreadSanitizer, which instruments the actual concurrent accesses
// to cur_/epoch_/head_/tail_/the slots/the retired Ts and reports any data
// race or use-after-free the analytical review might have missed.
//
// Deliberately NOT wired into ./dbt test's CppSpec runner: that would need
// -fsanitize=thread across the whole spec suite (and a CMake reconfigure to
// pick up a new spec file). A focused standalone TSan binary is cleaner and
// is what actually gates this. See run.sh for the build+run wrapper.
//
// Build/run directly (see run.sh for the scripted version that also runs it
// N times and checks TSan is actually linked in):
//
//   clang++ -std=c++23 -fsanitize=thread -O1 -g \
//       -I src -pthread tests/component/stress/snapshot_primitive_stress.cpp \
//       -o /tmp/snap_stress
//   TSAN_OPTIONS="halt_on_error=1" /tmp/snap_stress
//
// Threads:
//   [audio] consumer -- a tight loop modeling one render block per
//   iteration: domain.advance_epoch(), then pub.load(), read the whole
//   Snapshot and check its internal consistency invariant, then
//   ring.drain() a bounded batch of Events and check each one's invariant
//   plus strictly-increasing sequence number. Never holds a load()'d
//   pointer across an iteration boundary, matching the real reader
//   contract.
//
//   [task] producer -- a tight loop publish()ing fresh self-consistent
//   Snapshots (b == a ^ kSnapshotMagic), periodically reclaim()ing, and
//   try_push()ing a monotonically-sequenced Event stream. Uses a
//   PoisonDeleter that scribbles a retired Snapshot's bytes before freeing
//   it, so a use-after-free read is maximally likely to both (a) break the
//   invariant check below, independent of TSan, and (b) be caught directly
//   by ThreadSanitizer as a race between the poison store and an in-flight
//   reader.
//
// A failed invariant fails the process (nonzero exit); an uncaught data
// race or use-after-free is reported by TSan itself (and, with
// halt_on_error=1, aborts the process).

#include "deluge/util/published.h"
#include "deluge/util/spsc_ring.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

using deluge::util::Published;
using deluge::util::SnapshotDomain;
using deluge::util::SpscRing;

constexpr std::uint64_t kSnapshotMagic = 0x9E3779B97F4A7C15ull;
constexpr std::uint64_t kEventMagic = 0xD1B54A32D192ED03ull;

/// The immutable snapshot payload under test. `b` is a deterministic
/// function of `a`; a torn write, torn read, or use-after-free read is
/// overwhelmingly likely to break this invariant.
struct Snapshot {
	std::uint64_t a;
	std::uint64_t b;
};

[[nodiscard]] bool snapshotValid(const Snapshot& s) noexcept {
	return s.b == (s.a ^ kSnapshotMagic);
}

/// One [task]->[audio] event. `seq` is a per-producer monotonic counter;
/// `payload` is a deterministic function of `seq`, same rationale as above.
struct Event {
	std::uint64_t seq = 0;
	std::uint64_t payload = 0;
};

[[nodiscard]] bool eventValid(const Event& e) noexcept {
	return e.payload == (e.seq ^ kEventMagic);
}

/// Reclamation hook for `Published<Snapshot>`: scribble a poison pattern
/// into a retired object before freeing it. This makes a use-after-free
/// read (a) very likely to violate `snapshotValid()` even where it doesn't
/// crash outright, and (b) directly catchable by ThreadSanitizer, since the
/// poison store races with any reader that (incorrectly, i.e. because of a
/// real bug in the primitive) still holds the pointer.
struct PoisonDeleter {
	void operator()(const Snapshot* p) const noexcept {
		std::memset(const_cast<void*>(static_cast<const void*>(p)), 0xDE, sizeof(Snapshot));
		delete p;
	}
};

std::atomic<bool> g_failed{false};

void fail(const char* what) {
	// First failure wins verbosely; later ones just keep the flag set so
	// both threads wind down promptly without a stderr pile-up.
	if (!g_failed.exchange(true, std::memory_order_relaxed)) {
		std::fprintf(stderr, "FAIL: %s\n", what);
	}
}

constexpr std::chrono::milliseconds kRunDuration{1500};
constexpr std::size_t kRingCapacity = 1024;
constexpr std::size_t kDrainBudget = 16;
constexpr std::size_t kReclaimPeriod = 7; // publishes between reclaim() sweeps

using PublishedSnapshot = Published<Snapshot, PoisonDeleter>;
using EventRing = SpscRing<Event, kRingCapacity>;

/// [audio] Consumer loop: one iteration == one render block.
void audioThread(SnapshotDomain& domain, PublishedSnapshot& pub, EventRing& ring, const std::atomic<bool>& stop) {
	std::uint64_t lastSnapshotA = 0;
	bool haveLastSnapshot = false;
	std::uint64_t lastEventSeq = 0;
	bool haveLastEvent = false;

	while (!stop.load(std::memory_order_relaxed) && !g_failed.load(std::memory_order_relaxed)) {
		domain.advance_epoch();

		const Snapshot* s = pub.load();
		if (s != nullptr) {
			const Snapshot local = *s; // whole-object read, scoped to this block only
			if (!snapshotValid(local)) {
				fail("Published<Snapshot>::load() returned a torn/poisoned/UAF snapshot");
			}
			else if (haveLastSnapshot && local.a < lastSnapshotA) {
				// Coherence guarantee: a single reader's successive loads of
				// one atomic object cannot regress relative to the single
				// writer's modification order.
				fail("Published<Snapshot>::load() went backwards (publish order violated)");
			}
			lastSnapshotA = local.a;
			haveLastSnapshot = true;
		}

		ring.drain(
		    [&](Event&& e) {
			    if (!eventValid(e)) {
				    fail("SpscRing::drain() returned a torn event");
			    }
			    else if (haveLastEvent && e.seq <= lastEventSeq) {
				    fail("SpscRing::drain() delivered an out-of-order/duplicate seq");
			    }
			    lastEventSeq = e.seq;
			    haveLastEvent = true;
		    },
		    kDrainBudget);
	}
}

/// [task] Producer loop: publish fresh Snapshots + push Events.
void taskThread(SnapshotDomain& domain, PublishedSnapshot& pub, EventRing& ring, const std::atomic<bool>& stop) {
	std::uint64_t a = 0;
	std::uint64_t seq = 0;
	std::size_t sincePublish = 0;

	while (!stop.load(std::memory_order_relaxed) && !g_failed.load(std::memory_order_relaxed)) {
		++a;
		auto* next = new Snapshot{a, a ^ kSnapshotMagic};
		pub.publish(next);
		if (++sincePublish >= kReclaimPeriod) {
			pub.reclaim();
			sincePublish = 0;
		}

		++seq;
		const Event ev{seq, seq ^ kEventMagic};
		// Overflow policy is the caller's problem (see spsc_ring.h's file
		// banner): dropping on a full ring is fine here -- we only assert
		// the ordering/validity of whatever IS delivered, never delivery
		// itself.
		(void)ring.try_push(ev);

		// Vary interleaving across iterations/runs rather than always
		// spinning lock-step against the consumer.
		if ((a & 0xF) == 0) {
			std::this_thread::yield();
		}
	}
}

} // namespace

int main() {
	SnapshotDomain domain;
	auto* initial = new Snapshot{0, 0 ^ kSnapshotMagic};
	PublishedSnapshot pub(domain, initial);
	EventRing ring;
	std::atomic<bool> stop{false};

	std::thread audio(audioThread, std::ref(domain), std::ref(pub), std::ref(ring), std::cref(stop));
	std::thread task(taskThread, std::ref(domain), std::ref(pub), std::ref(ring), std::cref(stop));

	std::this_thread::sleep_for(kRunDuration);
	stop.store(true, std::memory_order_relaxed);

	task.join();
	audio.join();

	// pub's destructor now runs single-threaded (both workers joined),
	// freeing cur_ and everything still on the retire list -- ordinary
	// teardown, not itself part of the concurrency scenario under test.

	if (g_failed.load(std::memory_order_relaxed)) {
		std::fprintf(stderr, "STRESS RESULT: FAIL\n");
		return 1;
	}
	std::fprintf(stderr, "STRESS RESULT: PASS\n");
	return 0;
}
