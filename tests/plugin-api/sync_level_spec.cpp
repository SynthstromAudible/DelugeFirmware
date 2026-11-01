#include "plugin-api/types/sync.h"

#include "cppspec.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>

using namespace deluge::plugin;

// clang-format off
describe sync_level("SyncLevel and SyncType", $ {
	it("are one byte each, as songs and records store them", _ {
		expect(std::is_same_v<std::underlying_type_t<SyncLevel>, uint8_t>).to_be_true();
		expect(std::is_same_v<std::underlying_type_t<SyncType>, uint8_t>).to_be_true();
	});

	it("keeps the sync-level values songs save", _ {
		expect(index_of(SyncLevel::NONE)).to_equal(0);
		expect(index_of(SyncLevel::WHOLE)).to_equal(1);
		expect(index_of(SyncLevel::EIGHTH)).to_equal(4);
		expect(index_of(SyncLevel::SIXTEENTH)).to_equal(5);
		expect(index_of(SyncLevel::TWO_HUNDRED_FIFTY_SIXTH)).to_equal(9);
		expect(kMaxSyncLevel).to_equal(9);
	});

	it("keeps the sync-type values the menus and songs use", _ {
		expect(std::to_underlying(SyncType::EVEN)).to_equal(0);
		expect(std::to_underlying(SyncType::TRIPLET)).to_equal(10);
		expect(std::to_underlying(SyncType::DOTTED)).to_equal(19);
	});

	it("round-trips every level through its index", _ {
		for (int32_t i = 0; i <= kMaxSyncLevel; i++) {
			expect(index_of(sync_level_from_index(i))).to_equal(i);
		}
	});

	it("clamps out-of-range indices to the nearest level", _ {
		expect(sync_level_from_index(-3)).to_equal(SyncLevel::NONE);
		expect(sync_level_from_index(10)).to_equal(SyncLevel::TWO_HUNDRED_FIFTY_SIXTH);
		expect(sync_level_from_index(200)).to_equal(SyncLevel::TWO_HUNDRED_FIFTY_SIXTH);
	});

	it("computes the shift amounts the swing and LFO code computes today", _ {
		// playback_handler: leftShift = 10 - swing_interval; Sound LFO: SYNC_LEVEL_256TH - level
		expect(10 - index_of(SyncLevel::SIXTEENTH)).to_equal(5);
		expect(index_of(SyncLevel::TWO_HUNDRED_FIFTY_SIXTH) - index_of(SyncLevel::EIGHTH)).to_equal(5);
	});
});

CPPSPEC_SPEC(sync_level)
