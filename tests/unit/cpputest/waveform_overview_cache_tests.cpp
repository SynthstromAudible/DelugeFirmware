#include "CppUTest/TestHarness.h"

#include "model/sample/overview_cache_entry.h"
#include "util/segmented_vector.h"
#include <cstdint>

// Mechanics for the container backing the waveform overview cache --
// `OverviewCacheEntry` plus the `SegmentedVector` it's stored in on `Sample` (`Sample::overviewCache_`,
// sized via `Sample::resizeOverviewCache()`, read via `overviewCacheSize()` / `overviewCacheEntry()`).
//
// A real `Sample` can't be constructed in this host harness: `sample.h`'s transitive closure pulls in
// firmware-only dependencies (e.g. `argon.hpp`), and its cache uses `deluge::memory::fast_allocator`,
// which routes through the firmware/Rust heap that isn't linked here (see fast_allocator.h).
// `OverviewCacheEntry` is deliberately its own lightweight header for exactly this reason (see its own
// docs), and `SegmentedVector`'s allocator defaults to `std::allocator` for exactly this reason too (see
// its own docs: "stays BSP-free ... links in the plain host ... harness with no initialized heap"). So
// this test instantiates the identical container template, over the real production
// `OverviewCacheEntry` type, with that default allocator -- validating precisely the resize/default/
// round-trip mechanics that `overviewCacheSize()` / `overviewCacheEntry()` / `resizeOverviewCache()`
// forward to (those three are one-line forwards onto this same container type, see sample.h).
using OverviewCache = deluge::SegmentedVector<OverviewCacheEntry, 256>;

TEST_GROUP(WaveformOverviewCache){};

TEST(WaveformOverviewCache, EntriesDefaultOnGrow) {
	OverviewCache cache;
	cache.resize(10);
	CHECK_EQUAL(10, cache.size());
	for (size_t i = 0; i < cache.size(); i++) {
		CHECK_EQUAL(INT32_MAX, (cache[i].min));
		CHECK_EQUAL(INT32_MIN, (cache[i].max));
		CHECK_FALSE(cache[i].investigated);
	}
}

TEST(WaveformOverviewCache, SizeTracksResize) {
	OverviewCache cache;
	CHECK_EQUAL(0, cache.size());

	cache.resize(5);
	CHECK_EQUAL(5, cache.size());

	cache.resize(2); // shrink
	CHECK_EQUAL(2, cache.size());

	cache.resize(300); // grow back past the 256-entry segment boundary
	CHECK_EQUAL(300, cache.size());
	// Freshly-grown tail entries (including ones past the old high-water mark) still default.
	CHECK_EQUAL(INT32_MAX, (cache[299].min));
	CHECK_EQUAL(INT32_MIN, (cache[299].max));
	CHECK_FALSE(cache[299].investigated);
}

TEST(WaveformOverviewCache, EntryRoundTrips) {
	OverviewCache cache;
	cache.resize(3);

	// A quiet passage's peaks, exactly as a direct read of the samples finds them.
	cache[1].min = -385280;
	cache[1].max = 356608;
	cache[1].investigated = true;

	CHECK_EQUAL(-385280, cache[1].min);
	CHECK_EQUAL(356608, cache[1].max);
	CHECK_TRUE(cache[1].investigated);

	// Neighbours untouched.
	CHECK_EQUAL(INT32_MAX, (cache[0].min));
	CHECK_EQUAL(INT32_MIN, (cache[0].max));
	CHECK_FALSE(cache[0].investigated);
	CHECK_EQUAL(INT32_MAX, (cache[2].min));
	CHECK_EQUAL(INT32_MIN, (cache[2].max));
	CHECK_FALSE(cache[2].investigated);
}
