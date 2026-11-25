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

// fp::Envelope::advance_block: n samples at once, against n steps, through stage ends at their exact samples.

#include "dsp/float/envelope/envelope.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <vector>

#include "support/matchers/value_matchers.h"
#include "support/spec_main.h"

using namespace deluge::dsp;
using E = fp::Envelope;

namespace {

/// A curve, then a cosine, then a hold.
[[nodiscard]] fp::Program timed_chain() {
	fp::Program p;
	p.segments[0] = fp::detail::curve_segment(1.f, 300.f, 8.f, 1.f);
	p.segments[1] = fp::detail::cosine_segment(0.25f, 211.f, 2.f);
	p.segments[2] = fp::detail::hold_segment(0.25f);
	return p;
}

[[nodiscard]] std::vector<fp::Program> programs() {
	fp::Program curve;
	curve.segments[0] = fp::detail::curve_segment(0.9f, 640.f, -6.f, fp::kOff);
	return {fp::adsr(50.f, 80.f, 0.5f, 100.f), fp::ad(60.f, 150.f), timed_chain(), curve,
	        fp::four_segment({0.8f, 0.3f, 0.6f, 0.1f}, {23.f, 31.f, 17.f, 29.f})};
}

/// The program's envelope gated on.
[[nodiscard]] E::State gated(const E::Coefficients& c) {
	E::State s;
	E::gate_on(c, s);
	return s;
}

} // namespace

// clang-format off
describe float_envelope_advance_spec_body("fp::Envelope::advance_block", ${
	it("equals n steps for every kind and block size", _ {
		for (const fp::Program& program : programs()) {
			const E::Coefficients c = E::coefficients({.program = program});
			for (uint32_t n : {1U, 2U, 5U, 32U, 128U, 1000U}) {
				E::State blocked = gated(c);
				E::State stepped = gated(c);
				for (int32_t block = 0; block < 12; ++block) {
					const f32 y = E::advance_block(c, blocked, n);
					f32 z = stepped.value;
					for (uint32_t i = 0; i < n; ++i) {
						z = E::step(c, EmptyBlock{}, stepped);
					}
					expect(y).to_be_within(0x1p-20f).of(z);
					expect(blocked.index).to_equal(stepped.index);
					expect(blocked.elapsed).to_equal(stepped.elapsed);
				}
			}
		}
	});

	it("goes through a stage end inside the block into the next stage", _ {
		fp::Program p;
		p.segments[0] = fp::detail::curve_segment(1.f, 10.f, 4.f, 1.f);
		p.segments[1] = fp::detail::curve_segment(0.f, 20.f, 4.f, fp::kOff);
		const E::Coefficients c = E::coefficients({.program = p});
		E::State s = gated(c);
		(void)E::advance_block(c, s, 25);
		expect(s.index).to_equal(1.f);
		expect(s.elapsed).to_equal(15.f);
	});

	it("does not move a hold or an envelope at rest", _ {
		fp::Program p;
		p.segments[0] = fp::detail::hold_segment(0.4f);
		const E::Coefficients c = E::coefficients({.program = p});
		E::State rest;
		E::State held = gated(c);
		for (uint32_t n : {1U, 128U, 100000U}) {
			expect(E::advance_block(c, rest, n)).to_equal(0.f);
			expect(rest.index).to_equal(fp::kOff);
			expect(E::advance_block(c, held, n)).to_equal(0.f);
			expect(held.index).to_equal(0.f);
		}
	});

	it("ends the slowest stage in the block that holds its last sample", _ {
		fp::Program p;
		p.segments[0] = fp::detail::curve_segment(1.f, 8388608.f, 8.f, 1.f);
		p.segments[1] = fp::detail::hold_segment(1.f);
		const E::Coefficients c = E::coefficients({.program = p});
		E::State s = gated(c);
		uint32_t blocks = 0;
		while (s.index == 0.f) {
			(void)E::advance_block(c, s, 128);
			++blocks;
		}
		expect(blocks).to_equal(8388608U / 128U);
		expect(s.value).to_equal(1.f);
	});
});
// clang-format on

DELUGE_SPEC(float_envelope_advance_spec, float_envelope_advance_spec_body)
