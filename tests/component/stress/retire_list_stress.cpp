// ThreadSanitizer stress harness for deluge::RetireList -- the deferred-destruction list that lets [task] free
// objects the [audio] render may still be reading, keyed to SnapshotDomain's block epoch.
//
// Models the real split: [audio] advances the epoch once per render block, then reads through a small published
// window (an atomic slot updated by [task]); [task] retires superseded objects into the RetireList and reclaim()s
// on its own schedule, never freeing an object [audio] can still see because the epoch hasn't yet proven it
// unreachable. destroyEntry() poisons before freeing, so a retire-too-early bug shows up as a magic-check abort
// even on a run TSan itself doesn't catch as a race.
//
// Three cases, run in order:
//   (a) boot: before [audio] starts, the domain's epoch is 0 -- retire() has no reader to protect against and
//       must destroy immediately.
//   (b) steady state: [task] repeatedly publishes into an atomic slot, retires the superseded entry, and
//       reclaim()s every iteration, while [audio] is running concurrently.
//   (c) burst: retire 512 objects in one go, then reclaim() repeatedly until size() == 0.
// At the end, every object ever retired must have been destroyed exactly once.
//
// What this test does and doesn't prove: it is a regression test for "no grace period at all". Neutering
// retire_list.h's `e.stamp + 2 <= epoch` down to `e.stamp <= epoch` (+0) reliably turns this run RED under TSan,
// which shows SOME grace period is necessary. It does not, and structurally cannot, show that two epochs are
// necessary rather than one: TSan instruments real memory accesses but does not force weak-memory reordering
// windows open, and the +1-vs-+2 gap here is exactly such a window (nanoseconds wide, closed by store draining
// on real hardware long before this test's [audio] loop gets back around). A `+ 1` variant of retire_list.h
// passes this test too. `+ 2` is kept because it matches `Published<T>::reclaim`'s grace period (see
// published.h), which covers the one-block under-count `retire()`'s epoch read can hit -- not because this test
// demonstrates `+ 1` is unsafe.
//
// Built and run by run.sh beside snapshot_primitive_stress.cpp and sound_registry_stress.cpp.

#include "deluge/util/retire_list.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t kMagic = 0x5EED'50D0'CAFE'F00DULL;

struct Entry {
	std::uint64_t magic = kMagic;
	std::uint64_t check = ~kMagic;
};

std::atomic<std::size_t> g_retired_count{0};
std::atomic<std::size_t> g_destroyed_count{0};

void destroyEntry(void* p) {
	auto* e = static_cast<Entry*>(p);
	// Poison before freeing: a reader that still (incorrectly) holds this pointer sees garbage rather than a
	// stale-but-plausible magic, and TSan sees the poison store race directly against any in-flight read.
	std::memset(e, 0xDD, sizeof *e);
	delete e;
	g_destroyed_count.fetch_add(1, std::memory_order_relaxed);
}

std::atomic<Entry*> g_slot{nullptr};
std::atomic<bool> g_stop{false};

/// [audio] Consumer loop: one iteration == one render block.
void audioThread(deluge::util::SnapshotDomain& domain) {
	while (!g_stop.load(std::memory_order_relaxed)) {
		domain.advance_epoch();
		Entry* e = g_slot.load(std::memory_order_acquire);
		if (e != nullptr && (e->magic != kMagic || e->check != ~kMagic)) {
			std::fprintf(stderr, "audio thread read a retired entry\n");
			std::abort();
		}
	}
}

} // namespace

int main() {
	deluge::util::SnapshotDomain domain;
	deluge::RetireList retire(domain);

	// (a) boot case: epoch is 0 before [audio] ever runs -- retire() must destroy immediately.
	{
		auto* e = new Entry;
		g_retired_count.fetch_add(1, std::memory_order_relaxed);
		retire.retire(e, destroyEntry);
		if (g_destroyed_count.load(std::memory_order_relaxed) != 1) {
			std::fprintf(stderr, "boot case: retire() at epoch 0 did not destroy immediately\n");
			return 1;
		}
	}

	std::thread audio(audioThread, std::ref(domain));

	// (b) steady state: publish/retire/reclaim while [audio] is running concurrently.
	constexpr int kIterations = 200000;
	for (int i = 0; i < kIterations; ++i) {
		auto* fresh = new Entry;
		Entry* old = g_slot.exchange(fresh, std::memory_order_acq_rel);
		if (old != nullptr) {
			g_retired_count.fetch_add(1, std::memory_order_relaxed);
			retire.retire(old, destroyEntry);
		}
		retire.reclaim();
	}

	// Drain whatever the steady-state loop's tail left on the list -- [audio] is still running and advancing
	// the epoch, so this converges quickly.
	{
		int spins = 0;
		while (retire.size() != 0) {
			retire.reclaim();
			if (++spins > 1'000'000) {
				std::fprintf(stderr, "steady state: reclaim() never drained (epoch stalled?)\n");
				return 1;
			}
			std::this_thread::yield();
		}
	}

	// (c) burst: retire 512 objects in one go, then reclaim() until they're all gone.
	{
		constexpr int kBurst = 512;
		std::vector<Entry*> burst;
		burst.reserve(kBurst);
		for (int i = 0; i < kBurst; ++i) {
			burst.push_back(new Entry);
		}
		for (Entry* e : burst) {
			g_retired_count.fetch_add(1, std::memory_order_relaxed);
			retire.retire(e, destroyEntry);
		}
		int spins = 0;
		while (retire.size() != 0) {
			retire.reclaim();
			if (++spins > 1'000'000) {
				std::fprintf(stderr, "burst: reclaim() never drained (epoch stalled?)\n");
				return 1;
			}
			std::this_thread::yield();
		}
	}

	g_stop.store(true, std::memory_order_relaxed);
	audio.join();

	// Whatever is left in the slot has no reader left to protect against (audio has joined); free it directly
	// rather than through RetireList, since it was never superseded and so never retired.
	if (Entry* last = g_slot.exchange(nullptr, std::memory_order_relaxed)) {
		delete last;
	}

	const std::size_t retired = g_retired_count.load(std::memory_order_relaxed);
	const std::size_t destroyed = g_destroyed_count.load(std::memory_order_relaxed);
	if (retired != destroyed) {
		std::fprintf(stderr, "retired %zu objects but destroyed %zu\n", retired, destroyed);
		return 1;
	}
	if (retire.size() != 0) {
		std::fprintf(stderr, "retire list not empty at exit: %zu entries remain\n", retire.size());
		return 1;
	}

	std::printf("retire_list_stress: clean (retired/destroyed %zu)\n", retired);
	return 0;
}
