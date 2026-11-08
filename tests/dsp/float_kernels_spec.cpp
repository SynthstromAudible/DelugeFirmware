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
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/float/filter/one_pole.h"
#include "dsp-next/float/filter/smoother.h"
#include "dsp-next/float/filter/svf.h"

#include "float_kernel_records.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <numbers>
#include <random>
#include <tuple>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;

// The spec's own float arithmetic rounds as written, as the kernels' does, so its twin built with the device's float
// flags (float_kernels_unsafe_math_spec) holds the kernels to the same records. The headers above pin themselves.
#pragma clang fp contract(off) reassociate(off) reciprocal(off)

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

/// A scalar struct over f32 whose fields are distinct values derived from @p seed.
template <class S>
[[nodiscard]] S distinct(f32 seed) {
	S s{};
	std::apply(
	    [&](auto... m) {
		    f32 k = seed;
		    ((s.*m = (k += 1.5f)), ...);
	    },
	    S::fields);
	return s;
}

/// Whether @p a and @p b hold the same bits in every field.
template <class S>
[[nodiscard]] bool same_fields(const S& a, const S& b) {
	bool same = true;
	std::apply(
	    [&](auto... m) { ((same = same && std::bit_cast<uint32_t>(a.*m) == std::bit_cast<uint32_t>(b.*m)), ...); },
	    S::fields);
	return same;
}

/// Sets distinct scalars into lanes 0..N-1 of @p packed one at a time; true when after each set the lanes set so far
/// read back bit-equal and the lanes not yet set still read zero.
template <class S, std::size_t N, class P>
[[nodiscard]] bool lanes_round_trip(P packed) {
	for (std::size_t k = 0; k < N; ++k) {
		set_lane<S>(packed, k, distinct<S>(static_cast<f32>(k) * 10.f));
		for (std::size_t l = 0; l < N; ++l) {
			const S expected = l <= k ? distinct<S>(static_cast<f32>(l) * 10.f) : S{};
			if (!same_fields(get_lane<S>(packed, l), expected)) {
				return false;
			}
		}
	}
	return true;
}

/// A float processor kernel that names no `fields`.
struct NoFields {
	using value_type = f32;
	using step_type = f32;
	struct Settings {};
	template <class V>
	struct CoefficientsT {
		V a{};
	};
	using Coefficients = CoefficientsT<f32>;
	template <class V>
	struct StateT {
		V s{};
	};
	using State = StateT<f32>;
	[[nodiscard]] static Coefficients coefficients(const Settings& /*settings*/) { return {}; }
	template <class V>
	[[nodiscard]] static V step(const CoefficientsT<V>& /*c*/, const EmptyBlock& /*b*/, StateT<V>& /*st*/,
	                            std::type_identity_t<V> x) {
		return x;
	}
};

} // namespace

static_assert(poly_kernel<fp::OnePole> && poly_kernel<fp::Smoother> && poly_kernel<fp::Svf>);
static_assert(processor_kernel<NoFields> && !poly_kernel<NoFields>);
static_assert(!poly_kernel<fixed::OnePole>);

// clang-format off
describe float_kernels_spec("float kernels", ${
	context("OnePole", _ {
		it("clamps a NaN cutoff to a valid one: finite coefficients", _ {
			const auto c = fp::OnePole::coefficients({.cutoff = std::nanf(""), .mode = fp::OnePole::Mode::LOW_PASS});
			expect(std::isfinite(c.G)).to_be_true();
		});
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
		it("matches its high-pass record", _ {
			fp::FlushToZero ftz;
			Mono<fp::OnePole> hp;
			hp.configure({.cutoff = 0.02f, .mode = fp::OnePole::Mode::HIGH_PASS});
			const auto in = noise(kBlock * 4, 4);
			std::vector<f32> out(in.size());
			hp.render_block(fp::Signal{in}, fp::Buffer{out});
			expect(digest(out)).to_equal(records::kOnePoleHighPass);
		});
		it("clamps its cutoff: zero, negative and above Nyquist stay finite", _ {
			fp::FlushToZero ftz;
			const auto in = noise(kBlock, 5);
			// At the 0.49 clamp G is about 0.97 and the state rings with factor 1 - 2G, about -0.94: the low-pass impulse
			// response has an L1 norm of about 1.94, and the high-pass's (x - lp) of about 2.94, so |out| <= 2.1 and 3.1
			// for |x| <= 1.
			for (const auto mode : {fp::OnePole::Mode::LOW_PASS, fp::OnePole::Mode::HIGH_PASS}) {
				const f32 bound = mode == fp::OnePole::Mode::LOW_PASS ? 2.1f : 3.1f;
				for (const f32 cutoff : {0.f, -0.3f, 0.9f}) {
					Mono<fp::OnePole> filter;
					filter.configure({.cutoff = cutoff, .mode = mode});
					std::vector<f32> out(kBlock);
					filter.render_block(fp::Signal{in}, fp::Buffer{out});
					expect(std::ranges::all_of(out, [bound](f32 x) { return std::isfinite(x) && std::fabs(x) <= bound; }))
					    .to_be_true();
				}
			}
		});
	});
	context("Smoother", _ {
		it("lands exactly on the target at the block's end, LINEAR", _ {
			fp::FlushToZero ftz;
			Mono<fp::Smoother> s;
			s.configure({.target = 0.75f, .time = 0.f, .mode = fp::Smoother::Mode::LINEAR});
			std::vector<f32> unused(kBlock, 0.f), out(kBlock);
			s.render_block(fp::Signal{unused}, fp::Buffer{out});
			expect(out.back()).to_equal(0.75f);
			expect(out.front() > 0.f && out.front() < 0.75f).to_be_true();
		});
		it("reaches 1 - 1/e of a step in its time constant, EXPONENTIAL", _ {
			fp::FlushToZero ftz;
			Mono<fp::Smoother> s;
			s.configure({.target = 1.f, .time = 100.f, .mode = fp::Smoother::Mode::EXPONENTIAL});
			std::vector<f32> unused(kBlock, 0.f), out(kBlock);
			s.render_block(fp::Signal{unused}, fp::Buffer{out});
			expect(std::fabs(out[99] - (1.f - std::exp(-1.f))) < 1e-3f).to_be_true();
		});
		it("matches its record", _ {
			fp::FlushToZero ftz;
			Mono<fp::Smoother> s;
			std::vector<f32> unused(kBlock, 0.f), out(kBlock * 4);
			for (std::size_t b = 0; b < 4; ++b) {
				s.configure({.target = (b % 2 == 0) ? 0.9f : -0.3f, .time = 37.f, .mode = fp::Smoother::Mode::EXPONENTIAL});
				s.render_block(fp::Signal{unused}, fp::Buffer{out}.subspan(b * kBlock, kBlock));
			}
			expect(digest(out)).to_equal(records::kSmootherExponential);
		});
		it("lands exactly on the target at the last sample of each block, whatever its length, LINEAR", _ {
			fp::FlushToZero ftz;
			// One smoother, configured once per block with only a target: each block's ramp spans that block.
			Mono<fp::Smoother> s;
			f32 from = 0.f;
			f32 target = 0.75f;
			for (const std::size_t n : {100UZ, 37UZ, 1UZ, 128UZ, 2UZ}) {
				s.configure({.target = target, .time = 0.f, .mode = fp::Smoother::Mode::LINEAR});
				std::vector<f32> unused(n, 0.f), out(n);
				s.render_block(fp::Signal{unused}, fp::Buffer{out});
				expect(std::bit_cast<uint32_t>(out.back())).to_equal(std::bit_cast<uint32_t>(target));
				bool before_is_between = true;
				for (std::size_t i = 0; i + 1 < n; ++i) {
					before_is_between = before_is_between && out[i] > std::min(from, target) && out[i] < std::max(from, target);
				}
				expect(before_is_between).to_be_true();
				from = target;
				target = -target * 0.5f;
			}
		});
		it("starts each LINEAR block from the last value and lands on the new target", _ {
			fp::FlushToZero ftz;
			Mono<fp::Smoother> s;
			std::vector<f32> unused(64, 0.f), first(64), second(64);
			s.configure({.target = 0.5f, .time = 0.f, .mode = fp::Smoother::Mode::LINEAR});
			s.render_block(fp::Signal{unused}, fp::Buffer{first});
			s.configure({.target = -0.25f, .time = 0.f, .mode = fp::Smoother::Mode::LINEAR});
			s.render_block(fp::Signal{unused}, fp::Buffer{second});
			expect(std::bit_cast<uint32_t>(first.back())).to_equal(std::bit_cast<uint32_t>(0.5f));
			expect(second.front() < 0.5f && second.front() > 0.5f - 0.75f / 32.f).to_be_true();
			expect(std::bit_cast<uint32_t>(second.back())).to_equal(std::bit_cast<uint32_t>(-0.25f));
		});
		it("jumps to the target exactly when its time is zero or less, EXPONENTIAL", _ {
			fp::FlushToZero ftz;
			for (const f32 time : {0.f, -5.f}) {
				Mono<fp::Smoother> s;
				s.configure({.target = 0.3f, .time = time, .mode = fp::Smoother::Mode::EXPONENTIAL});
				std::vector<f32> unused(16, 0.f), out(16);
				s.render_block(fp::Signal{unused}, fp::Buffer{out});
				bool all_target = true;
				for (const f32 x : out) {
					all_target = all_target && std::bit_cast<uint32_t>(x) == std::bit_cast<uint32_t>(0.3f);
				}
				expect(all_target).to_be_true();
			}
		});
		it("matches its linear record", _ {
			fp::FlushToZero ftz;
			Mono<fp::Smoother> s;
			std::vector<f32> unused(kBlock, 0.f), out(kBlock * 4);
			for (std::size_t b = 0; b < 4; ++b) {
				s.configure({.target = (b % 2 == 0) ? 0.9f : -0.3f, .time = 0.f, .mode = fp::Smoother::Mode::LINEAR});
				s.render_block(fp::Signal{unused}, fp::Buffer{out}.subspan(b * kBlock, kBlock));
			}
			expect(digest(out)).to_equal(records::kSmootherLinear);
		});
	});
	context("Svf", _ {
		it("clamps a NaN cutoff to a valid one: finite coefficients", _ {
			const auto c = fp::Svf::coefficients({.cutoff = std::nanf(""), .q = 1.f, .mode = fp::Svf::Mode::LOW_PASS});
			for (const f32 v : {c.k, c.a1, c.a2, c.a3}) {
				expect(std::isfinite(v)).to_be_true();
			}
		});
		it("low-passes: unity at DC, -3 dB at fc for Q = 1/sqrt 2, -12 dB per octave above", _ {
			fp::FlushToZero ftz;
			Mono<fp::Svf> f;
			f.configure({.cutoff = 0.01f, .q = std::numbers::sqrt2_v<f32> / 2.f, .mode = fp::Svf::Mode::LOW_PASS});
			expect(std::fabs(sine_gain(f, 0.01f) - std::numbers::sqrt2_v<f32> / 2.f) < 0.01f).to_be_true();
			f.reset();
			// Two octaves above fc the warped frequency ratio is s = tan(4*pi*fc)/tan(pi*fc), about 4.02, so
			// |H| = 1/sqrt((1 - s^2)^2 + (s/Q)^2), about 0.062: -24 dB.
			const f32 two_octaves = sine_gain(f, 0.04f);
			expect(two_octaves < 0.08f && two_octaves > 0.05f).to_be_true();
		});
		it("band-passes at a peak gain of Q at fc", _ {
			fp::FlushToZero ftz;
			Mono<fp::Svf> f;
			f.configure({.cutoff = 0.02f, .q = 4.f, .mode = fp::Svf::Mode::BAND_PASS});
			// v1 = g*s / (s^2 + k*g*s + g^2) with s = j*g at fc: |H| = g^2 / (k*g^2) = 1/k = Q.
			expect(std::fabs(sine_gain(f, 0.02f) - 4.f) < 0.1f).to_be_true();
		});
		it("high-passes and notches", _ {
			fp::FlushToZero ftz;
			Mono<fp::Svf> hp, notch;
			hp.configure({.cutoff = 0.02f, .q = 0.7071f, .mode = fp::Svf::Mode::HIGH_PASS});
			notch.configure({.cutoff = 0.02f, .q = 2.f, .mode = fp::Svf::Mode::NOTCH});
			expect(sine_gain(hp, 0.2f) > 0.98f).to_be_true();
			expect(sine_gain(notch, 0.02f) < 0.02f).to_be_true();
		});
		it("stays bounded under cutoff and Q sweeps to the clamps", _ {
			fp::FlushToZero ftz;
			Mono<fp::Svf> f;
			const auto in = noise(kBlock, 4);
			std::vector<f32> out(kBlock);
			for (int k = 0; k < 400; ++k) {
				f.configure({.cutoff = 0.0001f + (0.6f * static_cast<f32>(k % 40) / 40.f),
				             .q = 0.1f + static_cast<f32>(k % 50),
				             .mode = fp::Svf::Mode::LOW_PASS});
				f.render_block(fp::Signal{in}, fp::Buffer{out});
				for (f32 y : out) {
					expect(std::isfinite(y) && std::fabs(y) < 100.f).to_be_true();
				}
			}
		});
		it("matches its records", _ {
			fp::FlushToZero ftz;
			const auto in = noise(kBlock * 4, 6);
			std::vector<f32> out(in.size());
			Mono<fp::Svf> f;
			f.configure({.cutoff = 0.03f, .q = 3.f, .mode = fp::Svf::Mode::LOW_PASS});
			f.render_block(fp::Signal{in}, fp::Buffer{out});
			expect(digest(out)).to_equal(records::kSvfLowPass);
			f.reset();
			f.configure({.cutoff = 0.03f, .q = 3.f, .mode = fp::Svf::Mode::NOTCH});
			f.render_block(fp::Signal{in}, fp::Buffer{out});
			expect(digest(out)).to_equal(records::kSvfNotch);
			f.reset();
			f.configure({.cutoff = 0.03f, .q = 3.f, .mode = fp::Svf::Mode::HIGH_PASS});
			f.render_block(fp::Signal{in}, fp::Buffer{out});
			expect(digest(out)).to_equal(records::kSvfHighPass);
		});
	});
	context("Linked", _ {
		it("runs each lane exactly as Mono", _ {
			fp::FlushToZero ftz;
			Linked<fp::OnePole, 2, Frame<f32, 2>> two;
			Mono<fp::OnePole> one, other;
			const fp::OnePole::Settings set{.cutoff = 0.03f, .mode = fp::OnePole::Mode::LOW_PASS};
			two.configure(set);
			one.configure(set);
			other.configure(set);
			const auto in = noise(kBlock * 2, 3);
			std::vector<Frame<f32, 2>> frames(kBlock), framed_out(kBlock);
			std::vector<f32> left(kBlock), left_out(kBlock), right(kBlock), right_out(kBlock);
			for (std::size_t i = 0; i < kBlock; ++i) {
				frames[i].lane[0] = in[2 * i];
				frames[i].lane[1] = in[(2 * i) + 1];
				left[i] = in[2 * i];
				right[i] = in[(2 * i) + 1];
			}
			two.render_block(std::span<const Frame<f32, 2>>{frames}, std::span<Frame<f32, 2>>{framed_out});
			one.render_block(fp::Signal{left}, fp::Buffer{left_out});
			other.render_block(fp::Signal{right}, fp::Buffer{right_out});
			for (std::size_t i = 0; i < kBlock; ++i) {
				expect(std::bit_cast<uint32_t>(framed_out[i].lane[0])).to_equal(std::bit_cast<uint32_t>(left_out[i]));
				expect(std::bit_cast<uint32_t>(framed_out[i].lane[1])).to_equal(std::bit_cast<uint32_t>(right_out[i]));
			}
		});
		it("runs each Svf lane exactly as Mono", _ {
			fp::FlushToZero ftz;
			Linked<fp::Svf, 2, Frame<f32, 2>> two;
			Mono<fp::Svf> one, other;
			const fp::Svf::Settings set{.cutoff = 0.02f, .q = 3.f, .mode = fp::Svf::Mode::NOTCH};
			two.configure(set);
			one.configure(set);
			other.configure(set);
			const auto in = noise(kBlock * 2, 3);
			std::vector<Frame<f32, 2>> frames(kBlock), framed_out(kBlock);
			std::vector<f32> left(kBlock), left_out(kBlock), right(kBlock), right_out(kBlock);
			for (std::size_t i = 0; i < kBlock; ++i) {
				frames[i].lane[0] = in[2 * i];
				frames[i].lane[1] = in[(2 * i) + 1];
				left[i] = in[2 * i];
				right[i] = in[(2 * i) + 1];
			}
			two.render_block(std::span<const Frame<f32, 2>>{frames}, std::span<Frame<f32, 2>>{framed_out});
			one.render_block(fp::Signal{left}, fp::Buffer{left_out});
			other.render_block(fp::Signal{right}, fp::Buffer{right_out});
			for (std::size_t i = 0; i < kBlock; ++i) {
				expect(std::bit_cast<uint32_t>(framed_out[i].lane[0])).to_equal(std::bit_cast<uint32_t>(left_out[i]));
				expect(std::bit_cast<uint32_t>(framed_out[i].lane[1])).to_equal(std::bit_cast<uint32_t>(right_out[i]));
			}
		});
	});
	context("set_lane and get_lane", _ {
		it("round-trip every lane of OnePole's coefficients and state, at N = 2 and 4", _ {
			using C4 = fp::OnePole::CoefficientsT<Pack<f32, 4>>;
			using C2 = fp::OnePole::CoefficientsT<Pack<f32, 2>>;
			expect((lanes_round_trip<fp::OnePole::Coefficients, 4>(C4{}))).to_be_true();
			expect((lanes_round_trip<fp::OnePole::Coefficients, 2>(C2{}))).to_be_true();
			expect((lanes_round_trip<fp::OnePole::State, 4>(fp::OnePole::StateT<Pack<f32, 4>>{}))).to_be_true();
			expect((lanes_round_trip<fp::OnePole::State, 2>(fp::OnePole::StateT<Pack<f32, 2>>{}))).to_be_true();
		});
		it("round-trip every lane of Smoother's coefficients and state, at N = 2 and 4", _ {
			using C4 = fp::Smoother::CoefficientsT<Pack<f32, 4>>;
			using C2 = fp::Smoother::CoefficientsT<Pack<f32, 2>>;
			expect((lanes_round_trip<fp::Smoother::Coefficients, 4>(C4{}))).to_be_true();
			expect((lanes_round_trip<fp::Smoother::Coefficients, 2>(C2{}))).to_be_true();
			expect((lanes_round_trip<fp::Smoother::State, 4>(fp::Smoother::StateT<Pack<f32, 4>>{}))).to_be_true();
			expect((lanes_round_trip<fp::Smoother::State, 2>(fp::Smoother::StateT<Pack<f32, 2>>{}))).to_be_true();
		});
		it("leave the packed mode alone", _ {
			fp::OnePole::CoefficientsT<Pack<f32, 4>> packed{};
			packed.mode = fp::OnePole::Mode::HIGH_PASS;
			set_lane<fp::OnePole::Coefficients>(
			    packed, 1, fp::OnePole::Coefficients{.G = 0.25f, .mode = fp::OnePole::Mode::LOW_PASS});
			expect(packed.mode == fp::OnePole::Mode::HIGH_PASS).to_be_true();
			expect(get_lane<fp::OnePole::Coefficients>(packed, 1).G).to_equal(0.25f);
			expect(get_lane<fp::OnePole::Coefficients>(packed, 1).mode == fp::OnePole::Mode::LOW_PASS).to_be_true();
		});
	});
});
// clang-format on

CPPSPEC_MAIN(float_kernels_spec);
