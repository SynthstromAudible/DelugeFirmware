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

// fp::Curve: the normalised envelope curve against its formula, the Deluge's decay tables, and its shape from the one
// curve control.

#include "dsp/core/pack_f32.h"
#include "dsp/fixed/tables/decay_small.h"
#include "dsp/float/envelope/curve.h"

#include <array>
#include <cmath>
#include <cppspec.hpp>
#include <cstdint>

#include "support/matchers/value_matchers.h"
#include "support/spec_main.h"

using namespace deluge::dsp;

namespace {

constexpr std::array kKs{-12.5f, -2.5f, 0.f, 0.125f, 4.f, 8.f, 12.5f};

/// The formula in double: (2^(-k·x) - 2^-k) / (1 - 2^-k), and 1 - x at k = 0.
[[nodiscard]] double formula(double k, double x) {
	if (k == 0.0) {
		return 1.0 - x;
	}
	return (std::exp2(-k * x) - std::exp2(-k)) / (1.0 - std::exp2(-k));
}

/// The largest |falling - formula| over 1,001 evenly spaced x.
[[nodiscard]] double worst_against_formula(float k) {
	const fp::Curve c{k};
	double worst = 0.0;
	for (int32_t i = 0; i <= 1000; ++i) {
		const double x = i / 1000.0;
		worst = std::max(worst, std::abs(c.falling(static_cast<f32>(x)) - formula(k, static_cast<f32>(x))));
	}
	return worst;
}

/// The largest |falling - table| at the table's 257 points.
[[nodiscard]] double worst_against_table(float k, const std::array<uint16_t, 257>& table) {
	const fp::Curve c{k};
	double worst = 0.0;
	for (size_t i = 0; i < table.size(); ++i) {
		const double level = table[i] / 65535.0;
		worst = std::max(worst, std::abs(c.falling(static_cast<f32>(i) / 256.f) - level));
	}
	return worst;
}

} // namespace

// clang-format off
describe envelope_curve_spec_body("fp::Curve", ${
	it("is exactly 1 at x = 0 and 0 at x = 1 for every k", _ {
		for (float k : kKs) {
			const fp::Curve c{k};
			expect(c.falling(0.f)).to_equal(1.f);
			expect(c.falling(1.f)).to_equal(0.f);
		}
	});

	it("follows its formula within 2^-20", _ {
		for (float k : kKs) {
			to_be_at_most(expect(worst_against_formula(k)), 0x1p-20);
		}
	});

	it("rises as 1 - falling", _ {
		for (float k : kKs) {
			const fp::Curve c{k};
			for (int32_t i = 0; i <= 256; ++i) {
				const f32 x = static_cast<f32>(i) / 256.f;
				expect(c.rising(x)).to_be_within(0x1p-23f).of(1.f - c.falling(x));
			}
		}
	});

	it("inverts its rise within 2^-18", _ {
		for (float k : kKs) {
			const fp::Curve c{k};
			for (int32_t i = 0; i <= 256; ++i) {
				const f32 v = std::max(static_cast<f32>(i) / 256.f, 0x1p-31f);
				expect(c.rising(c.rising_inverse(v))).to_be_within(0x1p-18f).of(v);
			}
		}
	});

	it("takes decay and release k = curve / 4 and attack half that", _ {
		const fp::CurveShape classic = fp::CurveShape::from_curve(fp::kClassicCurve);
		expect(classic.attack.k).to_equal(4.f);
		expect(classic.decay.k).to_equal(8.f);
		expect(classic.release.k).to_equal(8.f);
		const fp::CurveShape line = fp::CurveShape::from_curve(0);
		expect(line.attack.k).to_equal(0.f);
		expect(line.decay.k).to_equal(0.f);
		const fp::CurveShape least = fp::CurveShape::from_curve(fp::kMinCurve);
		expect(least.attack.k).to_equal(-6.25f);
		expect(least.decay.k).to_equal(-12.5f);
		expect(least.release.k).to_equal(-12.5f);
		expect(fp::kAudioClipShape.release.k).to_equal(4.f);
		expect(fp::kAudioClipShape.decay.k).to_equal(8.f);
	});

	it("is the Deluge's decay tables at k = 8 and k = 4, within their rounding (k = 4's worst is 8.33e-6)", _ {
		to_be_at_most(expect(worst_against_table(8.f, fixed::tables::kDecaySmall8)), 9e-6);
		to_be_at_most(expect(worst_against_table(4.f, fixed::tables::kDecaySmall4)), 9e-6);
	});

	it("falls over a Pack lane by lane as the scalar does", _ {
		const Pack<f32, 4> k = Pack<f32, 4>::load(std::array{-12.5f, 0.f, 4.f, 8.f}.data());
		const Pack<f32, 4> x = Pack<f32, 4>::load(std::array{0.1f, 0.5f, 0.25f, 0.9f}.data());
		const Pack<f32, 4> out = fp::curve_falling(k, x);
		for (size_t l = 0; l < 4; ++l) {
			expect(out.get(l)).to_equal(fp::curve_falling(k.get(l), x.get(l)));
		}
	});
});
// clang-format on

DELUGE_SPEC(envelope_curve_spec, envelope_curve_spec_body)
