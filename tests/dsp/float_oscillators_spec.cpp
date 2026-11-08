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
// fp::BlepOscillator on Mono: its spectrum (DC, harmonics, aliasing), its pulse duty, and its output pinned by digest
// (float_kernel_records.h).

#include "dsp-next/core/flush_to_zero.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/poly_kernel.h"
#include "dsp-next/float/osc/blep_oscillator.h"

#include "float_kernel_records.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

using namespace deluge::dsp;

// The spec's own float arithmetic rounds as written, as the kernels' does, so its twin built with the device's float
// flags (float_oscillators_unsafe_math_spec) holds the kernels to the same records. The headers above pin themselves.
#pragma clang fp contract(off) reassociate(off) reciprocal(off)

static_assert(poly_kernel<fp::BlepOscillator>);

namespace {

/// FNV-1a over the output's bits: the digest a record pins.
[[nodiscard]] uint32_t digest(std::span<const f32> out) {
	uint32_t h = 2166136261U;
	for (f32 x : out) {
		h = (h ^ std::bit_cast<uint32_t>(x)) * 16777619U;
	}
	return h;
}

/// Magnitude of the DFT bin at @p freq (fraction of fs) over @p x, normalised so a unit-amplitude sine reads 1.
[[nodiscard]] f32 bin(std::span<const f32> x, f32 freq) {
	double re = 0.0, im = 0.0;
	for (std::size_t i = 0; i < x.size(); ++i) {
		const double w = 2.0 * std::numbers::pi * freq * static_cast<double>(i);
		re += x[i] * std::cos(w);
		im -= x[i] * std::sin(w);
	}
	return static_cast<f32>(2.0 * std::hypot(re, im) / static_cast<double>(x.size()));
}

} // namespace

// clang-format off
describe float_oscillators_spec("float oscillators", ${
	fp::FlushToZero ftz;
	context("BlepOscillator", _ {
		it("is DC-free over whole cycles", _ {
			Mono<fp::BlepOscillator> osc;
			osc.configure({.frequency = 1.f / 64.f, .mode = fp::BlepOscillator::Mode::SAW});
			std::vector<f32> out(64 * 32);
			osc.render_block(fp::Buffer{out});
			double mean = 0.0;
			for (f32 y : out) { mean += y; }
			expect(std::fabs(mean / static_cast<double>(out.size())) < 1e-3).to_be_true();
		});
		it("has a saw's harmonics, 1/k less the residual's sinc-squared droop, at a high pitch", _ {
			// f0 = 0.09 fs: harmonics 1..5 lie below Nyquist. 0.09 = 9/100, so 100-sample windows hold whole cycles.
			// The two-sample residual shapes the spectrum as a triangle kernel does: by sinc²(k·f0).
			Mono<fp::BlepOscillator> osc;
			osc.configure({.frequency = 0.09f, .mode = fp::BlepOscillator::Mode::SAW});
			std::vector<f32> out(100 * 64);
			osc.render_block(fp::Buffer{out});
			for (int k = 1; k <= 5; ++k) {
				const f32 x = std::numbers::pi_v<f32> * 0.09f * static_cast<f32>(k);
				const f32 droop = (std::sin(x) / x) * (std::sin(x) / x);
				const f32 expected = droop * 2.f / (std::numbers::pi_v<f32> * static_cast<f32>(k));
				expect(std::fabs(bin(out, 0.09f * static_cast<f32>(k)) - expected) < 0.05f * expected).to_be_true();
			}
		});
		it("keeps aliased energy below the recorded bound", _ {
			// No harmonic of 0.09 fs lands on 0.39 fs below Nyquist; only the folded 29th and 71st do, so the bin measures
			// aliasing.
			Mono<fp::BlepOscillator> osc;
			osc.configure({.frequency = 0.09f, .mode = fp::BlepOscillator::Mode::SAW});
			std::vector<f32> out(100 * 64);
			osc.render_block(fp::Buffer{out});
			const f32 alias = bin(out, 0.39f);
			expect(alias < records::kBlepAliasBound * bin(out, 0.09f)).to_be_true();
		});
		it("holds a pulse's duty", _ {
			Mono<fp::BlepOscillator> osc;
			osc.configure({.frequency = 1.f / 200.f, .width = 0.25f, .mode = fp::BlepOscillator::Mode::PULSE});
			std::vector<f32> out(200 * 8);
			osc.render_block(fp::Buffer{out});
			const auto high = std::ranges::count_if(out, [](f32 y) { return y > 0.f; });
			expect(std::abs(static_cast<double>(high) / static_cast<double>(out.size()) - 0.25) < 0.01).to_be_true();
		});
		it("holds a pulse's mean and fundamental", _ {
			// f0 = 0.04 fs, width 0.3: 25-sample windows hold whole cycles.
			constexpr f32 kWidth = 0.3f;
			Mono<fp::BlepOscillator> osc;
			osc.configure({.frequency = 0.04f, .width = kWidth, .mode = fp::BlepOscillator::Mode::PULSE});
			std::vector<f32> out(25 * 64);
			osc.render_block(fp::Buffer{out});
			double mean = 0.0;
			for (f32 y : out) { mean += y; }
			expect(std::fabs(mean / static_cast<double>(out.size()) - (2.0 * kWidth - 1.0)) < 1e-3).to_be_true();
			const f32 x = std::numbers::pi_v<f32> * 0.04f;
			const f32 droop = (std::sin(x) / x) * (std::sin(x) / x);
			const f32 expected = droop * 4.f / std::numbers::pi_v<f32> * std::sin(std::numbers::pi_v<f32> * kWidth);
			expect(std::fabs(bin(out, 0.04f) - expected) < 0.05f * expected).to_be_true();
		});
		it("stays finite and bounded at the top of the frequency clamp", _ {
			// At 0.45 fs the residual windows [0, inc) and (1 - inc, 1) are each under half a cycle, so they never
			// overlap and the correction never doubles: |out| stays within 1 + the residual's peak.
			Mono<fp::BlepOscillator> osc;
			osc.configure({.frequency = 0.45f, .mode = fp::BlepOscillator::Mode::SAW});
			std::vector<f32> out(1000);
			osc.render_block(fp::Buffer{out});
			expect(std::ranges::all_of(out, [](f32 y) { return std::isfinite(y) && std::fabs(y) <= 1.5f; })).to_be_true();
		});
		it("matches its records", _ {
			Mono<fp::BlepOscillator> saw, pulse;
			saw.configure({.frequency = 0.013f, .mode = fp::BlepOscillator::Mode::SAW});
			pulse.configure({.frequency = 0.013f, .width = 0.3f, .mode = fp::BlepOscillator::Mode::PULSE});
			std::vector<f32> out(512);
			saw.render_block(fp::Buffer{out});
			expect(digest(out)).to_equal(records::kBlepSaw);
			pulse.render_block(fp::Buffer{out});
			expect(digest(out)).to_equal(records::kBlepPulse);
		});
	});
});
// clang-format on
CPPSPEC_MAIN(float_oscillators_spec);
