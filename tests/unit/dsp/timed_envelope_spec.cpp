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

// fp::TimedEnvelope against its stages in continuous time: every program, curve and length, at any block size and from
// any sample of a block; stages ending at their exact sample; retriggers from where the level is; lanes in different
// stages each as a scalar copy stepped at their split.

#include "dsp/core/mono.h"
#include "dsp/core/poly.h"
#include "dsp/float/envelope/curve.h"
#include "dsp/float/envelope/timed_envelope.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <numbers>
#include <vector>

#include "support/matchers/value_matchers.h"
#include "support/spec_main.h"

using namespace deluge::dsp;
using fp::TimedEnvelope;
namespace timed = fp::timed;

// As float_envelope_spec: the twin built with the device's float flags holds the kernels to IEEE order.
#pragma clang fp contract(off) reassociate(off) reciprocal(off)

static_assert(generator_kernel<TimedEnvelope> && poly_kernel<TimedEnvelope> && runs_kernel<TimedEnvelope>);

namespace {

constexpr double kTolerance = 0x1p-14;

[[nodiscard]] double falling(double k, double x) {
	x = std::clamp(x, 0.0, 1.0);
	return k == 0.0 ? 1.0 - x : std::expm1(k * std::numbers::ln2 * (1.0 - x)) / std::expm1(k * std::numbers::ln2);
}

/// A program's level in continuous time, samples as the unit.
class Reference {
public:
	explicit Reference(const timed::Program& program) : program_{program} {}

	/// @param now The level the envelope is at, which an event goes on from.
	void gate_on(double t, double now) { enter(0.f, now, t); }
	void gate_off(double t, double now) {
		(void)level(t);
		if (program_.release >= 0.f && index_ >= 0.f) {
			enter(program_.release, now, t);
		}
	}
	double level(double t) {
		while (index_ >= 0.f && t - start_ > length() + 1e-6) {
			const double end = start_ + length();
			const double target = segment().target;
			enter(segment().next, target, end);
		}
		if (index_ < 0.f) {
			return 0.0;
		}
		const double target = segment().target;
		if (std::isinf(length())) {
			return target;
		}
		return target + ((from_ - target) * falling(segment().curve, (t - start_) / length()));
	}
	[[nodiscard]] bool resting() const { return index_ < 0.f; }

private:
	[[nodiscard]] const timed::Segment<f32>& segment() const {
		return program_.segments[static_cast<std::size_t>(index_)];
	}
	[[nodiscard]] double length() const { return std::max<double>(segment().length, 1.0); }
	void enter(f32 index, double from, double t) {
		index_ = index;
		from_ = from;
		start_ = t;
		if (index_ >= 0.f && segment().resume > 0.5f && from > 0.0) {
			// As if it had started at 0, so long ago that it is at `from` now
			double low = 0.0;
			double high = 1.0;
			for (int32_t i = 0; i < 50; ++i) {
				const double middle = 0.5 * (low + high);
				((1.0 - falling(segment().curve, middle)) * segment().target < from ? low : high) = middle;
			}
			if (from >= segment().target) { // there already
				enter(segment().next, from, t);
				return;
			}
			from_ = 0.0;
			start_ = t - (high * length());
		}
	}

	timed::Program program_;
	f32 index_ = -1.f;
	double from_ = 0.0;
	double start_ = 0.0;
};

/// A gate at a sample.
struct Gate {
	std::size_t at;
	bool on;
};

/// The worst error of @p program against its reference over @p samples in blocks of @p n, gated as @p gates say (each
/// at a block's start).
[[nodiscard]] double worst_error(const timed::Program& program, std::size_t n, std::size_t samples,
                                 const std::vector<Gate>& gates) {
	Mono<TimedEnvelope> env;
	env.configure({.program = program});
	Reference exact{program};
	std::vector<f32> out(n);
	double worst = 0.0;
	for (std::size_t t = 0; t < samples; t += n) {
		for (const Gate& gate : gates) {
			if (gate.at >= t && gate.at < t + n && gate.at == t) {
				if (gate.on) {
					exact.gate_on(static_cast<double>(t), env.state().v);
					env.on<&TimedEnvelope::gate_on>();
				}
				else {
					exact.gate_off(static_cast<double>(t), env.state().v);
					env.on<&TimedEnvelope::gate_off>();
				}
			}
		}
		env.render_block(fp::Buffer{out});
		for (std::size_t i = 0; i < n; ++i) {
			worst = std::max(worst, std::abs(out[i] - exact.level(static_cast<double>(t + i + 1))));
		}
	}
	return worst;
}

/// Samples until the envelope rests, at most @p limit, in blocks of @p n.
[[nodiscard]] std::size_t samples_to_rest(Mono<TimedEnvelope>& env, std::size_t n, std::size_t limit) {
	std::vector<f32> out(n);
	std::size_t t = 0;
	while (t < limit && env.state().index >= 0.f) {
		env.render_block(fp::Buffer{out});
		t += n;
	}
	return t;
}

constexpr std::array<f32, 11> kCurves{-12.5f, -8.f, -4.f, -1.f, -0.25f, 0.f, 0.25f, 1.f, 4.f, 8.f, 12.5f};

} // namespace

// clang-format off
describe timed_envelope_spec_body("fp::TimedEnvelope", ${
	it("follows main's curves at every sample, at every length, curve and block size", _ {
		for (const f32 length : {64.f, 100.f, 1000.f, 44100.f}) {
			for (const f32 k : kCurves) {
				const timed::Program p = timed::adsr(length, length, 0.25f, length, {.attack = k, .decay = k, .release = k});
				for (const std::size_t n : {std::size_t{1}, std::size_t{7}, std::size_t{32}, std::size_t{128}}) {
					if (n == 1 && length > 2000.f) {
						continue;
					}
					const auto whole = static_cast<std::size_t>(length);
					const std::size_t off = ((2 * whole + whole / 2) / n) * n; // in the sustain
					to_be_at_most(expect(worst_error(p, n, off + whole + 256, {{0, true}, {off, false}})), kTolerance);
				}
			}
		}
	});

	it("enters a stage at any sample of a block", _ {
		for (const f32 k : kCurves) {
			for (std::size_t at = 1; at <= 128; ++at) {
				const timed::Program p =
				    timed::adsr(static_cast<f32>(at), 300.f, 0.5f, 100.f, {.attack = 0.f, .decay = k, .release = k});
				to_be_at_most(expect(worst_error(p, 128, 1024, {{0, true}, {768, false}})), kTolerance);
			}
		}
	});

	it("carries the fraction of a sample a stage ends at", _ {
		for (const f32 fraction : {0.1f, 0.5f, 0.93f}) {
			const timed::Program p = timed::adsr(20.f + fraction, 33.f + fraction, 0.5f, 47.f + fraction, {});
			for (const std::size_t n : {std::size_t{1}, std::size_t{32}, std::size_t{128}}) {
				to_be_at_most(expect(worst_error(p, n, 512, {{0, true}, {256, false}})), kTolerance);
			}
		}
	});

	it("ends a stage of a minute within a sample, at any block size", _ {
		constexpr f32 kMinute = 60.f * 44100.f;
		for (const std::size_t n : {std::size_t{1}, std::size_t{128}}) {
			Mono<TimedEnvelope> env;
			env.configure({.program = timed::ad(1.f, kMinute, {.attack = 0.f, .decay = 4.f})});
			env.on<&TimedEnvelope::gate_on>();
			const std::size_t end = samples_to_rest(env, n, static_cast<std::size_t>(kMinute) + 4096);
			to_be_at_least(expect(end), static_cast<std::size_t>(kMinute));
			to_be_at_most(expect(end), static_cast<std::size_t>(kMinute) + 2 + n);
		}
	});

	it("gives every block size the same levels at the same samples", _ {
		for (const timed::Program& p : {timed::adsr(300.f, 700.f, 0.4f, 500.f, {}), timed::ahd(64.f, 200.f, 300.f, {}),
		                                timed::ad(50.f, 900.f, {}), timed::asr(90.f, 400.f, {}),
		                                timed::four_segment({0.9f, 0.4f, 0.6f, 0.1f}, {130.f, 125.f, 140.f, 150.f})}) {
			for (const std::size_t n : {std::size_t{1}, std::size_t{32}, std::size_t{128}}) {
				to_be_at_most(expect(worst_error(p, n, 3072, {{0, true}, {1280, false}})), kTolerance);
			}
		}
	});

	it("runs stages of one sample in order, and loops them without end in finite time", _ {
		const timed::Program four = timed::four_segment({1.f, 0.25f, 0.75f, 0.f}, {0.f, 1.f, 0.5f, 1.f});
		Mono<TimedEnvelope> env;
		env.configure({.program = four});
		env.on<&TimedEnvelope::gate_on>();
		std::vector<f32> out(8);
		env.render_block(fp::Buffer{out});
		expect(out[0]).to_be_within(kTolerance).of(1.f);
		expect(out[1]).to_be_within(kTolerance).of(0.25f);
		expect(out[2]).to_be_within(kTolerance).of(0.75f);
		expect(out[7]).to_be_within(kTolerance).of(0.75f);
		Mono<TimedEnvelope> loop;
		loop.configure({.program = timed::looping(0.f, 1.f, 0.f)});
		loop.on<&TimedEnvelope::gate_on>();
		std::vector<f32> looped(128);
		loop.render_block(fp::Buffer{looped});
		for (std::size_t i = 0; i < looped.size(); ++i) {
			expect(looped[i]).to_be_within(kTolerance).of(i % 2 == 0 ? 1.f : 0.f);
		}
	});

	it("ignores a gate-off where its program has no release, and releases everywhere else", _ {
		for (const std::size_t off : {std::size_t{64}, std::size_t{256}, std::size_t{512}}) {
			for (const timed::Program& p : {timed::ad(128.f, 900.f, {}), timed::ahd(128.f, 300.f, 900.f, {})}) {
				to_be_at_most(expect(worst_error(p, 64, 2048, {{0, true}, {off, false}})), kTolerance);
				Mono<TimedEnvelope> env;
				env.configure({.program = p});
				env.on<&TimedEnvelope::gate_on>();
				expect(env.state().index).to_equal(0.f);
				env.on<&TimedEnvelope::gate_off>();
				expect(env.state().index).to_equal(0.f);
			}
			// A sustain of 0 is no exception: the key-up cuts a decay
			const timed::Program pluck = timed::adsr(16.f, 4000.f, 0.f, 200.f, {});
			Mono<TimedEnvelope> env;
			env.configure({.program = pluck});
			env.on<&TimedEnvelope::gate_on>();
			std::vector<f32> out(off);
			env.render_block(fp::Buffer{out});
			env.on<&TimedEnvelope::gate_off>();
			to_be_at_most(expect(samples_to_rest(env, 1, 4000)), std::size_t{202});
			to_be_at_most(expect(worst_error(pluck, 64, 2048, {{0, true}, {off, false}})), kTolerance);
		}
	});

	it("rests at exactly 0, where a gate-off does nothing", _ {
		Mono<TimedEnvelope> env;
		env.configure({.program = timed::adsr(10.f, 10.f, 0.5f, 10.f, {})});
		std::vector<f32> out(64, 1.f);
		env.on<&TimedEnvelope::gate_off>();
		expect(env.state().index).to_be_less_than(0.f);
		env.render_block(fp::Buffer{out});
		for (const f32 y : out) {
			expect(y).to_equal(0.f);
		}
		env.on<&TimedEnvelope::gate_on>();
		env.render_block(fp::Buffer{out});
		env.on<&TimedEnvelope::gate_off>();
		env.render_block(fp::Buffer{out});
		env.render_block(fp::Buffer{out});
		expect(out[63]).to_equal(0.f);
		expect(env.state().index).to_be_less_than(0.f);
	});

	it("retriggers from where the level is, in any stage of any program, an attack for the time its curve has left", _ {
		for (const f32 k : kCurves) {
			for (const timed::Program& p :
			     {timed::adsr(400.f, 700.f, 0.4f, 500.f, {.attack = k, .decay = k, .release = k}),
			      timed::ahd(400.f, 200.f, 300.f, {.attack = k, .decay = k}), timed::asr(400.f, 400.f, {.attack = k, .release = k}),
			      timed::four_segment({0.9f, 0.4f, 0.6f, 0.1f}, {130.f, 125.f, 140.f, 150.f})}) {
				for (std::size_t again = 64; again <= 1600; again += 64) {
					to_be_at_most(expect(worst_error(p, 64, 3072, {{0, true}, {1152, false}, {again, true}})), kTolerance);
				}
			}
		}
	});

	it("moves a held level to a changed one over 32 samples, at any block size", _ {
		for (const std::size_t n : {std::size_t{1}, std::size_t{7}, std::size_t{32}, std::size_t{128}}) {
			Mono<TimedEnvelope> env;
			env.configure({.program = timed::adsr(8.f, 8.f, 0.9f, 100.f, {})});
			env.on<&TimedEnvelope::gate_on>();
			std::vector<f32> out(n);
			for (std::size_t t = 0; t < 512; t += n) {
				env.render_block(fp::Buffer{out});
			}
			env.configure({.program = timed::adsr(8.f, 8.f, 0.4f, 100.f, {})});
			std::vector<f32> all;
			while (all.size() < 64) {
				env.render_block(fp::Buffer{out});
				all.insert(all.end(), out.begin(), out.end());
			}
			for (std::size_t i = 0; i < 64; ++i) {
				const double want = i < 32 ? 0.9 - (0.5 * static_cast<double>(i + 1) / 32.0) : 0.4;
				expect(all[i]).to_be_within(kTolerance).of(want);
			}
		}
	});

	it("goes in a line from where it was to where a changed curve is 32 samples on, and along it", _ {
		for (const f32 k : kCurves) {
			for (const std::size_t n : {std::size_t{1}, std::size_t{32}, std::size_t{128}}) {
				const auto program = [](f32 curve) {
					return timed::adsr(8.f, 1024.f, 0.25f, 100.f, {.attack = 0.f, .decay = curve});
				};
				Mono<TimedEnvelope> env;
				env.configure({.program = program(4.f)});
				env.on<&TimedEnvelope::gate_on>();
				std::vector<f32> out(n);
				for (std::size_t t = 0; t < 264 - (264 % n); t += n) {
					env.render_block(fp::Buffer{out});
				}
				const double at = 264.0 - static_cast<double>(264 % n) - 8.0;
				const double before = env.state().v;
				const double aim = 0.25 + (0.75 * falling(k, (at + 32.0) / 1024.0));
				env.configure({.program = program(k)});
				std::vector<f32> all;
				while (all.size() < 128) {
					env.render_block(fp::Buffer{out});
					all.insert(all.end(), out.begin(), out.end());
				}
				for (std::size_t i = 0; i < 128; ++i) {
					const double done = static_cast<double>(i + 1) / 32.0;
					const double exact = 0.25 + (0.75 * falling(k, (at + static_cast<double>(i + 1)) / 1024.0));
					// The curve it was on is the one it stays on
					const bool same = k == 4.f;
					expect(all[i]).to_be_within(kTolerance).of(
					    done < 1.0 && !same ? before + ((aim - before) * done) : exact);
				}
			}
		}
	});

	// A stage's length from one sample to a minute, another every block
	it("is retimed every block without a jump, and ends once", _ {
		for (const f32 k : kCurves) {
			for (const std::size_t n : {std::size_t{1}, std::size_t{32}, std::size_t{128}}) {
				Mono<TimedEnvelope> env;
				const std::array<f32, 6> lengths{5000.f, 60.f * 44100.f, 300.f, 20000.f, 5000.f, 1e9f};
				double position = 0.0; // of the decay, at the last sample
				std::vector<f32> out(n);
				env.configure({.program = timed::adsr(1.f, lengths[0], 0.25f, 100.f, {.attack = 0.f, .decay = k})});
				env.on<&TimedEnvelope::gate_on>();
				std::size_t t = 0;
				for (std::size_t block = 0; block < 1200 && env.state().index < 2.f; ++block) {
					const f32 length = lengths[block % lengths.size()];
					env.configure({.program = timed::adsr(1.f, length, 0.25f, 100.f, {.attack = 0.f, .decay = k})});
					env.render_block(fp::Buffer{out});
					for (const f32 y : out) {
						++t;
						if (t > 1) {
							position += 1.0 / length;
						}
						const double exact = position < 1.0 ? 0.25 + (0.75 * falling(k, position)) : 0.25;
						expect(y).to_be_within(kTolerance).of(exact);
					}
				}
			}
		}
	});

	it("ends a stage at once that is shortened past its end, and goes on from its level", _ {
		for (const std::size_t n : {std::size_t{1}, std::size_t{32}, std::size_t{128}}) {
			Mono<TimedEnvelope> env;
			std::vector<f32> out(n);
			env.configure({.program = timed::adsr(1.f, 10000.f, 0.25f, 100.f, {})});
			env.on<&TimedEnvelope::gate_on>();
			for (std::size_t t = 0; t < 1280; t += n) {
				env.render_block(fp::Buffer{out});
			}
			f32 last = out[n - 1];
			const f32 begun = last;
			env.configure({.program = timed::adsr(1.f, 1.f, 0.25f, 100.f, {})});
			std::size_t t = 0;
			while (t < 64) {
				env.render_block(fp::Buffer{out});
				for (const f32 y : out) {
					to_be_at_most(expect(std::abs(y - last)), ((begun - 0.25) / 32.0) + kTolerance);
					last = y;
				}
				t += n;
			}
			expect(env.state().index).to_equal(2.f);
			expect(last).to_be_within(kTolerance).of(0.25f);
		}
	});

	it("starts a stage on time that is lengthened before its first sample", _ {
		for (const f32 fraction : {0.05f, 0.5f, 0.95f}) {
			Mono<TimedEnvelope> env;
			std::vector<f32> out(32);
			env.configure({.program = timed::adsr(32.f + fraction, 64.f, 0.25f, 100.f, {.attack = 0.f, .decay = 8.f})});
			env.on<&TimedEnvelope::gate_on>();
			env.render_block(fp::Buffer{out}); // the attack ends before the next sample
			env.configure({.program = timed::adsr(32.f + fraction, 1e5f, 0.25f, 100.f, {.attack = 0.f, .decay = 8.f})});
			env.render_block(fp::Buffer{out});
			for (const f32 y : out) {
				to_be_at_most(expect(y), 1.f);
			}
			// At its 64th sample it has run 64 less its attack, whatever the length
			expect(env.state().hi * 1e5f).to_be_within(0.05).of(32.0 - fraction);
		}
	});

	it("is bounded, sample to sample, when every setting changes every block", _ {
		uint32_t seed = 12345;
		const auto random = [&seed](f32 low, f32 high) {
			seed = (seed * 1664525U) + 1013904223U;
			return low + ((high - low) * static_cast<f32>(seed >> 8) / 16777216.f);
		};
		Mono<TimedEnvelope> env;
		std::vector<f32> out(32);
		f32 last = 0.f;
		for (std::size_t block = 0; block < 4000; ++block) {
			const f32 k = random(-12.5f, 12.5f);
			const f32 length = std::exp2(random(6.f, 18.f));
			const f32 sustain = random(0.f, 1.f);
			env.configure(
			    {.program = timed::adsr(length, length, sustain, length, {.attack = k, .decay = k, .release = k})});
			if (block % 37 == 0) {
				env.on<&TimedEnvelope::gate_on>();
			}
			if (block % 37 == 25) {
				env.on<&TimedEnvelope::gate_off>();
			}
			env.render_block(fp::Buffer{out});
			// The steepest a curve of 12.5 octaves moves in a sample of 64, and a 32nd of the most a gap can be
			for (const f32 y : out) {
				to_be_at_most(expect(std::abs(y - last)), (12.5 * std::numbers::ln2 / 64.0) + (1.0 / 32.0) + kTolerance);
				to_be_at_least(expect(y), -kTolerance);
				to_be_at_most(expect(y), 1.0 + kTolerance);
				last = y;
			}
		}
	});

	it("steps its lanes, in different stages, as scalar copies at the lanes' split", _ {
		const std::array<timed::Program, 4> programs{
		    timed::adsr(20.5f, 310.f, 0.5f, 60.f, {}), timed::four_segment({0.9f, 0.4f, 0.6f, 0.1f}, {30.f, 25.f, 40.f, 50.f}),
		    timed::asr(15.f, 90.f, {.attack = -6.f, .release = -12.5f}), timed::looping(0.1f, 0.8f, 17.3f)};
		Poly<TimedEnvelope, 4> poly;
		std::array<TimedEnvelope::Coefficients, 4> c{};
		std::array<TimedEnvelope::State, 4> s{};
		for (std::size_t l = 0; l < 4; ++l) {
			poly.configure(l, {.program = programs[l]});
			c[l] = TimedEnvelope::coefficients({.program = programs[l]});
		}
		std::size_t block = 0;
		for (const std::size_t n : {std::size_t{128}, std::size_t{37}, std::size_t{1}, std::size_t{0}, std::size_t{64},
		                            std::size_t{128}, std::size_t{5}, std::size_t{128}, std::size_t{128}}) {
			for (std::size_t l = 0; l < 4; ++l) {
				if (block % 4 == l) {
					poly.on<&TimedEnvelope::gate_on>(l);
					TimedEnvelope::gate_on(c[l], s[l]);
				}
				if (block % 4 == (l + 2) % 4) {
					poly.on<&TimedEnvelope::gate_off>(l);
					TimedEnvelope::gate_off(c[l], s[l]);
				}
			}
			++block;
			std::array<std::vector<f32>, 4> out{};
			std::array<fp::Buffer, 4> os{};
			for (std::size_t l = 0; l < 4; ++l) {
				out[l].resize(n);
				os[l] = fp::Buffer{out[l]};
			}
			poly.render_block(os);
			for (std::size_t i = 0; i < n;) {
				std::size_t m = n - i;
				for (std::size_t l = 0; l < 4; ++l) {
					m = std::min(m, kernel_ops::run_length<TimedEnvelope>(c[l], s[l], n - i));
				}
				for (std::size_t l = 0; l < 4; ++l) {
					TimedEnvelope::prepare_run(c[l], s[l], m);
					for (std::size_t t = 0; t < m; ++t) {
						expect(std::bit_cast<uint32_t>(out[l][i + t]))
						    .to_equal(std::bit_cast<uint32_t>(TimedEnvelope::step(c[l], EmptyBlock{}, s[l])));
					}
				}
				i += m;
			}
		}
	});
});
// clang-format on

DELUGE_SPEC(timed_envelope_spec, timed_envelope_spec_body)
