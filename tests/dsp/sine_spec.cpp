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

// Behavioural spec for deluge::dsp::fixed sine kernel (dsp-next, Phase 1 / S2).
// Run on BOTH x86 (SIMDe) and ARM/QEMU (real NEON) by tests/dsp/run.sh — the two
// backends must agree, so every assertion here is also an x86-vs-NEON cross-check.
//
// Reference values are computed from the table at fraction == 0 (phases that are
// multiples of 2^24): there the result is exactly table[index] << 16, which makes
// these assertions an exact regression guard — notably for the argon-gather index
// scaling that the FM/pulse OOB bugs came from.

#include "dsp-next/fixed/sine.h"
#include <arm_neon.h> // int32x4_t, vld1q_u32, vgetq_lane_s32 (SIMDe shim on host)
#include <cppspec.hpp>
#include <cstdint>

namespace {
using deluge::dsp::fixed::sine;
using deluge::dsp::fixed::sine_fm_operator;

// The sine table's peak in Q31: the table tops out at the int16 value 32767 (Q15),
// promoted to Q31 by << 16. So full scale here is 0x7FFF0000, not 0x7FFFFFFF.
constexpr int32_t kPeak = 32767 << 16; // 2147418112

// One quarter of the 2^32 phase circle.
constexpr uint32_t kQuarter = 0x40000000u;
constexpr uint32_t kHalf = 0x80000000u;
constexpr uint32_t kThreeQuarter = 0xC0000000u;
constexpr uint32_t kEighth = 0x20000000u;

// Evaluate one phase by broadcasting it across the vector and reading lane 0.
int32_t sineAt(uint32_t phase) {
	int32x4_t out = sine(Argon<uint32_t>{phase});
	return vgetq_lane_s32(out, 0);
}
} // namespace

// clang-format off
describe sine_kernel("deluge::dsp::fixed sine", ${
	context("sine() — cardinal points (fraction zero, exact)", _ {
		it("is zero at phase 0", _ {
			expect(sineAt(0)).to_equal(0);
		});
		it("peaks at +Q31 full scale at the quarter cycle", _ {
			expect(sineAt(kQuarter)).to_equal(kPeak);
		});
		it("returns to zero at the half cycle", _ {
			expect(sineAt(kHalf)).to_equal(0);
		});
		it("troughs at -Q31 full scale at the three-quarter cycle", _ {
			expect(sineAt(kThreeQuarter)).to_equal(-kPeak);
		});
		it("is 1/sqrt(2) of full scale at the eighth cycle", _ {
			// table[32] == 23170 == round(sin(45 deg) * 32767)
			expect(sineAt(kEighth)).to_equal(23170 << 16);
		});
	});

	context("sine() — all four lanes at once", _ {
		it("evaluates {0, 1/4, 1/2, 3/4} cycle as {0, +peak, 0, -peak}", _ {
			// The lane-wise gather regression guard: a wrong index/stride here (the
			// argon-bump OOB class) would skew or alias these.
			const uint32_t phases[4] = {0u, kQuarter, kHalf, kThreeQuarter};
			int32x4_t out = sine(Argon<uint32_t>{vld1q_u32(phases)});
			expect(vgetq_lane_s32(out, 0)).to_equal(0);
			expect(vgetq_lane_s32(out, 1)).to_equal(kPeak);
			expect(vgetq_lane_s32(out, 2)).to_equal(0);
			expect(vgetq_lane_s32(out, 3)).to_equal(-kPeak);
		});
		it("broadcasts a single phase to four identical lanes", _ {
			int32x4_t out = sine(Argon<uint32_t>{kEighth});
			int32_t lane0 = vgetq_lane_s32(out, 0);
			expect(vgetq_lane_s32(out, 1)).to_equal(lane0);
			expect(vgetq_lane_s32(out, 2)).to_equal(lane0);
			expect(vgetq_lane_s32(out, 3)).to_equal(lane0);
		});
	});

	context("sine() — shape", _ {
		it("rises monotonically through the first quarter", _ {
			expect(sineAt(0x00000000u)).to_be_less_than(sineAt(0x10000000u));
			expect(sineAt(0x10000000u)).to_be_less_than(sineAt(kEighth));
			expect(sineAt(kEighth)).to_be_less_than(sineAt(0x30000000u));
			expect(sineAt(0x30000000u)).to_be_less_than(sineAt(kQuarter));
		});
		it("stays within +/- the Q31 peak across the whole circle", _ {
			for (uint32_t k = 0; k < 256; ++k) {
				int32_t s = sineAt(k << 24);          // each table entry, fraction 0
				expect(s).to_be_between(-kPeak, kPeak); // inclusive
			}
		});
		it("is odd-symmetric: sine(p + half) == -sine(p) at fraction-zero points", _ {
			for (uint32_t p : {0x10000000u, kEighth, 0x30000000u, kQuarter}) {
				expect(sineAt(p + kHalf)).to_equal(-sineAt(p));
			}
		});
	});

	context("sine_fm_operator() — scalar FM-feedback step", _ {
		it("is zero at phase 0", _ {
			expect(sine_fm_operator(0, 0)).to_equal(0);
		});
		it("peaks at the quarter cycle reached via the carrier", _ {
			expect(sine_fm_operator(kQuarter, 0)).to_equal(kPeak);
		});
		it("reaches the quarter cycle via the phase-shift (modulation) input", _ {
			// phase_shift is added in the operator's 24-bit phase space; a quarter
			// cycle there is (kQuarter >> 8).
			expect(sine_fm_operator(0, kQuarter >> 8)).to_equal(kPeak);
		});
		it("returns to zero at the half cycle", _ {
			expect(sine_fm_operator(kHalf, 0)).to_equal(0);
		});
		it("agrees with sine() at the quarter cycle (shared table, fraction zero)", _ {
			expect(sine_fm_operator(kQuarter, 0)).to_equal(sineAt(kQuarter));
		});
	});
});
// clang-format on

CPPSPEC_MAIN(sine_kernel);
