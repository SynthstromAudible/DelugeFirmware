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

// The float ladder's saturator: odd, monotone, bounded, unity slope at 0, landing on ±1 at its knee with no slope, and
// lane for lane equal to its scalar form.

#include "dsp-next/core/flush_to_zero.h"
#include "dsp-next/core/pack_f32.h"
#include "dsp-next/float/math/detail.h"

#include <array>
#include <bit>
#include <cmath>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>

using namespace deluge::dsp;

namespace {

[[nodiscard]] uint32_t bits(f32 x) {
	return std::bit_cast<uint32_t>(x);
}

} // namespace

// clang-format off
describe float_ladder_spec("the float ladder", ${
	context("soft_clip", _ {
		it("is odd, monotone and bounded by 1", _ {
			fp::FlushToZero ftz;
			f32 last = -2.F;
			for (int i = -5000; i <= 5000; ++i) {
				const f32 x = static_cast<f32>(i) * 1e-3F;
				const f32 y = fp::detail::soft_clip(x);
				expect(bits(fp::detail::soft_clip(-x))).to_equal(bits(-y));
				// Monotone to within one rounding step where the curve is flat near the knee.
				expect(y >= last - 1.2e-7F).to_be_true();
				expect(std::fabs(y) <= 1.F).to_be_true();
				last = y;
			}
		});
		it("passes small signals with unity slope", _ {
			fp::FlushToZero ftz;
			for (const f32 x : {1e-6F, 1e-4F, 1e-3F}) {
				expect(std::fabs(fp::detail::soft_clip(x) / x - 1.F) < 1e-5F).to_be_true();
			}
			expect(fp::detail::soft_clip_slope(0.F)).to_equal(1.F);
		});
		it("lands on ±1 at the knee and beyond, with no slope", _ {
			fp::FlushToZero ftz;
			for (const f32 x : {fp::detail::kSoftClipLimit, 2.F, 10.F, 1e30F}) {
				expect(fp::detail::soft_clip(x)).to_equal(1.F);
				expect(fp::detail::soft_clip(-x)).to_equal(-1.F);
				expect(fp::detail::soft_clip_slope(x)).to_equal(0.F);
			}
			const f32 just_below = std::nextafter(fp::detail::kSoftClipLimit, 0.F);
			expect(std::fabs(fp::detail::soft_clip(just_below) - 1.F) < 1e-6F).to_be_true();
			expect(fp::detail::soft_clip_slope(just_below) < 1e-10F).to_be_true();
		});
		it("matches the closed form's slope inside the knee", _ {
			fp::FlushToZero ftz;
			for (int i = -180; i <= 180; ++i) {
				const f32 x = static_cast<f32>(i) * 0.01F;
				const double l2 = 1.875 * 1.875;
				const double t = 1.0 - (static_cast<double>(x) * static_cast<double>(x)) / l2;
				expect(std::fabs(static_cast<double>(fp::detail::soft_clip_slope(x)) - t * t) < 1e-6).to_be_true();
			}
		});
		it("packs lane by lane", _ {
			fp::FlushToZero ftz;
			const std::array<f32, 4> in{-5.F, -0.7F, 1.3F, 1.874F};
			Pack<f32, 4> x{};
			for (std::size_t l = 0; l < 4; ++l) {
				x.set(l, in[l]);
			}
			const Pack<f32, 4> y = fp::detail::soft_clip(x);
			const Pack<f32, 4> d = fp::detail::soft_clip_slope(x);
			for (std::size_t l = 0; l < 4; ++l) {
				expect(bits(y.get(l))).to_equal(bits(fp::detail::soft_clip(in[l])));
				expect(bits(d.get(l))).to_equal(bits(fp::detail::soft_clip_slope(in[l])));
			}
		});
	});
});
// clang-format on

CPPSPEC_MAIN(float_ladder_spec);
