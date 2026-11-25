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
#pragma once

// fp::exp2m1, curve_falling and curve_position: main's normalised envelope curve in float against its formula at every
// eighth of an octave, its ends, its inverse, and the exponential a lane of a Pack and a float compute alike.

#include "dsp/core/pack_f32.h"
#include "dsp/float/envelope/curve.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <numbers>

#include "support/matchers/value_matchers.h"
#include "support/spec_main.h"

using namespace deluge::dsp;

// As float_envelope_spec: the twin built with the device's float flags holds the kernels to IEEE order.
#pragma clang fp contract(off) reassociate(off) reciprocal(off)

namespace {
/// 1 at x = 0 and 0 at x = 1: (2^(-k·x) - 2^-k) / (1 - 2^-k).
[[nodiscard]] double formula(double k, double x) {
	return k == 0.0 ? 1.0 - x : std::expm1(k * std::numbers::ln2 * (1.0 - x)) / std::expm1(k * std::numbers::ln2);
}
} // namespace

// clang-format off
describe float_curve_spec_body("the envelope curve in float", ${
	it("follows its formula within 2^-20 at every eighth of an octave", _ {
		for (int32_t e = -100; e <= 100; ++e) {
			double worst = 0.0;
			for (int32_t i = 0; i <= 1024; ++i) {
				const double x = i / 1024.0;
				const f32 got = fp::curve_falling(static_cast<f32>(e) / 8.f, static_cast<f32>(x));
				worst = std::max(worst, std::abs(got - formula(e / 8.0, x)));
			}
			to_be_at_most(expect(worst), 0x1p-20);
		}
	});

	it("is exactly 1 at the start and 0 at the end, and stays there past them", _ {
		for (int32_t e = -100; e <= 100; ++e) {
			const f32 k = static_cast<f32>(e) / 8.f;
			for (const f32 x : {0.f, -1.f}) {
				expect(fp::curve_falling(k, x)).to_equal(1.f);
			}
			for (const f32 x : {1.f, 2.f}) {
				expect(fp::curve_falling(k, x)).to_equal(0.f);
			}
		}
	});

	it("finds where its rise reaches a level", _ {
		for (int32_t e = -100; e <= 100; ++e) {
			const f32 k = static_cast<f32>(e) / 8.f;
			for (int32_t i = 0; i <= 64; ++i) {
				const f32 level = static_cast<f32>(i) / 64.f;
				const fp::CurvePosition at = fp::curve_position(k, level);
				to_be_at_least(expect(at.phase), 0.f);
				to_be_at_most(expect(at.phase), 1.f);
				expect(1.0 - formula(k, static_cast<double>(at.phase) + at.rest)).to_be_within(0x1p-20).of(level);
			}
		}
	});

	it("finds a level's position exactly near the end it is flat at", _ {
		for (const f32 k : {12.5f, 8.f, 4.f}) {
			for (f32 close = 1e-2f; close > 1e-5f; close *= 0.3f) {
				// Above 0 octaves the rise is flat at its end, below 0 at its start
				const fp::CurvePosition end = fp::curve_position(k, 1.f - close);
				const double left = (1.0 - end.phase) - end.rest;
				expect(formula(k, 1.0 - left) / (1.0 - static_cast<double>(1.f - close))).to_be_within(0x1p-18).of(1.0);
				const fp::CurvePosition start = fp::curve_position(-k, close);
				expect((1.0 - formula(-k, start.phase)) / close).to_be_within(0x1p-18).of(1.0);
			}
		}
	});

	it("keeps the exponential accurate relative to itself near 0, and in every lane of a Pack", _ {
		for (f32 y = 1e-7f; y < 20.f; y *= 1.3f) {
			for (const f32 x : {y, -y}) {
				const double exact = std::expm1(static_cast<double>(x) * std::numbers::ln2);
				expect(fp::exp2m1(x) / exact).to_be_within(0x1p-21).of(1.0);
				const Pack<f32, 4> lanes = fp::exp2m1(Pack<f32, 4>{x});
				for (std::size_t l = 0; l < 4; ++l) {
					expect(std::bit_cast<uint32_t>(lanes.get(l))).to_equal(std::bit_cast<uint32_t>(fp::exp2m1(x)));
				}
			}
		}
		expect(fp::exp2m1(0.f)).to_equal(0.f);
	});
});
// clang-format on

DELUGE_SPEC(float_curve_spec, float_curve_spec_body)
