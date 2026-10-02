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

/// Differential DSP test driver.
///
/// Exercises the portable DSP primitives over a deterministic input set and prints every result.
/// The SAME source is built two ways — native ARM/NEON (the `#if defined(__arm__)` fast paths, run
/// under qemu) and the host x86 fallback (the `#else` portable branch used by the host-sim/SIMDe
/// build). The two outputs MUST be byte-identical; any diff is a host-portability bug (this is how
/// the `signed_saturate`/`add_saturate`/`subtract_saturate` stub bugs and the q31_* gaps are caught,
/// and a regression guard going forward). See run.sh.

#include "util/fixedpoint.h"

#include <cstdint>
#include <cstdio>

namespace {

// Deterministic LCG so both builds see identical inputs.
uint32_t g_state = 0x12345678u;
int32_t next32() {
	g_state = g_state * 1664525u + 1013904223u;
	return static_cast<int32_t>(g_state);
}

// Interesting edge values, exercised pairwise as well as against random inputs.
constexpr int32_t kEdges[] = {
    0,
    1,
    -1,
    2,
    -2,
    0x40000000,
    -0x40000000,
    0x7FFFFFFF,
    INT32_MIN,
    0x7FFFFFFE,
    INT32_MIN + 1,
    1 << 16,
    -(1 << 16),
    1 << 24,
    -(1 << 24),
    1 << 28,
    -(1 << 28),
    1000000,
    -1000000,
    123456789,
    -123456789,
};
constexpr int kNumEdges = static_cast<int>(sizeof(kEdges) / sizeof(kEdges[0]));

void emitPair(int32_t a, int32_t b) {
	// Q31 multiplies / accumulates (the smmul/smmla family).
	printf("%d %d %d %d %d %d %d %d ", q31_mult(a, b), q31tRescale(a, static_cast<uint32_t>(b)),
	       multiply_32x32_rshift32(a, b), multiply_32x32_rshift32_rounded(a, b),
	       multiply_accumulate_32x32_rshift32(a, a, b), multiply_accumulate_32x32_rshift32_rounded(a, a, b),
	       multiply_subtract_32x32_rshift32_rounded(a, a, b), toPositive(a));

	// Saturating ops (qadd/qsub/ssat) — where the host stubs were wrong.
	printf("%d %d ", add_saturate(a, b), subtract_saturate(a, b));
	printf("%d %d %d %d %d %d ", signed_saturate<8>(a), signed_saturate<12>(a), signed_saturate<16>(a),
	       signed_saturate<24>(a), signed_saturate<30>(a), signed_saturate<32>(a));

	// clz, and the float<->q31 conversions (vcvt).
	printf("%d ", clz(static_cast<uint32_t>(a)));
	float f = static_cast<float>(b) / 2147483648.0f; // b mapped into roughly [-1, 1)
	printf("%d ", q31_from_float(f));
	// Round-tripping the q31 back to float and re-quantising keeps the dump integer-comparable.
	printf("%d\n", q31_from_float(q31_to_float(a)));
}

} // namespace

int main() {
	// 1. Every edge against every edge.
	for (int i = 0; i < kNumEdges; ++i) {
		for (int j = 0; j < kNumEdges; ++j) {
			emitPair(kEdges[i], kEdges[j]);
		}
	}
	// 2. Edges against random.
	for (int i = 0; i < kNumEdges; ++i) {
		for (int k = 0; k < 64; ++k) {
			emitPair(kEdges[i], next32());
		}
	}
	// 3. Random against random. NB: sequence the two next32() calls explicitly — passing
	// `emitPair(next32(), next32())` leaves their evaluation order unspecified, and x86 vs ARM
	// pick different orders, which would swap a/b and create spurious "divergence".
	for (int k = 0; k < 20000; ++k) {
		int32_t a = next32();
		int32_t b = next32();
		emitPair(a, b);
	}
	return 0;
}
