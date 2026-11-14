#include "dsp-next/fixed/fft/magnitude.h"

#include <algorithm>
#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <limits>

using deluge::dsp::fixed::fft::Complex;
using deluge::dsp::fixed::fft::magnitude;

namespace {
constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
constexpr int32_t kMin = std::numeric_limits<int32_t>::min();

/// How far the magnitude of (x, y) is from the exact hypotenuse, as a fraction of it.
double relative_error(int32_t x, int32_t y) {
	const double exact = std::hypot(static_cast<double>(x), static_cast<double>(y));
	return std::abs(magnitude({x, y}) - exact) / exact;
}
} // namespace

// clang-format off
describe fft_magnitude("an FFT bin's magnitude", ${
	it("is exact on an axis and for 3-4-5", _ {
		expect(magnitude({0, 0})).to_equal(0);
		expect(magnitude({12345, 0})).to_equal(12345);
		expect(magnitude({0, -12345})).to_equal(12345);
		expect(magnitude({3, 4})).to_equal(5);
	});

	it("is within 0.15% across angles and scales", _ {
		double worst = 0;
		for (int32_t scale : {1 << 10, 1 << 20, 1 << 28}) {
			for (int k = 1; k < 256; k++) {
				const double a = k * 3.14159265358979 / 512;
				const auto x = static_cast<int32_t>(scale * std::cos(a));
				const auto y = static_cast<int32_t>(scale * std::sin(a));
				worst = std::max(worst, relative_error(x, y));
			}
		}
		expect(worst < 0.0015).to_equal(true);
	});

	it("saturates rather than wrapping", _ {
		expect(magnitude({kMin, kMin})).to_equal(kMax);
		expect(magnitude({kMax, kMax})).to_equal(kMax);
		expect(magnitude({kMin, 0})).to_equal(kMax);
	});
});
// clang-format on

CPPSPEC_MAIN(fft_magnitude);
