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

// The float ladder: its saturator (odd, monotone, bounded, unity slope at 0, landing on ±1 at its knee with no slope,
// lane for lane equal to its scalar form), its responses (slopes, DC gain, the resonance peak, self-oscillation), its
// morph between presets, and its stability at every setting.

#include "dsp-next/core/flush_to_zero.h"
#include "dsp-next/core/linked.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/pack_f32.h"
#include "dsp-next/float/filter/ladder.h"
#include "dsp-next/float/math/detail.h"

#include "float_kernel_records.h"

#include <array>
#include <bit>
#include <cmath>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <span>
#include <vector>

using namespace deluge::dsp;

namespace {

[[nodiscard]] uint32_t bits(f32 x) {
	return std::bit_cast<uint32_t>(x);
}

using Ladder = fp::Ladder;
using Response = Ladder::Response;
constexpr std::size_t kBlock = 128;

/// The steady-state gain of a sine of amplitude @p amplitude at @p freq (fraction of fs) through a ladder configured
/// with @p settings, after 32 blocks to settle: the peak over the next 32 blocks over the amplitude.
[[nodiscard]] f32 sine_gain(const Ladder::Settings& settings, f32 freq, f32 amplitude = 0.01F) {
	Mono<Ladder> ladder;
	ladder.configure(settings);
	std::vector<f32> in(kBlock * 64), out(in.size());
	for (std::size_t i = 0; i < in.size(); ++i) {
		in[i] = amplitude * std::sin(2.F * std::numbers::pi_v<f32> * freq * static_cast<f32>(i));
	}
	ladder.render_block(fp::Signal{in}, fp::Buffer{out});
	f32 peak = 0.F;
	for (std::size_t i = kBlock * 32; i < out.size(); ++i) {
		peak = std::max(peak, std::fabs(out[i]));
	}
	return peak / amplitude;
}

/// Magnitude of the DFT bin at @p freq (fraction of fs) over @p x, normalised so a unit sine reads 1.
[[nodiscard]] f32 bin(const std::vector<f32>& x, f32 freq) {
	double re = 0.0;
	double im = 0.0;
	for (std::size_t i = 0; i < x.size(); ++i) {
		const double w = 2.0 * std::numbers::pi * static_cast<double>(freq) * static_cast<double>(i);
		re += static_cast<double>(x[i]) * std::cos(w);
		im -= static_cast<double>(x[i]) * std::sin(w);
	}
	return static_cast<f32>(2.0 * std::hypot(re, im) / static_cast<double>(x.size()));
}

[[nodiscard]] f32 db(f32 ratio) {
	return 20.F * std::log10(ratio);
}

/// FNV-1a over the output's bits: the digest a record pins.
[[nodiscard]] uint32_t digest(std::span<const f32> out) {
	uint32_t h = 2166136261U;
	for (const f32 x : out) {
		h = (h ^ std::bit_cast<uint32_t>(x)) * 16777619U;
	}
	return h;
}

/// Seeded noise over ±1.
[[nodiscard]] std::vector<f32> noise(std::size_t n, uint32_t seed) {
	std::mt19937 rng{seed};
	std::uniform_real_distribution<f32> d{-1.F, 1.F};
	std::vector<f32> v(n);
	for (f32& x : v) {
		x = d(rng);
	}
	return v;
}

/// The digest of 8 blocks of seeded noise through a ladder configured with @p settings, in one render.
[[nodiscard]] uint32_t record(const Ladder::Settings& settings) {
	fp::FlushToZero ftz;
	Mono<Ladder> ladder;
	ladder.configure(settings);
	const auto in = noise(kBlock * 8, 5);
	std::vector<f32> out(in.size());
	ladder.render_block(fp::Signal{in}, fp::Buffer{out});
	return digest(out);
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
	context("Ladder, CLEAN", _ {
		fp::FlushToZero ftz;
		constexpr f32 kFc = 0.005F;
		it("falls 24 dB per octave above the cutoff as LP24, and 12 as LP12", _ {
			const f32 lp24 = db(sine_gain({.cutoff = kFc}, 8 * kFc) / sine_gain({.cutoff = kFc}, 4 * kFc));
			const f32 lp12 = db(sine_gain({.cutoff = kFc, .response = Response::LP12}, 8 * kFc) /
			                    sine_gain({.cutoff = kFc, .response = Response::LP12}, 4 * kFc));
			expect(std::fabs(lp24 + 24.F) < 2.F).to_be_true();
			expect(std::fabs(lp12 + 12.F) < 2.F).to_be_true();
		});
		it("rises 24 dB per octave below the cutoff as HP", _ {
			const Ladder::Settings hp{.cutoff = kFc, .response = Response::HP};
			const f32 slope = db(sine_gain(hp, kFc / 8) / sine_gain(hp, kFc / 16));
			expect(std::fabs(slope - 24.F) < 2.F).to_be_true();
		});
		it("peaks near the cutoff as BP", _ {
			const Ladder::Settings bp{.cutoff = kFc, .response = Response::BP};
			f32 best = 0.F;
			f32 at = 0.F;
			for (int i = 0; i <= 20; ++i) {
				const f32 f = kFc * std::pow(2.F, (static_cast<f32>(i) - 10.F) / 5.F);
				const f32 gain = sine_gain(bp, f);
				if (gain > best) {
					best = gain;
					at = f;
				}
			}
			expect(std::fabs(at / kFc - 1.F) < 0.2F).to_be_true();
		});
		it("passes DC at unity with no resonance, and with compensation at resonance 2", _ {
			for (const Ladder::Settings settings : {Ladder::Settings{.cutoff = kFc},
			                                        Ladder::Settings{.cutoff = kFc, .resonance = 2.F, .compensate = true}}) {
				Mono<Ladder> ladder;
				ladder.configure(settings);
				std::vector<f32> in(kBlock * 64, 0.01F), out(in.size());
				ladder.render_block(fp::Signal{in}, fp::Buffer{out});
				const f32 tolerance = settings.compensate ? 0.02F : 1e-4F;
				expect(std::fabs(out.back() / 0.01F - 1.F) < tolerance).to_be_true();
			}
		});
		it("raises its peak at the cutoff as the resonance rises", _ {
			f32 last = 0.F;
			for (const f32 k : {0.F, 1.F, 2.F, 3.F, 3.5F}) {
				const f32 gain = sine_gain({.cutoff = kFc, .resonance = k}, kFc);
				expect(gain > last).to_be_true();
				last = gain;
			}
		});
		it("self-oscillates near its cutoff above resonance 4, held by its saturator", _ {
			constexpr f32 kOsc = 0.01F;
			Mono<Ladder> ladder;
			ladder.configure({.cutoff = kOsc, .resonance = 4.25F});
			std::vector<f32> in(48000, 0.F), out(in.size());
			in[0] = 1e-3F;
			ladder.render_block(fp::Signal{in}, fp::Buffer{out});
			f32 peak = 0.F;
			int crossings = 0;
			for (std::size_t i = 48000 - 4800; i < out.size(); ++i) {
				peak = std::max(peak, std::fabs(out[i]));
				crossings += (out[i - 1] < 0.F) != (out[i] < 0.F) ? 1 : 0;
			}
			const f32 frequency = static_cast<f32>(crossings) / 2.F / 4800.F;
			expect(peak > 0.05F && peak <= 1.F).to_be_true();
			expect(std::fabs(frequency / kOsc - 1.F) < 0.05F).to_be_true();
		});
		it("sustains an impulse at resonance 4 without growing or decaying", _ {
			Mono<Ladder> ladder;
			ladder.configure({.cutoff = 0.01F, .resonance = 4.F});
			std::vector<f32> in(48000, 0.F), out(in.size());
			in[0] = 1e-3F;
			ladder.render_block(fp::Signal{in}, fp::Buffer{out});
			const auto peak_of = [&](std::size_t from) {
				f32 peak = 0.F;
				for (std::size_t i = from; i < from + 4800; ++i) {
					peak = std::max(peak, std::fabs(out[i]));
				}
				return peak;
			};
			const f32 early = peak_of(4800);
			const f32 late = peak_of(48000 - 4800);
			expect(early > 0.F).to_be_true();
			expect(std::fabs(late / early - 1.F) < 0.1F).to_be_true();
		});
		it("morphs linearly in its weights", _ {
			const Ladder::Settings lp{.cutoff = 0.02F, .resonance = 1.F, .response = Response::LP24};
			Ladder::Settings bp = lp;
			bp.response = Response::BP;
			Ladder::Settings half = lp;
			half.morph_to = Response::BP;
			half.morph = 0.5F;
			const Ladder::Coefficients c = Ladder::coefficients(half);
			for (std::size_t i = 0; i < 5; ++i) {
				const double a = Ladder::weights(Response::LP24)[i];
				const double b = Ladder::weights(Response::BP)[i];
				expect(bits(c.w[i])).to_equal(bits(static_cast<f32>(a + 0.5 * (b - a))));
			}
			const auto in = noise(kBlock * 4, 3);
			std::vector<f32> a(in.size()), b(in.size()), m(in.size());
			Mono<Ladder> la, lb, lm;
			la.configure(lp);
			lb.configure(bp);
			lm.configure(half);
			la.render_block(fp::Signal{in}, fp::Buffer{a});
			lb.render_block(fp::Signal{in}, fp::Buffer{b});
			lm.render_block(fp::Signal{in}, fp::Buffer{m});
			for (std::size_t i = 0; i < in.size(); ++i) {
				expect(std::fabs(m[i] - 0.5F * (a[i] + b[i])) < 1e-6F).to_be_true();
			}
		});
		it("stays finite and bounded at every setting", _ {
			const auto in = noise(kBlock, 7);
			std::vector<f32> out(kBlock);
			constexpr f32 kNan = std::numeric_limits<f32>::quiet_NaN();
			for (int i = 0; i < 400; ++i) {
				Ladder::Settings s{.cutoff = 1e-5F + 0.6F * static_cast<f32>(i % 20) / 20.F,
				                   .resonance = -1.F + 6.F * static_cast<f32>(i / 20) / 20.F,
				                   .drive = 1.F + static_cast<f32>(i % 7),
				                   .compensate = (i % 2) == 0,
				                   .response = static_cast<Response>(i % 4)};
				if (i % 50 == 0) {
					s.cutoff = kNan;
				}
				if (i % 50 == 1) {
					s.resonance = kNan;
				}
				if (i % 50 == 2) {
					s.drive = kNan;
				}
				if (i % 50 == 3) {
					s.morph = kNan;
				}
				Mono<Ladder> ladder;
				ladder.configure(s);
				ladder.render_block(fp::Signal{in}, fp::Buffer{out});
				for (const f32 y : out) {
					expect(std::isfinite(y) && std::fabs(y) <= 64.F).to_be_true();
				}
			}
		});
	});
	context("Ladder, DRIVE", _ {
		fp::FlushToZero ftz;
		it("matches CLEAN at small signals and drive 1", _ {
			const auto in = noise(kBlock * 4, 11);
			std::vector<f32> scaled(in.size()), clean(in.size()), drive(in.size());
			for (std::size_t i = 0; i < in.size(); ++i) {
				scaled[i] = 0.01F * in[i];
			}
			Mono<Ladder> a, b;
			a.configure({.cutoff = 0.02F, .resonance = 2.F});
			b.configure({.cutoff = 0.02F, .resonance = 2.F, .mode = Ladder::Mode::DRIVE});
			a.render_block(fp::Signal{scaled}, fp::Buffer{clean});
			b.render_block(fp::Signal{scaled}, fp::Buffer{drive});
			for (std::size_t i = 0; i < in.size(); ++i) {
				expect(std::fabs(clean[i] - drive[i]) < 1e-4F).to_be_true();
			}
		});
		it("adds odd harmonics at high drive", _ {
			// 0.5-amplitude sine at fc/4, 100-sample cycles so the DFT bins hold whole cycles.
			constexpr f32 kF0 = 0.01F;
			Mono<Ladder> ladder;
			ladder.configure({.cutoff = 4 * kF0, .resonance = 1.F, .drive = 8.F, .mode = Ladder::Mode::DRIVE});
			std::vector<f32> in(100 * 96), out(in.size());
			for (std::size_t i = 0; i < in.size(); ++i) {
				in[i] = 0.5F * std::sin(2.F * std::numbers::pi_v<f32> * kF0 * static_cast<f32>(i));
			}
			ladder.render_block(fp::Signal{in}, fp::Buffer{out});
			const std::vector<f32> settled(out.begin() + 100 * 32, out.end());
			const f32 fundamental = bin(settled, kF0);
			expect(db(bin(settled, 3 * kF0) / fundamental) > -40.F).to_be_true();
			expect(db(bin(settled, 2 * kF0) / fundamental) < -80.F).to_be_true();
		});
		it("keeps its state across a flavour switch", _ {
			const auto in = noise(kBlock * 16, 13);
			std::vector<f32> scaled(in.size()), out(in.size());
			for (std::size_t i = 0; i < in.size(); ++i) {
				scaled[i] = 0.01F * in[i];
			}
			Mono<Ladder> ladder;
			ladder.configure({.cutoff = 0.02F, .resonance = 1.F});
			const std::size_t half = kBlock * 8;
			ladder.render_block(fp::Signal{scaled}.first(half), fp::Buffer{out}.first(half));
			ladder.configure({.cutoff = 0.02F, .resonance = 1.F, .mode = Ladder::Mode::DRIVE});
			ladder.render_block(fp::Signal{scaled}.subspan(half), fp::Buffer{out}.subspan(half));
			f32 largest = 0.F;
			for (std::size_t i = 1; i < out.size(); ++i) {
				if (i != half) {
					largest = std::max(largest, std::fabs(out[i] - out[i - 1]));
				}
			}
			expect(std::fabs(out[half] - out[half - 1]) <= 2.F * largest).to_be_true();
		});
	});
	context("Linked", _ {
		it("runs each lane exactly as Mono, in both flavours", _ {
			for (const auto mode : {Ladder::Mode::CLEAN, Ladder::Mode::DRIVE}) {
				fp::FlushToZero ftz;
				Linked<Ladder, 2, Frame<f32, 2>> two;
				Mono<Ladder> one, other;
				const Ladder::Settings set{.cutoff = 0.04F, .resonance = 3.F, .drive = 2.F, .response = Response::BP,
				                           .mode = mode};
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
					expect(bits(framed_out[i].lane[0])).to_equal(bits(left_out[i]));
					expect(bits(framed_out[i].lane[1])).to_equal(bits(right_out[i]));
				}
			}
		});
	});
	context("records", _ {
		it("matches its records", _ {
			expect(record({.cutoff = 0.05F, .resonance = 3.F, .drive = 2.F})).to_equal(records::kLadderLp24Clean);
			expect(record({.cutoff = 0.05F, .resonance = 3.F, .drive = 2.F, .mode = Ladder::Mode::DRIVE}))
			    .to_equal(records::kLadderLp24Drive);
			expect(record({.cutoff = 0.1F, .resonance = 2.F, .response = Response::HP, .morph_to = Response::BP,
			               .morph = 0.3F}))
			    .to_equal(records::kLadderMorphHpBp);
		});
	});
});
// clang-format on

CPPSPEC_MAIN(float_ladder_spec);
