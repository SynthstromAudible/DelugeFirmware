#pragma once

#include "allocator_under_test.h"
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

// Allocator-agnostic conformance suite. These runners drive randomized
// alloc/free/realloc sequences against any AllocatorUnderTest and report
// invariant violations as counters the spec asserts to be zero. The same
// runners gate both the current MemoryRegion and the future Rust core, so a
// regression in either is unit-visible in ~ms instead of a 90s render +
// crash-tea-leaf reading (see docs/dev/memory_allocator.md).
//
// Invariants checked:
//   * alignment — every pointer honors max(16, requested align)
//   * no overlap — live usable ranges are disjoint
//   * data integrity — a per-block byte pattern survives unrelated alloc/free
//     churn (catches overlap + bad coalescing that corrupts neighbours)
//   * realloc fidelity — the leading min(old,new) bytes are preserved
//   * no capacity leak — after a full drain the arena yields as much as a
//     pristine arena did (coalescing returns all freed space)
namespace conformance {

struct Report {
	int allocs = 0;
	int frees = 0;
	int reallocs = 0;
	int misaligned = 0;
	int overlaps = 0;
	int corrupted = 0;        // data-integrity violations found on free/realloc
	int reallocLostData = 0;  // realloc dropped preserved bytes
	int pristineCapacity = 0; // greedy fill of a fresh arena (bytes), reference
	int drainedCapacity = 0;  // greedy fill after churn+drain (must be >= pristine)
};

namespace detail {

struct LiveBlock {
	uint8_t* ptr;
	size_t usable;
	size_t requested;
	uint8_t seed;
};

// A cheap, position-dependent pattern so a misplaced write into a neighbour is
// caught: byte i = seed + i.
inline void writePattern(uint8_t* p, size_t n, uint8_t seed) {
	for (size_t i = 0; i < n; i++) {
		p[i] = static_cast<uint8_t>(seed + i);
	}
}

inline bool checkPattern(const uint8_t* p, size_t n, uint8_t seed) {
	for (size_t i = 0; i < n; i++) {
		if (p[i] != static_cast<uint8_t>(seed + i)) {
			return false;
		}
	}
	return true;
}

inline bool overlaps(const LiveBlock& a, const LiveBlock& b) {
	uint8_t* aEnd = a.ptr + a.usable;
	uint8_t* bEnd = b.ptr + b.usable;
	return a.ptr < bEnd && b.ptr < aEnd;
}

// Greedily allocate uniform medium chunks until the arena is exhausted, return
// total usable bytes handed out, then free them all. Used as a capacity probe.
inline int greedyFillThenDrain(AllocatorUnderTest& a, size_t chunk) {
	std::vector<void*> live;
	size_t total = 0;
	while (void* p = a.alloc(chunk, 16)) {
		total += a.usable_size(p);
		live.push_back(p);
	}
	for (void* p : live) {
		a.free(p);
	}
	return static_cast<int>(total);
}

} // namespace detail

// Randomized churn: a slot table of allocations, each step allocates an empty
// slot, frees / reallocs a full one. Log-distributed sizes are the worst case
// for packing. Verifies alignment, non-overlap and data integrity throughout,
// then drains and verifies no data was corrupted.
inline Report runChurn(AllocatorUnderTest& a, uint32_t seed, int iters, int slots = 1500) {
	using namespace detail;
	Report r;
	std::mt19937 rng(seed);
	std::vector<LiveBlock> table(slots);
	const size_t maxAlign = a.max_align();

	auto pickAlign = [&]() -> size_t {
		size_t cap = maxAlign;
		size_t al = size_t{1} << (rng() % 5); // 1..16
		return al > cap ? cap : al;
	};
	auto pickSize = [&]() -> size_t {
		int magnitude = rng() % 16;
		return static_cast<size_t>((rng() % 10 + 1) << magnitude);
	};

	for (int it = 0; it < iters; it++) {
		int slot = rng() % slots;
		LiveBlock& b = table[slot];

		if (b.ptr == nullptr) {
			// allocate
			size_t size = pickSize();
			size_t align = pickAlign();
			void* p = a.alloc(size, align);
			if (p == nullptr) {
				continue; // out of space is legal
			}
			r.allocs++;
			size_t need = align < 16 ? 16 : align;
			if ((reinterpret_cast<uintptr_t>(p) & (need - 1)) != 0) {
				r.misaligned++;
			}
			b = {static_cast<uint8_t*>(p), a.usable_size(p), size, static_cast<uint8_t>(rng())};
			for (int j = 0; j < slots; j++) {
				if (j != slot && table[j].ptr != nullptr && overlaps(table[j], b)) {
					r.overlaps++;
					break;
				}
			}
			writePattern(b.ptr, b.usable, b.seed);
		}
		else if (rng() % 3 == 0) {
			// realloc
			if (!checkPattern(b.ptr, b.usable, b.seed)) {
				r.corrupted++;
			}
			size_t oldUsable = b.usable;
			uint8_t oldSeed = b.seed;
			size_t newSize = pickSize();
			size_t align = pickAlign();
			void* np = a.realloc(b.ptr, newSize, align);
			r.reallocs++;
			if (np == nullptr) {
				continue; // failure leaves original intact (still tracked)
			}
			size_t preserved = oldUsable < newSize ? oldUsable : newSize;
			if (!checkPattern(static_cast<uint8_t*>(np), preserved, oldSeed)) {
				r.reallocLostData++;
			}
			b.ptr = static_cast<uint8_t*>(np);
			b.usable = a.usable_size(np);
			b.requested = newSize;
			b.seed = static_cast<uint8_t>(rng());
			writePattern(b.ptr, b.usable, b.seed);
		}
		else {
			// free
			if (!checkPattern(b.ptr, b.usable, b.seed)) {
				r.corrupted++;
			}
			a.free(b.ptr);
			r.frees++;
			b.ptr = nullptr;
		}
	}

	// Drain everything still live, verifying integrity on the way out.
	for (LiveBlock& b : table) {
		if (b.ptr != nullptr) {
			if (!checkPattern(b.ptr, b.usable, b.seed)) {
				r.corrupted++;
			}
			a.free(b.ptr);
			r.frees++;
			b.ptr = nullptr;
		}
	}
	return r;
}

// Capacity-leak check: a fresh arena's greedy fill must equal a post-churn,
// post-drain arena's greedy fill. If coalescing ever fails to return freed
// space, the drained capacity drops below pristine.
inline Report runDrainCapacity(AllocatorUnderTest& fresh, AllocatorUnderTest& churned, uint32_t seed, int iters) {
	Report r;
	r.pristineCapacity = detail::greedyFillThenDrain(fresh, 4096);
	runChurn(churned, seed, iters);
	r.drainedCapacity = detail::greedyFillThenDrain(churned, 4096);
	return r;
}

} // namespace conformance
