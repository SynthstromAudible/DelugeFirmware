#include "util/db_shift.h"

#include "cppspec.hpp"
#include "support/spec_main.h"
#include <cstdint>
#include <limits>

// clang-format off
describe db_shift_group("db shift", ${
	it("positive offset increases value", _ {
		int32_t oldValue = 0;
		expect(shiftVolumeByDB(oldValue, 3.0f)).to_be_greater_than(oldValue);
	});

	it("negative offset decreases value", _ {
		// A negative offset must attenuate, not be lost: converting a negative float to uint32_t is UB.
		int32_t oldValue = 0;
		expect(shiftVolumeByDB(oldValue, -3.0f)).to_be_less_than(oldValue);
	});

	it("zero offset is identity", _ {
		int32_t oldValue = 0x50000000;
		expect(shiftVolumeByDB(oldValue, 0.0f)).to_equal(oldValue);
	});

	it("intra interval position is preserved", _ {
		// shiftVolumeByDB keeps a value's fractional position up its interval (howFarUpInterval is a float fraction, not
		// an integer quotient), so two values in the same interval stay ordered after a shift.
		int32_t low = 0x50000000;
		int32_t high = 0x5F000000;
		expect(low).to_be_less_than(high);
		expect(shiftVolumeByDB(low, 1.0f)).to_be_less_than(shiftVolumeByDB(high, 1.0f));
	});

	it("large negative offset saturates to silence", _ {
		expect(shiftVolumeByDB(0, -500.0f)).to_equal(std::numeric_limits<int32_t>::min());
	});

	it("large positive offset saturates to maximum", _ {
		expect(shiftVolumeByDB(0, 500.0f)).to_equal(std::numeric_limits<int32_t>::max());
	});

	it("round trip approximately restores", _ {
		// Start mid-range. The upper intervals are narrow - 13, 14 and 15 together span only 3.6 dB - so a large offset
		// near the top saturates legitimately and cannot round-trip.
		int32_t oldValue = 0;
		int32_t shifted = shiftVolumeByDB(oldValue, 1.0f);
		expect(shifted).to_be_greater_than(oldValue);

		int32_t restored = shiftVolumeByDB(shifted, -1.0f);
		// Allow for fixed-point rounding across two conversions (strictly under 2^20).
		expect(static_cast<int64_t>(restored)).to_be_within((1 << 20) - 1).of(oldValue);
	});

	it("saturates rather than wrapping near the top", _ {
		// 0x50000000 sits in interval 13; intervals 13-15 span only 3.6 dB, so +6 dB has nowhere to go.
		expect(shiftVolumeByDB(0x50000000, 6.0f)).to_equal(std::numeric_limits<int32_t>::max());
	});
});

DELUGE_SPEC(db_shift_spec, db_shift_group)
