#include "modulation/sync.h"

#include <utility>

#include "cppspec.hpp"
#include "support/spec_main.h"

// clang-format off
describe sync_group("sync", ${
	it("wrap swing interval sync level", _ {
		for (int32_t i = 1; i < NUM_SWING_INTERVALS; i++) {
			expect(wrapSwingIntervalSyncLevel(i)).to_equal(i);
		}
		expect(wrapSwingIntervalSyncLevel(-1)).to_equal(NUM_SWING_INTERVALS - 2);
		expect(wrapSwingIntervalSyncLevel(0)).to_equal(NUM_SWING_INTERVALS - 1);
		expect(wrapSwingIntervalSyncLevel(NUM_SWING_INTERVALS)).to_equal(1);
		expect(wrapSwingIntervalSyncLevel(NUM_SWING_INTERVALS + 1)).to_equal(2);
	});

	it("sync value to sync type", _ {
		for (int32_t i = 0; i < std::to_underlying(SYNC_TYPE_TRIPLET); i++) {
			expect(std::to_underlying(syncValueToSyncType(i))).to_equal(std::to_underlying(SYNC_TYPE_EVEN));
		}
		for (int32_t i = std::to_underlying(SYNC_TYPE_TRIPLET) + 1; i < std::to_underlying(SYNC_TYPE_DOTTED); i++) {
			expect(std::to_underlying(syncValueToSyncType(i))).to_equal(std::to_underlying(SYNC_TYPE_TRIPLET));
		}
		for (int32_t i = std::to_underlying(SYNC_TYPE_DOTTED) + 1; i < NUM_SYNC_VALUES; i++) {
			expect(std::to_underlying(syncValueToSyncType(i))).to_equal(std::to_underlying(SYNC_TYPE_DOTTED));
		}
	});

	it("sync value to sync level", _ {
		// Zero is off
		for (int32_t i = 0; i <= MAX_SYNC_LEVEL; i++) {
			expect(deluge::plugin::index_of(syncValueToSyncLevel(i))).to_equal(i);
		}
		// Zero as level doesn't exist for triplets and dotted
		for (int32_t i = 0; i < MAX_SYNC_LEVEL; i++) {
			expect(deluge::plugin::index_of(syncValueToSyncLevel(i + std::to_underlying(SYNC_TYPE_TRIPLET)))).to_equal(i + 1);
		}
		for (int32_t i = 0; i < MAX_SYNC_LEVEL; i++) {
			expect(deluge::plugin::index_of(syncValueToSyncLevel(i + std::to_underlying(SYNC_TYPE_DOTTED)))).to_equal(i + 1);
		}
	});
});

DELUGE_SPEC(sync_spec, sync_group)
