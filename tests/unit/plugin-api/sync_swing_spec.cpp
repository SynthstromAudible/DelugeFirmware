#include "plugin-api/types/display_mapping.h"

#include <cppspec.hpp>

using namespace deluge::plugin;

// clang-format off
describe sync_swing("swing intervals", ${
	it("wraps past either end to the other, never to off", _ {
		expect(wrap_swing_interval(kNumSwingIntervals)).to_equal(1);
		expect(wrap_swing_interval(0)).to_equal(kNumSwingIntervals - 1);
		expect(wrap_swing_interval(1)).to_equal(1);
	});
	it("clamps to the intervals", _ {
		expect(clamp_swing_interval(-5)).to_equal(kMinSwingInterval);
		expect(clamp_swing_interval(1000)).to_equal(kMaxSwingInterval);
	});
	it("finds a sync type's choice index", _ {
		expect(sync_type_choice(SyncType::EVEN)).to_equal(0);
		expect(sync_type_choice(SyncType::TRIPLET)).to_equal(1);
		expect(sync_type_choice(SyncType::DOTTED)).to_equal(2);
	});
	it("counts every combined sync value", _ {
		expect(kNumCombinedSyncValues).to_equal(28);
	});
});

CPPSPEC_SPEC(sync_swing)
