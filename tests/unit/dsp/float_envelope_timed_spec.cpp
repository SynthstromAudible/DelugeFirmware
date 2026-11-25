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

// fp::Envelope's timed segments: a curve or a raised cosine of a given length from wherever it starts, ended by count,
// retimed and retargeted in place, on Mono and on the lanes of a Poly.

#include "dsp/core/flush_to_zero.h"
#include "dsp/core/mono.h"
#include "dsp/core/poly.h"
#include "dsp/float/envelope/curve.h"
#include "dsp/float/envelope/envelope.h"

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

namespace {

using Env = Mono<fp::Envelope>;

/// A program whose first segment is @p first, ending in a hold at its target.
[[nodiscard]] fp::Program starting_with(fp::Segment<f32> first) {
	fp::Program p;
	p.segments[0] = first;
	p.segments[1] = fp::detail::hold_segment(first.target);
	return p;
}

/// An envelope configured with @p program, set to @p start and gated on.
[[nodiscard]] Env gated(const fp::Program& program, f32 start) {
	Env env;
	// A one-sample line to `start` held there, then the program under test from that level.
	const fp::Program lead = starting_with(fp::detail::linear_segment(start, 1.f, 1.f));
	env.configure({.program = lead});
	env.on<&fp::Envelope::gate_on>();
	(void)env.render();
	env.configure({.program = program});
	env.on<&fp::Envelope::gate_on>();
	return env;
}

/// The curve stage's value at elapsed fraction @p x, in double.
[[nodiscard]] double curve_at(double start, double target, double k, double x) {
	const double falling = k == 0.0 ? 1.0 - x : (std::exp2(-k * x) - std::exp2(-k)) / (1.0 - std::exp2(-k));
	return target + ((start - target) * falling);
}

} // namespace

// clang-format off
describe float_envelope_timed_spec_body("fp::Envelope timed segments", ${
	it("runs a curve of a given length from any start and lands on its target", _ {
		Env env = gated(starting_with(fp::detail::curve_segment(0.8f, 1000.f, 8.f, 1.f)), 0.3f);
		expect(env.state().value).to_equal(0.3f);
		for (int32_t i = 1; i < 1000; ++i) {
			const f32 y = env.render();
			expect(static_cast<double>(y)).to_be_within(0x1p-20).of(curve_at(0.3, 0.8, 8.0, i / 1000.0));
			expect(env.state().index).to_equal(0.f);
		}
		expect(env.render()).to_equal(0.8f);
		expect(env.state().index).to_equal(1.f);
	});

	it("is a straight line at k = 0", _ {
		Env env = gated(starting_with(fp::detail::curve_segment(1.f, 512.f, 0.f, 1.f)), 0.f);
		for (int32_t i = 1; i < 512; ++i) {
			expect(static_cast<double>(env.render())).to_be_within(0x1p-22).of(i / 512.0);
		}
	});

	it("bends the other way for a negative curve", _ {
		Env env = gated(starting_with(fp::detail::curve_segment(0.f, 777.f, -12.5f, 1.f)), 1.f);
		for (int32_t i = 1; i < 777; ++i) {
			expect(static_cast<double>(env.render())).to_be_within(0x1p-20).of(curve_at(1.0, 0.0, -12.5, i / 777.0));
		}
		expect(env.render()).to_equal(0.f);
	});

	it("runs a raised cosine from its start to its target", _ {
		Env env = gated(starting_with(fp::detail::cosine_segment(0.f, 256.f, 1.f)), 0.7f);
		for (int32_t i = 1; i < 256; ++i) {
			const double expected = 0.7 * (0.5 + (0.5 * std::cos(std::numbers::pi * i / 256.0)));
			expect(static_cast<double>(env.render())).to_be_within(0x1p-20).of(expected);
		}
		expect(env.render()).to_equal(0.f);
		expect(env.state().index).to_equal(1.f);
	});

	it("keeps the fraction elapsed when retimed", _ {
		Env env = gated(starting_with(fp::detail::curve_segment(1.f, 1000.f, 4.f, 1.f)), 0.f);
		f32 last = 0.f;
		for (int32_t i = 0; i < 300; ++i) {
			last = env.render();
		}
		env.on<&fp::Envelope::retime>(500.f);
		const f32 first = env.render();
		expect(static_cast<double>(first)).to_be_within(0x1p-20).of(curve_at(0.0, 1.0, 4.0, 151.0 / 500.0));
		to_be_at_most(expect(first - last), 0.01f);
		for (int32_t i = 1; i < 349; ++i) {
			(void)env.render();
			expect(env.state().index).to_equal(0.f);
		}
		expect(env.render()).to_equal(1.f);
		expect(env.state().index).to_equal(1.f);
	});

	it("stays monotone and ends once when retimed every sample", _ {
		Env env = gated(starting_with(fp::detail::curve_segment(1.f, 100.f, 8.f, 1.f)), 0.f);
		f32 last = 0.f;
		int32_t ends = 0;
		for (int32_t i = 0; i < 20000 && env.state().index == 0.f; ++i) {
			env.on<&fp::Envelope::retime>((i % 2) == 0 ? 10000.f : 100.f);
			const f32 y = env.render();
			to_be_at_least(expect(y), last);
			to_be_at_most(expect(y), 1.f);
			last = y;
			ends += env.state().index == 1.f ? 1 : 0;
		}
		expect(ends).to_equal(1);
		expect(last).to_equal(1.f);
	});

	it("lands on a target moved during the stage", _ {
		Env env = gated(starting_with(fp::detail::curve_segment(0.5f, 400.f, 8.f, 1.f)), 1.f);
		for (int32_t i = 0; i < 200; ++i) {
			(void)env.render();
		}
		env.on<&fp::Envelope::retarget>(0.2f);
		for (int32_t i = 0; i < 199; ++i) {
			(void)env.render();
		}
		expect(env.render()).to_equal(0.2f);
		expect(env.state().index).to_equal(1.f);
	});

	it("holds its length at the slowest rate with the gentlest curve", _ {
		constexpr f32 kLength = 8388608.f; // 2^23
		Env env = gated(starting_with(fp::detail::curve_segment(1.f, kLength, 0.125f, 1.f)), 0.f);
		std::vector<f32> block(4096);
		for (int32_t done = 0; done < 4194304; done += 4096) {
			env.render_block(fp::Buffer{block});
		}
		expect(static_cast<double>(env.state().value)).to_be_within(0x1p-18).of(curve_at(0.0, 1.0, 0.125, 0.5));
		for (int32_t done = 4194304; done < 8388608 - 4096; done += 4096) {
			env.render_block(fp::Buffer{block});
		}
		expect(env.state().index).to_equal(0.f);
		for (int32_t i = 0; i < 4095; ++i) {
			(void)env.render();
		}
		expect(env.state().index).to_equal(0.f);
		expect(env.render()).to_equal(1.f);
		expect(env.state().index).to_equal(1.f);
	});

	it("runs timed stages on a Poly's lanes as on Monos, bit for bit", _ {
		fp::FlushToZero ftz;
		const std::array programs{
		    starting_with(fp::detail::curve_segment(1.f, 50.f, 8.f, 1.f)),
		    starting_with(fp::detail::cosine_segment(1.f, 70.f, 1.f)),
		    starting_with(fp::detail::curve_segment(1.f, 33.f, -6.f, 1.f)),
		    fp::adsr(5.f, 8.f, 0.5f, 10.f),
		};
		Poly<fp::Envelope, 4> poly;
		std::array<Env, 4> mono{};
		for (size_t l = 0; l < 4; ++l) {
			poly.configure(l, {.program = programs[l]});
			mono[l].configure({.program = programs[l]});
			poly.template on<&fp::Envelope::gate_on>(l);
			mono[l].template on<&fp::Envelope::gate_on>();
		}
		std::array<std::vector<f32>, 4> block{};
		std::array<fp::Buffer, 4> spans{};
		for (size_t l = 0; l < 4; ++l) {
			block[l].resize(100);
			spans[l] = fp::Buffer{block[l]};
		}
		poly.render_block(spans);
		for (size_t l = 0; l < 4; ++l) {
			for (size_t i = 0; i < 100; ++i) {
				expect(std::bit_cast<uint32_t>(block[l][i])).to_equal(std::bit_cast<uint32_t>(mono[l].render()));
			}
		}
	});
});
// clang-format on

DELUGE_SPEC(float_envelope_timed_spec, float_envelope_timed_spec_body)
