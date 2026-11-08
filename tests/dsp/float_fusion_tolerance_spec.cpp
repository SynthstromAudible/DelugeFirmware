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
// The fused policy against the unfused one: fp::Svf and fp::Smoother built with -mfma (mul_add fused) against
// test-local twins of their steps with every mul_add written unfused. The A9 runs the unfused form, an FMA target
// the fused one; their difference stays within the bounds here.

#include "dsp-next/core/flush_to_zero.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/float/filter/smoother.h"
#include "dsp-next/float/filter/svf.h"

#include <algorithm>
#include <cmath>
#include <cppspec.hpp>
#include <random>
#include <vector>

using namespace deluge::dsp;

static_assert(kFusedMulAdd, "the tolerance spec builds with -mfma");

namespace {

/// fp::Svf's low-pass step with each mul_add unfused.
[[nodiscard]] f32 svf_unfused(const fp::Svf::Coefficients& c, fp::Svf::State& s, f32 x) {
#pragma clang fp contract(off) reassociate(off)
	const f32 v3 = x - s.ic2;
	const f32 v1 = (c.a1 * s.ic1) + (c.a2 * v3);
	const f32 v2 = (s.ic2 + (c.a2 * s.ic1)) + (c.a3 * v3);
	s.ic1 = (v1 + v1) - s.ic1;
	s.ic2 = (v2 + v2) - s.ic2;
	return v2;
}

/// fp::Smoother's EXPONENTIAL step with its mul_add unfused.
[[nodiscard]] f32 smoother_unfused(const fp::Smoother::Coefficients& c, fp::Smoother::State& s) {
#pragma clang fp contract(off) reassociate(off)
	s.y = c.rate >= 1.f ? c.target : s.y + (c.rate * (c.target - s.y));
	return s.y;
}

} // namespace

// clang-format off
describe float_fusion_tolerance_spec("fused against unfused", ${
	it("keeps the SVF low-pass within 1e-5 of the unfused form over 64 blocks at Q 10", _ {
		fp::FlushToZero ftz;
		const fp::Svf::Settings set{.cutoff = 0.005f, .q = 10.f, .mode = fp::Svf::Mode::LOW_PASS};
		Mono<fp::Svf> fused;
		fused.configure(set);
		const fp::Svf::Coefficients c = fp::Svf::coefficients(set);
		fp::Svf::State s{};
		std::mt19937 rng{21};
		std::uniform_real_distribution<f32> d{-1.f, 1.f};
		std::vector<f32> in(128 * 64), out(in.size());
		for (f32& x : in) {
			x = d(rng);
		}
		fused.render_block(fp::Signal{in}, fp::Buffer{out});
		f32 worst = 0.f;
		for (std::size_t i = 0; i < in.size(); ++i) {
			worst = std::max(worst, std::fabs(out[i] - svf_unfused(c, s, in[i])));
		}
		expect(worst < 1e-5f).to_be_true();
	});
	it("keeps the Smoother's exponential approach within 1e-6 of the unfused form over 64 blocks", _ {
		fp::FlushToZero ftz;
		const fp::Smoother::Settings set{.target = 0.8f, .time = 37.3f, .mode = fp::Smoother::Mode::EXPONENTIAL};
		Mono<fp::Smoother> fused;
		fused.configure(set);
		const fp::Smoother::Coefficients c = fp::Smoother::coefficients(set);
		fp::Smoother::State s{};
		std::vector<f32> in(128 * 64), out(in.size());
		fused.render_block(fp::Signal{in}, fp::Buffer{out});
		f32 worst = 0.f;
		for (std::size_t i = 0; i < in.size(); ++i) {
			worst = std::max(worst, std::fabs(out[i] - smoother_unfused(c, s)));
		}
		expect(worst < 1e-6f).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(float_fusion_tolerance_spec);
