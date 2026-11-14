#include "model/drum/expression_combine.h"

#include "cppspec.hpp"
#include <cstdint>

// clang-format off
describe drum_expression("a drum's combined expression", ${
	it("is the two Q7 inputs' sum at Q15 while it fits", _ {
		expect(int32_t{combined_expression(0, 0)}).to_equal(0);
		expect(int32_t{combined_expression(127, 0)}).to_equal(127 << 8);
		expect(int32_t{combined_expression(-128, 0)}).to_equal(-(128 << 8));
		expect(int32_t{combined_expression(60, -20)}).to_equal(40 << 8);
	});
	it("holds at the 16-bit ends where the channel and finger levels sum past them", _ {
		// A full upward channel bend plus any upward finger bend, and the same downward
		expect(int32_t{combined_expression(127, 1)}).to_equal(INT16_MAX);
		expect(int32_t{combined_expression(127, 127)}).to_equal(INT16_MAX);
		expect(int32_t{combined_expression(-128, -1)}).to_equal(INT16_MIN);
		expect(int32_t{combined_expression(-128, -128)}).to_equal(INT16_MIN);
	});
});
// clang-format on

CPPSPEC_SPEC(drum_expression)
