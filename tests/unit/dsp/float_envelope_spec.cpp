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

// fp::Envelope against its stages in continuous time: every program, curve and length, at any block size and from
// any sample of a block; stages ending at their exact sample; retriggers from where the level is; lanes in different
// stages each as a scalar copy stepped at their split.

#include "dsp/core/flush_to_zero.h"
#include "dsp/core/mono.h"
#include "dsp/core/poly.h"
#include "dsp/float/envelope/curve.h"
#include "dsp/float/envelope/envelope.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <numbers>
#include <vector>

#include "dsp/float/vca/vca.h"
#include "float_kernel_records.h"
#include "scalar_lanes.h"

#include <random>
#include <span>

#include "support/matchers/value_matchers.h"
#include "support/spec_main.h"

using namespace deluge::dsp;

// As float_envelope_spec: the twin built with the device's float flags holds the kernels to IEEE order.
#pragma clang fp contract(off) reassociate(off) reciprocal(off)

static_assert(generator_kernel<fp::Envelope> && poly_kernel<fp::Envelope> && runs_kernel<fp::Envelope>);

namespace {

constexpr double kTolerance = 0x1p-14;

[[nodiscard]] double falling(double k, double x) {
	x = std::clamp(x, 0.0, 1.0);
	return k == 0.0 ? 1.0 - x : std::expm1(k * std::numbers::ln2 * (1.0 - x)) / std::expm1(k * std::numbers::ln2);
}

/// A program's level in continuous time, samples as the unit.
class Reference {
public:
	explicit Reference(const fp::Program& program) : program_{program} {}

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
	[[nodiscard]] const fp::Segment<f32>& segment() const {
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

	fp::Program program_;
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
[[nodiscard]] double worst_error(const fp::Program& program, std::size_t n, std::size_t samples,
                                 const std::vector<Gate>& gates) {
	Mono<fp::Envelope> env;
	env.configure({.program = program});
	Reference exact{program};
	std::vector<f32> out(n);
	double worst = 0.0;
	for (std::size_t t = 0; t < samples; t += n) {
		for (const Gate& gate : gates) {
			if (gate.at >= t && gate.at < t + n && gate.at == t) {
				if (gate.on) {
					exact.gate_on(static_cast<double>(t), env.state().v);
					env.on<&fp::Envelope::gate_on>();
				}
				else {
					exact.gate_off(static_cast<double>(t), env.state().v);
					env.on<&fp::Envelope::gate_off>();
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
[[nodiscard]] std::size_t samples_to_rest(Mono<fp::Envelope>& env, std::size_t n, std::size_t limit) {
	std::vector<f32> out(n);
	std::size_t t = 0;
	while (t < limit && env.state().index >= 0.f) {
		env.render_block(fp::Buffer{out});
		t += n;
	}
	return t;
}

constexpr std::array<f32, 11> kCurves{-12.5f, -8.f, -4.f, -1.f, -0.25f, 0.f, 0.25f, 1.f, 4.f, 8.f, 12.5f};

using VcaEnv = fp::Vca<fp::Envelope>;

[[nodiscard]] uint32_t bits(f32 x) {
	return std::bit_cast<uint32_t>(x);
}
/// FNV-1a over the output's bits: the digest a record pins.
[[nodiscard]] uint32_t digest(std::span<const f32> out) {
	uint32_t h = 2166136261U;
	for (f32 x : out) {
		h = (h ^ bits(x)) * 16777619U;
	}
	return h;
}
[[nodiscard]] std::vector<f32> noise(std::size_t n, uint32_t seed) {
	std::mt19937 rng{seed};
	std::uniform_real_distribution<f32> d{-1.f, 1.f};
	std::vector<f32> v(n);
	for (f32& x : v) {
		x = d(rng);
	}
	return v;
}

/// N lanes of Vca<Envelope>, each its own program and gain, gated and faded at its own blocks, against scalar copies at
/// the lanes' split and against x · envelope · gain from bare envelopes there.
template <std::size_t N>
void expect_vca_lanes_equal_scalars(auto& self) {
	fp::FlushToZero ftz;
	const std::array<fp::Program, 4> programs{fp::adsr(9.f, 80.f, 0.5f, 110.f, {}), fp::ar(90.f, 120.f, 0.6f, {}),
	                                          fp::ad(60.f, 150.f, {}),
	                                          fp::four_segment({0.8f, 0.3f, 0.6f, 0.1f}, {23.f, 31.f, 17.f, 29.f})};
	Poly<VcaEnv, N> poly;
	run_test::ScalarLanes<VcaEnv, N> scalar;
	run_test::ScalarLanes<fp::Envelope, N> bare;
	const auto gain = [](std::size_t l) { return 0.25f + (0.5f * static_cast<f32>(l)); };
	for (std::size_t l = 0; l < N; ++l) {
		const VcaEnv::Settings settings{.envelope = {.program = programs[l]}, .gain = gain(l)};
		poly.configure(l, settings);
		scalar.configure(l, settings);
		bare.configure(l, {.program = programs[l]});
	}
	bool audible = false;
	for (std::size_t b = 0; b < 12; ++b) {
		constexpr std::size_t kBlock = 37;
		for (std::size_t l = 0; l < N; ++l) {
			if (b == l || b == l + 6) {
				poly.template on<&VcaEnv::gate_on>(l);
				scalar.template on<&VcaEnv::gate_on>(l);
				bare.template on<&fp::Envelope::gate_on>(l);
			}
			else if (b == l + 3) {
				poly.template on<&VcaEnv::gate_off>(l);
				scalar.template on<&VcaEnv::gate_off>(l);
				bare.template on<&fp::Envelope::gate_off>(l);
			}
			else if (b == l + 8) {
				poly.template on<&VcaEnv::fade>(l, 50.f);
				scalar.template on<&VcaEnv::fade>(l, 50.f);
				bare.template on<&fp::Envelope::fade>(l, 50.f);
			}
		}
		std::array<std::vector<f32>, N> in{}, out{}, ref{}, env{};
		std::array<fp::Signal, N> is{};
		std::array<fp::Buffer, N> os{}, rs{}, es{};
		for (std::size_t l = 0; l < N; ++l) {
			in[l] = noise(kBlock, static_cast<uint32_t>(100 + l + (b * N)));
			out[l].resize(kBlock);
			ref[l].resize(kBlock);
			env[l].resize(kBlock);
			is[l] = fp::Signal{in[l]};
			os[l] = fp::Buffer{out[l]};
			rs[l] = fp::Buffer{ref[l]};
			es[l] = fp::Buffer{env[l]};
		}
		poly.render_block(is, os);
		scalar.render_block(is, rs);
		bare.render_block(es);
		for (std::size_t l = 0; l < N; ++l) {
			for (std::size_t i = 0; i < kBlock; ++i) {
				const f32 expected = (in[l][i] * env[l][i]) * gain(l);
				expect(bits(out[l][i])).to_equal(bits(ref[l][i]));
				expect(bits(ref[l][i])).to_equal(bits(expected));
				audible = audible || expected != 0.f;
			}
			const auto ps = poly.state(l);
			const auto ss = scalar.state(l);
			expect(std::bit_cast<std::array<uint32_t, sizeof(ps) / 4>>(ps))
			    .to_equal(std::bit_cast<std::array<uint32_t, sizeof(ss) / 4>>(ss));
		}
	}
	expect(audible).to_be_true();
}

} // namespace

// clang-format off
describe float_envelope_spec_body("fp::Envelope", ${
	it("follows main's curves at every sample, at every length, curve and block size", _ {
		for (const f32 length : {64.f, 100.f, 1000.f, 44100.f}) {
			for (const f32 k : kCurves) {
				const fp::Program p = fp::adsr(length, length, 0.25f, length, {.attack = k, .decay = k, .release = k});
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
				const fp::Program p =
				    fp::adsr(static_cast<f32>(at), 300.f, 0.5f, 100.f, {.attack = 0.f, .decay = k, .release = k});
				to_be_at_most(expect(worst_error(p, 128, 1024, {{0, true}, {768, false}})), kTolerance);
			}
		}
	});

	it("carries the fraction of a sample a stage ends at", _ {
		for (const f32 fraction : {0.1f, 0.5f, 0.93f}) {
			const fp::Program p = fp::adsr(20.f + fraction, 33.f + fraction, 0.5f, 47.f + fraction, {});
			for (const std::size_t n : {std::size_t{1}, std::size_t{32}, std::size_t{128}}) {
				to_be_at_most(expect(worst_error(p, n, 512, {{0, true}, {256, false}})), kTolerance);
			}
		}
	});

	it("ends a stage of a minute within a sample, at any block size", _ {
		constexpr f32 kMinute = 60.f * 44100.f;
		for (const std::size_t n : {std::size_t{1}, std::size_t{128}}) {
			Mono<fp::Envelope> env;
			env.configure({.program = fp::ad(1.f, kMinute, {.attack = 0.f, .decay = 4.f})});
			env.on<&fp::Envelope::gate_on>();
			const std::size_t end = samples_to_rest(env, n, static_cast<std::size_t>(kMinute) + 4096);
			to_be_at_least(expect(end), static_cast<std::size_t>(kMinute));
			to_be_at_most(expect(end), static_cast<std::size_t>(kMinute) + 2 + n);
		}
	});

	it("gives every block size the same levels at the same samples", _ {
		for (const fp::Program& p : {fp::adsr(300.f, 700.f, 0.4f, 500.f, {}), fp::ahd(64.f, 200.f, 300.f, {}),
		                                fp::ad(50.f, 900.f, {}), fp::asr(90.f, 400.f, {}),
		                                fp::four_segment({0.9f, 0.4f, 0.6f, 0.1f}, {130.f, 125.f, 140.f, 150.f})}) {
			for (const std::size_t n : {std::size_t{1}, std::size_t{32}, std::size_t{128}}) {
				to_be_at_most(expect(worst_error(p, n, 3072, {{0, true}, {1280, false}})), kTolerance);
			}
		}
	});

	it("runs stages of one sample in order, and loops them without end in finite time", _ {
		const fp::Program four = fp::four_segment({1.f, 0.25f, 0.75f, 0.f}, {0.f, 1.f, 0.5f, 1.f});
		Mono<fp::Envelope> env;
		env.configure({.program = four});
		env.on<&fp::Envelope::gate_on>();
		std::vector<f32> out(8);
		env.render_block(fp::Buffer{out});
		expect(out[0]).to_be_within(kTolerance).of(1.f);
		expect(out[1]).to_be_within(kTolerance).of(0.25f);
		expect(out[2]).to_be_within(kTolerance).of(0.75f);
		expect(out[7]).to_be_within(kTolerance).of(0.75f);
		Mono<fp::Envelope> loop;
		loop.configure({.program = fp::looping(0.f, 1.f, 0.f)});
		loop.on<&fp::Envelope::gate_on>();
		std::vector<f32> looped(128);
		loop.render_block(fp::Buffer{looped});
		for (std::size_t i = 0; i < looped.size(); ++i) {
			expect(looped[i]).to_be_within(kTolerance).of(i % 2 == 0 ? 1.f : 0.f);
		}
	});

	it("ignores a gate-off where its program has no release, and releases everywhere else", _ {
		for (const std::size_t off : {std::size_t{64}, std::size_t{256}, std::size_t{512}}) {
			for (const fp::Program& p : {fp::ad(128.f, 900.f, {}), fp::ahd(128.f, 300.f, 900.f, {})}) {
				to_be_at_most(expect(worst_error(p, 64, 2048, {{0, true}, {off, false}})), kTolerance);
				Mono<fp::Envelope> env;
				env.configure({.program = p});
				env.on<&fp::Envelope::gate_on>();
				expect(env.state().index).to_equal(0.f);
				env.on<&fp::Envelope::gate_off>();
				expect(env.state().index).to_equal(0.f);
			}
			// A sustain of 0 is no exception: the key-up cuts a decay
			const fp::Program pluck = fp::adsr(16.f, 4000.f, 0.f, 200.f, {});
			Mono<fp::Envelope> env;
			env.configure({.program = pluck});
			env.on<&fp::Envelope::gate_on>();
			std::vector<f32> out(off);
			env.render_block(fp::Buffer{out});
			env.on<&fp::Envelope::gate_off>();
			to_be_at_most(expect(samples_to_rest(env, 1, 4000)), std::size_t{202});
			to_be_at_most(expect(worst_error(pluck, 64, 2048, {{0, true}, {off, false}})), kTolerance);
		}
	});

	it("rests at exactly 0, where a gate-off does nothing", _ {
		Mono<fp::Envelope> env;
		env.configure({.program = fp::adsr(10.f, 10.f, 0.5f, 10.f, {})});
		std::vector<f32> out(64, 1.f);
		env.on<&fp::Envelope::gate_off>();
		expect(env.state().index).to_be_less_than(0.f);
		env.render_block(fp::Buffer{out});
		for (const f32 y : out) {
			expect(y).to_equal(0.f);
		}
		env.on<&fp::Envelope::gate_on>();
		env.render_block(fp::Buffer{out});
		env.on<&fp::Envelope::gate_off>();
		env.render_block(fp::Buffer{out});
		env.render_block(fp::Buffer{out});
		expect(out[63]).to_equal(0.f);
		expect(env.state().index).to_be_less_than(0.f);
	});

	it("retriggers from where the level is, in any stage of any program, an attack for the time its curve has left", _ {
		for (const f32 k : kCurves) {
			for (const fp::Program& p :
			     {fp::adsr(400.f, 700.f, 0.4f, 500.f, {.attack = k, .decay = k, .release = k}),
			      fp::ahd(400.f, 200.f, 300.f, {.attack = k, .decay = k}), fp::asr(400.f, 400.f, {.attack = k, .release = k}),
			      fp::four_segment({0.9f, 0.4f, 0.6f, 0.1f}, {130.f, 125.f, 140.f, 150.f})}) {
				for (std::size_t again = 64; again <= 1600; again += 64) {
					to_be_at_most(expect(worst_error(p, 64, 3072, {{0, true}, {1152, false}, {again, true}})), kTolerance);
				}
			}
		}
	});

	it("moves a held level to a changed one over 32 samples, at any block size", _ {
		for (const std::size_t n : {std::size_t{1}, std::size_t{7}, std::size_t{32}, std::size_t{128}}) {
			Mono<fp::Envelope> env;
			env.configure({.program = fp::adsr(8.f, 8.f, 0.9f, 100.f, {})});
			env.on<&fp::Envelope::gate_on>();
			std::vector<f32> out(n);
			for (std::size_t t = 0; t < 512; t += n) {
				env.render_block(fp::Buffer{out});
			}
			env.configure({.program = fp::adsr(8.f, 8.f, 0.4f, 100.f, {})});
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
					return fp::adsr(8.f, 1024.f, 0.25f, 100.f, {.attack = 0.f, .decay = curve});
				};
				Mono<fp::Envelope> env;
				env.configure({.program = program(4.f)});
				env.on<&fp::Envelope::gate_on>();
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
				Mono<fp::Envelope> env;
				const std::array<f32, 6> lengths{5000.f, 60.f * 44100.f, 300.f, 20000.f, 5000.f, 1e9f};
				double position = 0.0; // of the decay, at the last sample
				std::vector<f32> out(n);
				env.configure({.program = fp::adsr(1.f, lengths[0], 0.25f, 100.f, {.attack = 0.f, .decay = k})});
				env.on<&fp::Envelope::gate_on>();
				std::size_t t = 0;
				for (std::size_t block = 0; block < 1200 && env.state().index < 2.f; ++block) {
					const f32 length = lengths[block % lengths.size()];
					env.configure({.program = fp::adsr(1.f, length, 0.25f, 100.f, {.attack = 0.f, .decay = k})});
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
			Mono<fp::Envelope> env;
			std::vector<f32> out(n);
			env.configure({.program = fp::adsr(1.f, 10000.f, 0.25f, 100.f, {})});
			env.on<&fp::Envelope::gate_on>();
			for (std::size_t t = 0; t < 1280; t += n) {
				env.render_block(fp::Buffer{out});
			}
			f32 last = out[n - 1];
			const f32 begun = last;
			env.configure({.program = fp::adsr(1.f, 1.f, 0.25f, 100.f, {})});
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
			Mono<fp::Envelope> env;
			std::vector<f32> out(32);
			env.configure({.program = fp::adsr(32.f + fraction, 64.f, 0.25f, 100.f, {.attack = 0.f, .decay = 8.f})});
			env.on<&fp::Envelope::gate_on>();
			env.render_block(fp::Buffer{out}); // the attack ends before the next sample
			env.configure({.program = fp::adsr(32.f + fraction, 1e5f, 0.25f, 100.f, {.attack = 0.f, .decay = 8.f})});
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
		Mono<fp::Envelope> env;
		std::vector<f32> out(32);
		f32 last = 0.f;
		for (std::size_t block = 0; block < 4000; ++block) {
			const f32 k = random(-12.5f, 12.5f);
			const f32 length = std::exp2(random(6.f, 18.f));
			const f32 sustain = random(0.f, 1.f);
			env.configure(
			    {.program = fp::adsr(length, length, sustain, length, {.attack = k, .decay = k, .release = k})});
			if (block % 37 == 0) {
				env.on<&fp::Envelope::gate_on>();
			}
			if (block % 37 == 25) {
				env.on<&fp::Envelope::gate_off>();
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

	it("slews a minute as it slews a second, up and down, at any block size", _ {
		for (const f32 seconds : {1.f, 60.f}) {
			for (const f32 k : {0.25f, 1.f, 8.f}) {
				for (const std::size_t n : {std::size_t{1}, std::size_t{32}, std::size_t{128}}) {
					if (n == 1 && seconds > 10.f && k > 1.f) {
						continue;
					}
					const f32 length = seconds * 44100.f;
					Mono<fp::Envelope> env;
					std::vector<f32> out(n);
					env.configure({.program = fp::ar(length, length * 0.5f, 0.75f, {.decay = k})});
					env.on<&fp::Envelope::gate_on>();
					const auto blocks = static_cast<std::size_t>(length) / n;
					double worst = 0.0;
					for (std::size_t b = 0; b < blocks; ++b) {
						env.render_block(fp::Buffer{out});
						const double gone = static_cast<double>((b + 1) * n) / length;
						worst = std::max(worst, std::abs(out[n - 1] - (0.75 * (1.0 - std::exp2(-k * gone)))));
					}
					const double top = out[n - 1];
					env.configure({.program = fp::ar(length, length * 0.5f, 0.25f, {.decay = k})});
					for (std::size_t b = 0; b < blocks / 2; ++b) {
						env.render_block(fp::Buffer{out});
						const double gone = static_cast<double>((b + 1) * n) / (length * 0.5);
						const bool falls = top > 0.25;
						const double run = falls ? gone : gone * 0.5;
						worst = std::max(worst, std::abs(out[n - 1] - (0.25 + ((top - 0.25) * std::exp2(-k * run)))));
					}
					to_be_at_most(expect(worst), 0x1p-13);
				}
			}
		}
	});

	it("slews every sample along the curve, within a block as at its end", _ {
		for (const f32 k : {0.25f, 1.f, 4.f, 8.f, 12.5f}) {
			for (const f32 length : {100.f, 1000.f, 44100.f}) {
				Mono<fp::Envelope> env;
				std::vector<f32> out(128);
				env.configure({.program = fp::ar(length, length, 0.75f, {.decay = k})});
				env.on<&fp::Envelope::gate_on>();
				for (std::size_t t = 0; t < 2048; t += 128) {
					env.render_block(fp::Buffer{out});
					for (std::size_t i = 0; i < 128; ++i) {
						const double gone = static_cast<double>(t + i + 1) / length;
						expect(out[i]).to_be_within(kTolerance).of(0.75 * (1.0 - std::exp2(-k * gone)));
					}
				}
			}
		}
	});

	it("slews in a line at a curve of 0 or less, and arrives at its sample", _ {
		for (const f32 k : {0.f, -2.f, -12.5f}) {
			for (const f32 length : {100.f, 333.3f, 1000.f}) {
				for (const std::size_t n : {std::size_t{1}, std::size_t{32}, std::size_t{128}}) {
					Mono<fp::Envelope> env;
					std::vector<f32> out(n);
					env.configure({.program = fp::ar(length, length * 2.f, 0.6f, {.decay = k})});
					env.on<&fp::Envelope::gate_on>();
					for (std::size_t t = 0; t < 1024; t += n) {
						env.render_block(fp::Buffer{out});
						for (std::size_t i = 0; i < n; ++i) {
							const double line = static_cast<double>(t + i + 1) / length;
							expect(out[i]).to_be_within(kTolerance).of(std::min(line, 0.6));
						}
					}
					expect(out[n - 1]).to_equal(0.6f);
					env.configure({.program = fp::ar(length, length * 2.f, 0.1f, {.decay = k})});
					for (std::size_t t = 0; t < 1280; t += n) {
						env.render_block(fp::Buffer{out});
						for (std::size_t i = 0; i < n; ++i) {
							const double line = 0.6 - (static_cast<double>(t + i + 1) / (length * 2.0));
							expect(out[i]).to_be_within(kTolerance).of(std::max(line, 0.1));
						}
					}
				}
			}
		}
	});

	it("follows a target moved every block, without a jump", _ {
		for (const f32 k : {-1.f, 0.f, 1.f, 8.f}) {
			Mono<fp::Envelope> env;
			std::vector<f32> out(32);
			env.configure({.program = fp::ar(600.f, 900.f, 0.5f, {.decay = k})});
			env.on<&fp::Envelope::gate_on>();
			double level = 0.0;
			for (std::size_t block = 0; block < 400; ++block) {
				const double target = 0.5 + (0.4 * std::sin(0.11 * static_cast<double>(block)));
				env.configure({.program = fp::ar(600.f, 900.f, static_cast<f32>(target), {.decay = k})});
				env.render_block(fp::Buffer{out});
				const double start = level;
				for (std::size_t i = 0; i < 32; ++i) {
					const double gone = static_cast<double>(i + 1) / (target > start ? 600.0 : 900.0);
					level = k > 0.f ? target + ((start - target) * std::exp2(-k * gone))
					                : start + std::clamp(target - start, -gone, gone);
					expect(out[i]).to_be_within(kTolerance * 2.0).of(level);
				}
			}
		}
	});

	it("releases from a slew, and retriggers into one, from the level there is", _ {
		Mono<fp::Envelope> env;
		std::vector<f32> out(64);
		env.configure({.program = fp::ar(1000.f, 500.f, 0.8f, {})});
		env.on<&fp::Envelope::gate_on>();
		env.render_block(fp::Buffer{out});
		const double held = out[63];
		env.on<&fp::Envelope::gate_off>();
		env.render_block(fp::Buffer{out});
		for (std::size_t i = 0; i < 64; ++i) {
			expect(out[i]).to_be_within(kTolerance).of(held * falling(8.0, static_cast<double>(i + 1) / 500.0));
		}
		const double left = out[63];
		env.on<&fp::Envelope::gate_on>();
		env.render_block(fp::Buffer{out});
		for (std::size_t i = 0; i < 64; ++i) {
			const double gone = static_cast<double>(i + 1) / 1000.0;
			expect(out[i]).to_be_within(kTolerance).of(0.8 + ((left - 0.8) * std::exp2(-8.0 * gone)));
		}
	});

	it("fades along a raised cosine from any stage of any program, to 0 in its length, and stays", _ {
		for (const fp::Program& p :
		     {fp::adsr(300.f, 400.f, 0.5f, 600.f, {}), fp::ahd(300.f, 200.f, 500.f, {}),
		      fp::ar(400.f, 300.f, 0.7f, {}), fp::ar(400.f, 300.f, 0.7f, {.decay = 0.f}),
		      fp::four_segment({0.9f, 0.4f, 0.6f, 0.1f}, {130.f, 125.f, 140.f, 150.f})}) {
			for (std::size_t at = 64; at <= 960; at += 64) {
				for (const f32 length : {1.f, 5.f, 128.f, 300.f}) {
					for (const std::size_t n : {std::size_t{1}, std::size_t{32}, std::size_t{128}}) {
						Mono<fp::Envelope> env;
						std::vector<f32> out(n);
						env.configure({.program = p});
						env.on<&fp::Envelope::gate_on>();
						std::vector<f32> before(64);
						for (std::size_t t = 0; t < at; t += 64) {
							if (t == 640) {
								env.on<&fp::Envelope::gate_off>();
							}
							env.render_block(fp::Buffer{before});
						}
						const double from = env.state().v;
						env.on<&fp::Envelope::fade>(length);
						for (std::size_t t = 0; t < 512; t += n) {
							env.render_block(fp::Buffer{out});
							for (std::size_t i = 0; i < n; ++i) {
								const double x = std::min(1.0, static_cast<double>(t + i + 1) / length);
								expect(out[i]).to_be_within(kTolerance).of(
								    from * (0.5 + (0.5 * std::cos(std::numbers::pi * x))));
							}
						}
						expect(out[n - 1]).to_equal(0.f);
						expect(env.state().index).to_be_less_than(0.f);
					}
				}
			}
		}
	});

	it("never slows a fade: the shorter of what is left and what is asked, and a gate-off is nothing to it", _ {
		const auto rests_after = [](f32 first, std::size_t wait, f32 second, bool gate_off) {
			Mono<fp::Envelope> env;
			env.configure({.program = fp::adsr(8.f, 8.f, 0.5f, 9000.f, {})});
			env.on<&fp::Envelope::gate_on>();
			std::vector<f32> out(64);
			env.render_block(fp::Buffer{out});
			env.on<&fp::Envelope::fade>(first);
			std::vector<f32> part(wait);
			env.render_block(fp::Buffer{part});
			if (gate_off) {
				env.on<&fp::Envelope::gate_off>();
			}
			else {
				env.on<&fp::Envelope::fade>(second);
			}
			return wait + samples_to_rest(env, 1, 2000);
		};
		to_be_at_most(expect(rests_after(128.f, 64, 1000.f, false)), std::size_t{129});
		to_be_at_most(expect(rests_after(1000.f, 64, 100.f, false)), std::size_t{165});
		to_be_at_least(expect(rests_after(1000.f, 64, 100.f, false)), std::size_t{164});
		to_be_at_most(expect(rests_after(128.f, 64, 0.f, true)), std::size_t{129});
	});

	it("does not fade at rest, and retriggers out of a fade from its level", _ {
		Mono<fp::Envelope> env;
		env.configure({.program = fp::adsr(400.f, 8.f, 0.5f, 100.f, {.attack = 0.f})});
		env.on<&fp::Envelope::fade>(64.f);
		expect(env.state().index).to_be_less_than(0.f);
		env.on<&fp::Envelope::gate_on>();
		std::vector<f32> out(128);
		env.render_block(fp::Buffer{out});
		env.on<&fp::Envelope::fade>(128.f);
		std::vector<f32> half(64);
		env.render_block(fp::Buffer{half});
		const double from = half[63];
		env.on<&fp::Envelope::gate_on>();
		env.render_block(fp::Buffer{out});
		for (std::size_t i = 0; i < 128; ++i) {
			expect(out[i]).to_be_within(kTolerance).of(from + (static_cast<double>(i + 1) / 400.0));
		}
	});

	it("steps its lanes, in different stages, as scalar copies at the lanes' split", _ {
		const std::array<fp::Program, 4> programs{
		    fp::adsr(20.5f, 310.f, 0.5f, 60.f, {}), fp::four_segment({0.9f, 0.4f, 0.6f, 0.1f}, {30.f, 25.f, 40.f, 50.f}),
		    fp::asr(15.f, 90.f, {.attack = -6.f, .release = -12.5f}), fp::looping(0.1f, 0.8f, 17.3f)};
		const std::array<fp::Program, 4> others{fp::ar(70.f, 90.f, 0.8f, {}), fp::ar(70.f, 90.f, 0.3f, {.decay = -1.f}),
		                                           fp::adsr(90.f, 70.f, 0.1f, 80.f, {}), fp::ahd(9.f, 40.f, 80.f, {})};
		for (const std::array<fp::Program, 4>& set : {programs, others}) {
			Poly<fp::Envelope, 4> poly;
			std::array<fp::Envelope::Coefficients, 4> c{};
			std::array<fp::Envelope::State, 4> s{};
			const auto configure = [&](std::size_t l, const fp::Program& p) {
				poly.configure(l, {.program = p});
				c[l] = fp::Envelope::coefficients({.program = p});
			};
			for (std::size_t l = 0; l < 4; ++l) {
				configure(l, set[l]);
			}
			std::size_t block = 0;
			for (const std::size_t n : {std::size_t{128}, std::size_t{37}, std::size_t{1}, std::size_t{0}, std::size_t{64},
			                            std::size_t{128}, std::size_t{5}, std::size_t{128}, std::size_t{128}, std::size_t{33}}) {
				for (std::size_t l = 0; l < 4; ++l) {
					if (block % 4 == l) {
						poly.on<&fp::Envelope::gate_on>(l);
						fp::Envelope::gate_on(c[l], s[l]);
					}
					if (block % 4 == (l + 2) % 4) {
						poly.on<&fp::Envelope::gate_off>(l);
						fp::Envelope::gate_off(c[l], s[l]);
					}
				}
				if (block == 5) { // one lane fades and one changes its settings while the others go on
					poly.on<&fp::Envelope::fade>(0, 77.f);
					fp::Envelope::fade(s[0], 77.f);
					configure(1, fp::ar(33.f, 400.f, 0.9f, {.decay = 2.f}));
					configure(2, fp::adsr(60.f, 900.f, 0.35f, 70.f, {.decay = -3.f}));
				}
				++block;
				std::array<std::vector<f32>, 4> out{};
				std::array<fp::Buffer, 4> os{};
				for (std::size_t l = 0; l < 4; ++l) {
					out[l].resize(n);
					os[l] = fp::Buffer{out[l]};
					(void)fp::Envelope::prepare(c[l], s[l], n);
				}
				poly.render_block(os);
				for (std::size_t i = 0; i < n;) {
					std::size_t m = n - i;
					for (std::size_t l = 0; l < 4; ++l) {
						m = std::min(m, kernel_ops::run_length<fp::Envelope>(c[l], s[l], n - i));
					}
					for (std::size_t l = 0; l < 4; ++l) {
						fp::Envelope::prepare_run(c[l], s[l], m);
						for (std::size_t t = 0; t < m; ++t) {
							expect(std::bit_cast<uint32_t>(out[l][i + t]))
							    .to_equal(std::bit_cast<uint32_t>(fp::Envelope::step(c[l], EmptyBlock{}, s[l])));
						}
					}
					i += m;
				}
			}
		}
	});

	it("gives a Vca its level, its runs and its events, bit for bit, in lanes of their own", _ {
		expect_vca_lanes_equal_scalars<2>(self);
		expect_vca_lanes_equal_scalars<4>(self);
	});

	it("matches its records", _ {
		fp::FlushToZero ftz;
		const auto record = [](const fp::Program& program) {
			Mono<fp::Envelope> env;
			env.configure({.program = program});
			env.on<&fp::Envelope::gate_on>();
			std::vector<f32> out(1200);
			env.render_block(fp::Buffer{out}.subspan(0, 500));
			env.on<&fp::Envelope::gate_off>();
			env.render_block(fp::Buffer{out}.subspan(500, 500));
			env.on<&fp::Envelope::fade>(128.f);
			env.render_block(fp::Buffer{out}.subspan(1000));
			return digest(out);
		};
		expect(record(fp::adsr(130.f, 160.f, 0.5f, 600.f, {}))).to_equal(records::kEnvAdsr);
		expect(record(fp::four_segment({0.8f, 0.3f, 0.6f, 0.1f}, {50.f, 40.f, 30.f, 60.f})))
		    .to_equal(records::kEnvFourSegment);
		expect(record(fp::ar(700.f, 900.f, 0.8f, {}))).to_equal(records::kEnvAr);
	});
});
// clang-format on

DELUGE_SPEC(float_envelope_spec, float_envelope_spec_body)
