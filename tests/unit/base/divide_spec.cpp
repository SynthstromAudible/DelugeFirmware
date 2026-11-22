// divide.h against the integer operators, which are exact on the host and on qemu. unsafe_math/ runs it again under
// the device's float flags.
#include "base/divide.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::base;

namespace {

constexpr int64_t k2Pow53 = int64_t{1} << 53;
constexpr int64_t kI32Max = std::numeric_limits<int32_t>::max();
constexpr int64_t kI32Min = std::numeric_limits<int32_t>::min();

// Constant-evaluated, so the float flags never apply to these: the twin (unsafe_math/) leaves them out, so that its
// run-time cases alone answer for the flags.
#ifndef DELUGE_SPEC_TWIN
static_assert(floor_div(-7, 2) == -4);
static_assert(ceil_div(-7, 2) == -3);
static_assert(trunc_div(-7, 2) == -3);
static_assert(divide(-7, 2) == Quotient{-3, -1});
static_assert(divide(7, -2) == Quotient{-3, 1});
static_assert(floor_div(7, -2) == -4);
static_assert(ceil_div(7, 2) == 4);
static_assert(floor_div(-8, 2) == -4);
static_assert(ceil_div(-8, 2) == -4);
static_assert(exact_div(12, -4) == std::optional<int64_t>{-3});
static_assert(!exact_div(13, 4));
static_assert(floor_div((int64_t{1} << 53) - 1, 3) == 3002399751580330);
static_assert(trunc_div(kI32Max + 1, 1) == kI32Max + 1);
static_assert(trunc_div(kI32Min, 3) == kI32Min / 3);
// A bar's numerator past 2^53 whose double quotient rounds up past a whole number: the fix-up step takes it back.
static_assert(trunc_div(36028794854703104, 2147483647) == 16777214);
// Past 2^50 the estimate is refined: INT64_MAX converts to 2^63, which is one past it.
static_assert(divide(INT64_MAX, 1) == Quotient{INT64_MAX, 0});
static_assert(divide(-INT64_MAX, 3) == Quotient{-INT64_MAX / 3, -INT64_MAX % 3});
#endif

/// @brief Bar numerators (multiples of 2^24, exact as doubles though past 2^53) whose double quotient rounds up to the
///        next whole number, so that only the fix-up step answers them.
constexpr std::array<std::array<int64_t, 2>, 4> kOvershoots{{
    {36028794854703104, 2147483647},
    {72057591856889856, 2147483647},
    {18014399600001024, 1073741825},
    {54043196652519424, 1073741825},
}};

/// @brief Every helper's answer for @p a / @p b against the operators'; a line for each that differs.
struct Checker {
	int64_t checked = 0;
	int64_t failures = 0;

	void check(int64_t a, int64_t b) {
		checked++;
		const int64_t q = a / b;
		const int64_t r = a % b;
		const int64_t floor = (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
		const int64_t ceil = (r != 0 && ((r < 0) == (b < 0))) ? q + 1 : q;
		const std::optional<int64_t> exact = r == 0 ? std::optional{q} : std::nullopt;
		const bool ok = divide(a, b) == Quotient{q, r} && trunc_div(a, b) == q && floor_div(a, b) == floor
		                && ceil_div(a, b) == ceil && exact_div(a, b) == exact;
		if (!ok) {
			if (failures < 10) {
				std::cout << "divide " << a << " / " << b << ": got " << divide(a, b).quotient << " r "
				          << divide(a, b).remainder << ", floor " << floor_div(a, b) << ", ceil " << ceil_div(a, b)
				          << "; want " << q << " r " << r << ", floor " << floor << ", ceil " << ceil << "\n";
			}
			failures++;
		}
	}
};

} // namespace

// clang-format off
describe divide_spec_body("divide.h", ${
	it("agrees with the operators on every small pair", _ {
		Checker c;
		for (int64_t a = -1000; a <= 1000; a++) {
			for (int64_t b = -64; b <= 64; b++) {
				if (b != 0) {
					c.check(a, b);
				}
			}
		}
		expect(c.failures).to_equal(0);
	});
	it("agrees just below, on and just above multiples, small and near 2^53", _ {
		Checker c;
		const std::vector<int64_t> divisors{1, 2, 3, 6, 8, 16, (int64_t{1} << 28) + 1, (int64_t{1} << 40) + 3};
		for (const int64_t base : divisors) {
			for (const int64_t b : {base, -base}) {
				const int64_t kmax = (k2Pow53 - 2) / base;
				std::vector<int64_t> ks;
				for (int64_t k = -4; k <= 4; k++) {
					ks.push_back(k);
				}
				for (int64_t k = kmax - 3; k <= kmax; k++) {
					ks.push_back(k);
					ks.push_back(-k);
				}
				for (const int64_t k : ks) {
					for (int64_t d = -1; d <= 1; d++) {
						const int64_t a = k * b + d;
						if (a > -k2Pow53 && a < k2Pow53) {
							c.check(a, b);
						}
					}
				}
			}
		}
		for (const int64_t a : {k2Pow53 - 1, -(k2Pow53 - 1), k2Pow53 - 2, -(k2Pow53 - 2)}) {
			for (const int64_t b : {1, -1, 3, -3}) {
				c.check(a, b);
			}
		}
		expect(c.failures).to_equal(0);
	});
	it("agrees across the 32-bit fast path's edge", _ {
		Checker c;
		std::vector<int64_t> edge;
		for (const int64_t v : {kI32Max - 1, kI32Max, kI32Max + 1, int64_t{1} << 32}) {
			edge.push_back(v);
			edge.push_back(-v);
		}
		edge.push_back(kI32Min);
		for (const int64_t a : edge) {
			for (const int64_t b : edge) {
				c.check(a, b);
			}
			for (const int64_t b : {1, 3, 16}) {
				c.check(a, b);
				c.check(b, a);
			}
		}
		expect(c.failures).to_equal(0);
	});
	it("agrees on the bars' numerators wherever the bar fits 32 bits", _ {
		Checker c;
		const std::vector<int64_t> ds{0, 1, -1, kI32Max, -kI32Max, int64_t{1} << 31, -(int64_t{1} << 31),
		                              (int64_t{1} << 32) - 1, -((int64_t{1} << 32) - 1)};
		const std::vector<int64_t> spans{1, 3, int64_t{1} << 24, (int64_t{1} << 28) - 1, kI32Max};
		for (const int64_t d : ds) {
			for (const int64_t span : spans) {
				const int64_t a = d * (int64_t{1} << 24);
				const int64_t q = a / span;
				if (q > kI32Min && q < kI32Max) {
					c.check(a, span);
				}
			}
		}
		expect(c.failures).to_equal(0);
	});
	it("agrees where the double quotient rounds up past a whole number", _ {
		Checker c;
		for (const auto& [a, b] : kOvershoots) {
			expect(static_cast<int64_t>(static_cast<double>(a) / static_cast<double>(b))).to_equal(a / b + 1);
			c.check(a, b);
			c.check(-a, b);
			c.check(a, -b);
			c.check(-a, -b);
		}
		expect(c.failures).to_equal(0);
	});
	it("agrees on the column read's frame offsets", _ {
		Checker c;
		for (const int64_t f : {1, 2, 3, 4, 6, 8}) {
			for (const int64_t n : {int64_t{0}, int64_t{1}, int64_t{1} << 31, (int64_t{1} << 40) - 1}) {
				c.check(n * f, f);
				for (int64_t r = 0; r < f; r++) {
					c.check(n * f + r, f);
				}
			}
		}
		expect(c.failures).to_equal(0);
	});
	it("agrees at the ends of the int64_t range", _ {
		Checker c;
		constexpr int64_t kMax = std::numeric_limits<int64_t>::max();
		for (const int64_t a : {kMax, kMax - 1, kMax - 2, kMax >> 1, int64_t{1} << 62, k2Pow53 + 1}) {
			for (const int64_t b : {int64_t{1}, int64_t{2}, int64_t{3}, int64_t{7}, (int64_t{1} << 13) - 1,
			                        int64_t{1} << 13, kI32Max, kI32Max + 2, (int64_t{1} << 53) + 1, kMax - 1, kMax}) {
				c.check(a, b);
				c.check(-a, b);
				c.check(a, -b);
				c.check(-a, -b);
			}
		}
		expect(c.failures).to_equal(0);
	});
	it("agrees on 2^20 random pairs below 2^53", _ {
		Checker c;
		std::mt19937_64 rng{0x5a3d'1f00'c0de'2026};
		std::uniform_real_distribution<double> exponent{0.0, 53.0};
		const auto draw = [&] {
			const auto magnitude = static_cast<int64_t>(std::exp2(exponent(rng)));
			const int64_t v = std::max<int64_t>(1, std::min(magnitude, k2Pow53 - 1));
			return (rng() & 1) != 0 ? v : -v;
		};
		int64_t kept = 0;
		while (kept < (int64_t{1} << 20)) {
			const int64_t a = draw();
			const int64_t b = draw();
			if (a / b > -(int64_t{1} << 52) && a / b < (int64_t{1} << 52)) {
				c.check(a, b);
				kept++;
			}
		}
		expect(c.failures).to_equal(0);
	});
	it("agrees on 2^20 random pairs with a dividend past 2^53, whatever the quotient", _ {
		Checker c;
		std::mt19937_64 rng{0x2026'1121'5eed'0053};
		std::uniform_real_distribution<double> dividend{53.0, 63.0};
		std::uniform_real_distribution<double> divisor{0.0, 63.0};
		const auto draw = [&](std::uniform_real_distribution<double>& exponent) {
			const double m = std::exp2(exponent(rng));
			// Below 2^63 a double converts exactly; the low bits are then filled at random.
			const auto high = static_cast<int64_t>(std::min(m, 9.2e18));
			const int64_t v = std::max<int64_t>(1, high ^ static_cast<int64_t>(rng() & 0xFFFF));
			return (rng() & 1) != 0 ? v : -v;
		};
		for (int64_t n = 0; n < (int64_t{1} << 20); n++) {
			c.check(draw(dividend), draw(divisor));
		}
		expect(c.failures).to_equal(0);
	});
	it("agrees on many numerators over one divisor known only at run time", _ {
		// Shaped like a column read's frame offsets: one zoom, read at run time, divides a run of frames. A compiler
		// allowed reciprocal math may hoist 1 / zoom out of the loop and multiply; the fix-up step must still answer.
		Checker c;
		volatile int64_t laundered = 0;
		for (const int64_t zoom : {int64_t{3}, int64_t{7}, int64_t{49}, int64_t{1000003}, (int64_t{1} << 28) + 1}) {
			laundered = zoom;
			const int64_t b = laundered;
			std::vector<int64_t> frames;
			for (int64_t k = 0; k < 4096; k++) {
				laundered = k * zoom + (k % 3) - 1;
				const int64_t frame = laundered;
				frames.push_back(frame);
			}
			std::vector<int64_t> quotients(frames.size());
			for (size_t i = 0; i < frames.size(); i++) {
				quotients[i] = trunc_div(frames[i], b);
			}
			for (size_t i = 0; i < frames.size(); i++) {
				c.checked++;
				if (quotients[i] != frames[i] / b) {
					if (c.failures < 10) {
						std::cout << "frame " << frames[i] << " / " << b << ": got " << quotients[i] << "\n";
					}
					c.failures++;
				}
				c.check(frames[i], b);
			}
		}
		expect(c.failures).to_equal(0);
	});
});
// clang-format on

DELUGE_SPEC(divide_spec, divide_spec_body)
