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

// fixed::EnvelopeStage: a stage's position moved on by a rate, its end on the exact sample, and the time past it
// carried into the next stage.

#include "dsp/fixed/envelope/stage.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <optional>

#include "support/matchers/value_matchers.h"
#include "support/spec_main.h"

using namespace deluge::dsp::fixed;

// clang-format off
describe fixed_envelope_stage_spec_body("fixed::EnvelopeStage", ${
	it("moves by the rate times the samples, and ends with what it ran past the end", _ {
		EnvelopeStage stage;
		expect(stage.advance(1000, 32).has_value()).to_equal(false);
		expect(stage.position).to_equal(uint32_t{32000});
		stage.position = kEnvelopeStageEnd - 500;
		const std::optional<uint64_t> over = stage.advance(1000, 32);
		expect(over.has_value()).to_equal(true);
		expect(over.value_or(0)).to_equal(uint64_t{31500});
		// Reaching the end exactly is the end, with nothing over
		stage.position = kEnvelopeStageEnd - 32000;
		expect(stage.advance(1000, 32).value_or(99)).to_equal(uint64_t{0});
	});

	it("sums a rate that changes every block exactly", _ {
		EnvelopeStage stage;
		uint64_t want = 0;
		for (uint32_t b = 0; b < 400; ++b) {
			const uint32_t rate = (b % 2) == 0 ? 27962 / 64 : 1677 / 8;
			want += uint64_t{rate} * 32;
			expect(stage.advance(rate, 32).has_value()).to_equal(false);
			expect(uint64_t{stage.position}).to_equal(want);
		}
	});

	it("starts the next stage at its exact time, the fraction of a sample included", _ {
		// 13,981 steps a sample ends a stage at sample 600.018; the next stage, at 9,320, has run 7.982 of 608
		EnvelopeStage stage;
		std::optional<uint64_t> over;
		uint32_t blocks = 0;
		while (!over) {
			over = stage.advance(13981, 32);
			++blocks;
		}
		expect(blocks).to_equal(19U);
		const double exact = (608.0 - (8388608.0 / 13981.0)) * 9320.0;
		expect(static_cast<double>(carried(*over, 13981, 9320))).to_be_within(1.0).of(exact);
	});

	it("takes any rate at any block size", _ {
		for (uint32_t rate : {0U, 1U, 1U << 23, 0x7FFF'FFFFU, 0xFFFF'FFFFU}) {
			for (uint32_t n : {1U, 128U}) {
				EnvelopeStage stage;
				const std::optional<uint64_t> over = stage.advance(rate, n);
				const uint64_t moved = uint64_t{rate} * n;
				expect(over.has_value()).to_equal(moved >= kEnvelopeStageEnd);
				if (!over) {
					expect(uint64_t{stage.position}).to_equal(moved);
					continue;
				}
				for (uint32_t next : {0U, 1U, 9320U, 1U << 23, 0xFFFF'FFFFU}) {
					// The time past the end is under n samples, and the next stage runs at 2^23 a sample at most
					const uint64_t at = carried(*over, rate, next);
					to_be_at_most(expect(at), uint64_t{n} << 23);
					if (next == 0) {
						expect(at).to_equal(uint64_t{0});
					}
				}
			}
		}
	});

	it("carries the time through three stages that end in one block", _ {
		// An instant attack, a hold of a sample and a decay of 50 in a block of 128: all but the decay's 50 past
		EnvelopeStage stage;
		const uint64_t after_attack = stage.advance(1U << 23, 128).value_or(0);
		expect(after_attack).to_equal(uint64_t{127} << 23);
		const uint64_t in_hold = carried(after_attack, 1U << 23, 1U << 23);
		expect(in_hold - kEnvelopeStageEnd).to_equal(uint64_t{126} << 23);
		const uint64_t in_decay = carried(in_hold - kEnvelopeStageEnd, 1U << 23, kEnvelopeStageEnd / 50);
		expect(in_decay).to_equal(uint64_t{126} * (kEnvelopeStageEnd / 50));
		to_be_at_least(expect(in_decay), uint64_t{kEnvelopeStageEnd});
	});
});
// clang-format on

DELUGE_SPEC(fixed_envelope_stage_spec, fixed_envelope_stage_spec_body)
