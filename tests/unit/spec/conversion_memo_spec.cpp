// A host chain's remembered conversions: each distinct input is converted once, and a repeat gives back what the last
// conversion gave.

#include "model/fx/conversion_memo.h"

#include "cppspec.hpp"
#include <cstdint>

using deluge::model::ConversionMemo;

// clang-format off
describe conversion_memo("deluge::model::ConversionMemo", ${
	it("converts each new input, and gives a repeated one back without converting", _ {
		ConversionMemo<int32_t, int32_t> memo;
		int32_t calls = 0;
		const auto twice = [&](int32_t in) {
			calls++;
			return in * 2;
		};
		expect(memo(3, twice)).to_equal(6);
		expect(memo(3, twice)).to_equal(6);
		expect(calls).to_equal(1);
		expect(memo(4, twice)).to_equal(8);
		expect(calls).to_equal(2);
		expect(memo(3, twice)).to_equal(6);
		expect(calls).to_equal(3);
	});
	it("converts the first input, whatever it is", _ {
		ConversionMemo<int32_t, int32_t> memo;
		expect(memo(0, [](int32_t) { return 7; })).to_equal(7);
	});
});
// clang-format on

CPPSPEC_SPEC(conversion_memo)
