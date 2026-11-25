/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

// fixed::EnvelopeCurve: main's normalised envelope curve in integers, against its formula at every eighth of an octave
// the curve control gives, its ends, its order and its inverse; and the exponential under it.

#include "dsp/fixed/envelope/curve.h"

#include <algorithm>
#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <numbers>

#include "support/matchers/value_matchers.h"
#include "support/spec_main.h"

using namespace deluge::dsp;
using fixed::EnvelopeCurve;
using fixed::kEnvelopeStageEnd;

namespace {

/// The formula in double: (2^(-k·x) - 2^-k) / (1 - 2^-k), and 1 - x at k = 0.
[[nodiscard]] double formula(double k, double x) {
	return k == 0.0 ? 1.0 - x : (std::exp2(-k * x) - std::exp2(-k)) / (1.0 - std::exp2(-k));
}
[[nodiscard]] double level(Q<31> word) {
	return word.raw() / 2147483648.0;
}
/// Every position near an end, and every 997th between.
template <typename Visit>
void each_position(Visit visit) {
	for (uint32_t p = 0; p <= kEnvelopeStageEnd; p += (p < 3000 || p > kEnvelopeStageEnd - 3000) ? 1 : 997) {
		visit(p);
	}
}

} // namespace

// clang-format off
describe fixed_envelope_curve_spec_body("fixed::EnvelopeCurve", ${
	it("follows its formula within 2^-20 at every eighth of an octave", _ {
		for (int32_t e = -100; e <= 100; ++e) {
			const EnvelopeCurve curve{static_cast<int8_t>(e)};
			double worst = 0.0;
			each_position([&](uint32_t p) {
				worst = std::max(worst, std::abs(level(curve.falling(p)) - formula(e / 8.0, p / 8388608.0)));
			});
			to_be_at_most(expect(worst), 0x1p-20);
		}
	});

	it("is full scale at the start and 0 at the end, exactly, for every curve", _ {
		for (int32_t e = -100; e <= 100; ++e) {
			const EnvelopeCurve curve{static_cast<int8_t>(e)};
			expect(curve.falling(0)).to_equal(Q<31>::max());
			expect(curve.falling(kEnvelopeStageEnd)).to_equal(Q<31>{});
		}
	});

	it("never rises", _ {
		for (int32_t e : {-100, -32, -1, 0, 1, 32, 100}) {
			const EnvelopeCurve curve{static_cast<int8_t>(e)};
			int32_t rises = 0;
			Q<31> last = Q<31>::max();
			for (uint32_t p = 0; p <= kEnvelopeStageEnd; p += 3) {
				const Q<31> now = curve.falling(p);
				// A last bit of the exponential's rounding, scaled by the gentlest curve's 12
				rises += level(now) - level(last) > 0x1p-24 ? 1 : 0;
				last = now;
			}
			expect(rises).to_equal(0);
		}
	});

	it("finds where its rise reaches a level", _ {
		for (int32_t e = -100; e <= 100; ++e) {
			const EnvelopeCurve curve{static_cast<int8_t>(e)};
			for (int32_t i = 0; i <= 64; ++i) {
				const double want = std::min(i / 64.0, 1.0 - 0x1p-31);
				const uint32_t at = curve.position_of(Q<31>::from_raw(static_cast<int32_t>(want * 2147483648.0)));
				to_be_at_most(expect(at), kEnvelopeStageEnd);
				// A position is 2^-23 of the stage; the steepest curve moves 2^-19.9 of full scale in one
				expect(1.0 - level(curve.falling(at))).to_be_within(0x1p-18).of(want);
			}
		}
	});

	it("keeps the exponential's slope at 0, which a slew applies thousands of times", _ {
		for (uint32_t y = 1U << 6; y < (1U << 22); y = y + (y >> 2)) { // 2^-20 of an octave to 2^-4
			const double exact = -std::expm1(-(y / 67108864.0) * std::numbers::ln2);
			const double got = 1.0 - (fixed::exp2_neg(y).raw() / 2147483647.0);
			expect(got / exact).to_be_within(0.01).of(1.0);
		}
		expect(fixed::exp2_neg(0)).to_equal(Q<31>::max());
		expect(fixed::exp2_neg(31U << 26).raw()).to_equal(0);
	});
});
// clang-format on

DELUGE_SPEC(fixed_envelope_curve_spec, fixed_envelope_curve_spec_body)
