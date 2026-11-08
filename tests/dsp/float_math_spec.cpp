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
// The libm-free coefficient math (float/math/detail.h) against the library, over dense sweeps of its domains.

#include "dsp-next/float/math/detail.h"

#include <algorithm>
#include <cmath>
#include <cppspec.hpp>
#include <numbers>

using namespace deluge::dsp;

namespace {
/// The largest relative error of exp_negative against std::exp(-x) for x = lo, lo*ratio, ... below hi.
double worst_exp_error(double lo, double hi, double ratio) {
	double worst = 0.0;
	for (double x = lo; x < hi; x *= ratio) {
		const double want = std::exp(-x);
		worst = std::max(worst, std::fabs(fp::detail::exp_negative(x) - want) / want);
	}
	return worst;
}
} // namespace

// clang-format off
describe float_math_spec("float math", ${
	context("exp_negative", _ {
		it("matches std::exp within 1e-14 relative on (0, 1]", _ {
			expect(worst_exp_error(1e-9, 1.0, 1.0007) < 1e-14).to_be_true();
			expect(std::fabs(fp::detail::exp_negative(1.0) - std::exp(-1.0)) / std::exp(-1.0) < 1e-14).to_be_true();
		});
		it("matches std::exp within 1e-14 relative on (1, 700)", _ {
			expect(worst_exp_error(1.0000001, 699.9999, 1.0007) < 1e-14).to_be_true();
		});
		it("is exactly 1 at 0 and 0 from 700", _ {
			expect(fp::detail::exp_negative(0.0)).to_equal(1.0);
			expect(fp::detail::exp_negative(700.0)).to_equal(0.0);
			expect(fp::detail::exp_negative(1e30)).to_equal(0.0);
		});
		it("serves every time a Smoother can hold, down to the smallest positive f32", _ {
			for (double time = 1e-30; time < 1e6; time *= 1.31) {
				const double want = 1.0 - std::exp(-1.0 / time);
				expect(std::fabs((1.0 - fp::detail::exp_negative(1.0 / time)) - want) < 1e-14).to_be_true();
			}
		});
	});
	context("tan_pi", _ {
		it("matches std::tan within 1e-14 relative on (0, 0.49]", _ {
			double worst = 0.0;
			for (double fc = 1.0 / 48000.0; fc <= 0.49; fc += 1.0 / 48000.0 / 7.0) {
				const double want = std::tan(std::numbers::pi * fc);
				worst = std::max(worst, std::fabs(fp::detail::tan_pi(fc) - want) / want);
			}
			for (double fc = 1e-9; fc <= 0.49; fc *= 1.003) {
				const double want = std::tan(std::numbers::pi * fc);
				worst = std::max(worst, std::fabs(fp::detail::tan_pi(fc) - want) / want);
			}
			expect(worst < 1e-14).to_be_true();
		});
		it("matches at the ends of its domain", _ {
			for (const double fc : {0.25, 0.49, static_cast<double>(0.49f), 0.2500001, 0.2499999}) {
				const double want = std::tan(std::numbers::pi * fc);
				expect(std::fabs(fp::detail::tan_pi(fc) - want) / want < 1e-14).to_be_true();
			}
		});
	});
});
// clang-format on
CPPSPEC_MAIN(float_math_spec);
