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
// The float kernels on Mono: their responses, their resets, and their outputs pinned by digest, so that the unfused
// host and the A9 under qemu are held to one record (float_kernel_records.h), and the fused build to its own.
// Linked runs each lane exactly as Mono does.

#include "dsp-next/core/flush_to_zero.h"
#include "dsp-next/core/linked.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/poly_kernel.h"
#include "dsp-next/float/filter/one_pole.h"
#include "dsp-next/float/filter/smoother.h"

#include "float_kernel_records.h"

#include <array>
#include <bit>
#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <numbers>
#include <random>
#include <vector>

using namespace deluge::dsp;

namespace {

constexpr std::size_t kBlock = 128;

/// FNV-1a over the output's bits: the digest a record pins.
[[nodiscard]] uint32_t digest(std::span<const f32> out) {
	uint32_t h = 2166136261U;
	for (f32 x : out) {
		h = (h ^ std::bit_cast<uint32_t>(x)) * 16777619U;
	}
	return h;
}

/// Seeded noise over ±1.
[[nodiscard]] std::vector<f32> noise(std::size_t n, uint32_t seed) {
	std::mt19937 rng{seed};
	std::uniform_real_distribution<f32> d{-1.f, 1.f};
	std::vector<f32> v(n);
	for (f32& x : v) {
		x = d(rng);
	}
	return v;
}

/// The steady-state gain of a sine at @p freq (fraction of fs) through @p mono, after 8 blocks to settle.
template <class M>
[[nodiscard]] f32 sine_gain(M& mono, f32 freq) {
	std::vector<f32> in(kBlock * 16), out(kBlock * 16);
	for (std::size_t i = 0; i < in.size(); ++i) {
		in[i] = std::sin(2.f * std::numbers::pi_v<f32> * freq * static_cast<f32>(i));
	}
	mono.render_block(fp::Signal{in}, fp::Buffer{out});
	f32 peak = 0.f;
	for (std::size_t i = kBlock * 8; i < out.size(); ++i) {
		peak = std::max(peak, std::fabs(out[i]));
	}
	return peak;
}

} // namespace

static_assert(poly_kernel<fp::OnePole> && poly_kernel<fp::Smoother>);

// clang-format off
describe float_kernels_spec("float kernels", ${
	context("OnePole", _ {
		it("low-passes to -3 dB at its cutoff and passes DC", _ {
			fp::FlushToZero ftz;
			Mono<fp::OnePole> lp;
			lp.configure({.cutoff = 0.01f, .mode = fp::OnePole::Mode::LOW_PASS});
			expect(std::fabs(sine_gain(lp, 0.01f) - std::numbers::sqrt2_v<f32> / 2.f) < 0.01f).to_be_true();
			lp.reset();
			std::vector<f32> dc(kBlock * 64, 0.5f), out(dc.size());
			lp.render_block(fp::Signal{dc}, fp::Buffer{out});
			expect(std::fabs(out.back() - 0.5f) < 1e-5f).to_be_true();
		});
		it("high-passes as the input less the low-pass", _ {
			fp::FlushToZero ftz;
			Mono<fp::OnePole> lp, hp;
			lp.configure({.cutoff = 0.05f, .mode = fp::OnePole::Mode::LOW_PASS});
			hp.configure({.cutoff = 0.05f, .mode = fp::OnePole::Mode::HIGH_PASS});
			const auto in = noise(kBlock, 1);
			std::vector<f32> l(kBlock), h(kBlock);
			lp.render_block(fp::Signal{in}, fp::Buffer{l});
			hp.render_block(fp::Signal{in}, fp::Buffer{h});
			for (std::size_t i = 0; i < kBlock; ++i) {
				expect(std::bit_cast<uint32_t>(h[i])).to_equal(std::bit_cast<uint32_t>(in[i] - l[i]));
			}
		});
		it("matches its record", _ {
			fp::FlushToZero ftz;
			Mono<fp::OnePole> lp;
			lp.configure({.cutoff = 0.02f, .mode = fp::OnePole::Mode::LOW_PASS});
			const auto in = noise(kBlock * 4, 2);
			std::vector<f32> out(in.size());
			lp.render_block(fp::Signal{in}, fp::Buffer{out});
			expect(digest(out)).to_equal(records::kOnePoleLowPass);
		});
	});
	context("Smoother", _ {
		it("lands exactly on the target at the block's end, LINEAR", _ {
			fp::FlushToZero ftz;
			Mono<fp::Smoother> s;
			s.configure({.target = 0.75f, .time = 0.f, .block = static_cast<f32>(kBlock), .mode = fp::Smoother::Mode::LINEAR});
			std::vector<f32> unused(kBlock, 0.f), out(kBlock);
			s.render_block(fp::Signal{unused}, fp::Buffer{out});
			expect(out.back()).to_equal(0.75f);
			expect(out.front() > 0.f && out.front() < 0.75f).to_be_true();
		});
		it("reaches 1 - 1/e of a step in its time constant, EXPONENTIAL", _ {
			fp::FlushToZero ftz;
			Mono<fp::Smoother> s;
			s.configure({.target = 1.f, .time = 100.f, .block = static_cast<f32>(kBlock), .mode = fp::Smoother::Mode::EXPONENTIAL});
			std::vector<f32> unused(kBlock, 0.f), out(kBlock);
			s.render_block(fp::Signal{unused}, fp::Buffer{out});
			expect(std::fabs(out[99] - (1.f - std::exp(-1.f))) < 1e-3f).to_be_true();
		});
		it("matches its record", _ {
			fp::FlushToZero ftz;
			Mono<fp::Smoother> s;
			std::vector<f32> unused(kBlock, 0.f), out(kBlock * 4);
			for (std::size_t b = 0; b < 4; ++b) {
				s.configure({.target = (b % 2 == 0) ? 0.9f : -0.3f, .time = 37.f, .block = static_cast<f32>(kBlock),
				             .mode = fp::Smoother::Mode::EXPONENTIAL});
				s.render_block(fp::Signal{unused}, fp::Buffer{out}.subspan(b * kBlock, kBlock));
			}
			expect(digest(out)).to_equal(records::kSmootherExponential);
		});
	});
	context("Linked", _ {
		it("runs each lane exactly as Mono", _ {
			fp::FlushToZero ftz;
			Linked<fp::OnePole, 2, Frame<f32, 2>> two;
			Mono<fp::OnePole> one;
			const fp::OnePole::Settings set{.cutoff = 0.03f, .mode = fp::OnePole::Mode::LOW_PASS};
			two.configure(set);
			one.configure(set);
			const auto in = noise(kBlock * 2, 3);
			std::vector<Frame<f32, 2>> frames(kBlock), framed_out(kBlock);
			std::vector<f32> left(kBlock), left_out(kBlock);
			for (std::size_t i = 0; i < kBlock; ++i) {
				frames[i].lane[0] = in[2 * i];
				frames[i].lane[1] = in[(2 * i) + 1];
				left[i] = in[2 * i];
			}
			two.render_block(std::span<const Frame<f32, 2>>{frames}, std::span<Frame<f32, 2>>{framed_out});
			one.render_block(fp::Signal{left}, fp::Buffer{left_out});
			for (std::size_t i = 0; i < kBlock; ++i) {
				expect(std::bit_cast<uint32_t>(framed_out[i].lane[0])).to_equal(std::bit_cast<uint32_t>(left_out[i]));
			}
		});
	});
});
// clang-format on

CPPSPEC_MAIN(float_kernels_spec);
