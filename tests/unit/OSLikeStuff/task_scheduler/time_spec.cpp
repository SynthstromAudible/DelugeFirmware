#include "OSLikeStuff/task_scheduler/clock_type.h"

#include <cmath>
#include <cstdint>

#include "cppspec.hpp"
#include "support/spec_main.h"

// clang-format off
describe clock_group("clock", ${
	it("double conversion", _ {
		Time one = 0.0;
		Time two = Time(0, 0);
		expect(double(two)).to_equal(double(one));
	});

	it("double double conversion", _ {
		Time one = 123.5;
		expect((double)one).to_equal(123.5);
	});

	it("convert with rolls", _ {
		double base = 1234.123456; // 9 clock rollovers
		Time one = base;
		// Strictly closer than 0.00001: the largest tolerance below it, inclusive, is the same bound.
		expect((double)one).to_be_within(std::nextafter(0.00001, 0.0)).of(base);
	});
});

DELUGE_SPEC(time_spec, clock_group)
