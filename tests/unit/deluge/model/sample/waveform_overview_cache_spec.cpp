#include "model/sample/overview_cache_entry.h"
#include "util/segmented_vector.h"
#include <cstdint>

#include "cppspec.hpp"
#include "support/spec_main.h"

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
// this spec instantiates the identical container template, over the real production
// `OverviewCacheEntry` type, with that default allocator -- validating precisely the resize/default/
// round-trip mechanics that `overviewCacheSize()` / `overviewCacheEntry()` / `resizeOverviewCache()`
// forward to (those three are one-line forwards onto this same container type, see sample.h).
using OverviewCache = deluge::SegmentedVector<OverviewCacheEntry, 256>;

// clang-format off
describe waveform_overview_cache_group("waveform overview cache", ${
	it("entries default on grow", _ {
		OverviewCache cache;
		cache.resize(10);
		expect(cache.size()).to_equal(10);
		for (size_t i = 0; i < cache.size(); i++) {
			expect(static_cast<int>(cache[i].min)).to_equal(127);
			expect(static_cast<int>(cache[i].max)).to_equal(-128);
			expect(cache[i].investigated).to_be_false();
		}
	});

	it("size tracks resize", _ {
		OverviewCache cache;
		expect(cache.size()).to_equal(0);

		cache.resize(5);
		expect(cache.size()).to_equal(5);

		cache.resize(2); // shrink
		expect(cache.size()).to_equal(2);

		cache.resize(300); // grow back past the 256-entry segment boundary
		expect(cache.size()).to_equal(300);
		// Freshly-grown tail entries (including ones past the old high-water mark) still default.
		expect(static_cast<int>(cache[299].min)).to_equal(127);
		expect(static_cast<int>(cache[299].max)).to_equal(-128);
		expect(cache[299].investigated).to_be_false();
	});

	it("entry round trips", _ {
		OverviewCache cache;
		cache.resize(3);

		cache[1].min = 5;
		cache[1].max = 42;
		cache[1].investigated = true;

		expect(static_cast<int>(cache[1].min)).to_equal(5);
		expect(static_cast<int>(cache[1].max)).to_equal(42);
		expect(cache[1].investigated).to_be_true();

		// Neighbours untouched.
		expect(static_cast<int>(cache[0].min)).to_equal(127);
		expect(static_cast<int>(cache[0].max)).to_equal(-128);
		expect(cache[0].investigated).to_be_false();
		expect(static_cast<int>(cache[2].min)).to_equal(127);
		expect(static_cast<int>(cache[2].max)).to_equal(-128);
		expect(cache[2].investigated).to_be_false();
	});
});

DELUGE_SPEC(waveform_overview_cache_spec, waveform_overview_cache_group)
