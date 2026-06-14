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

// Pins the dsp-next wavetable sinc kernel (a private copy of the legacy dsp/interpolate
// windowedSincKernel) to the original: the table values are byte-identical, and select_sinc_kernel
// reproduces the legacy getKernel octave-walk across all 7 oversampling variants. Run on x86
// (SIMDe) and ARM/QEMU via CTest — pure index/data logic, kept dual-arch for parity.

#include "dsp-next/fixed/sinc_kernel.h"
#include <cppspec.hpp>
#include <cstdint>
#include <cstring>

// The legacy kernel table (defined in src/deluge/dsp/interpolate/interpolate.cpp, linked in as a
// spec dependency) — the bit-exact oracle for our private copy.
extern const int16_t windowedSincKernel[][17][16];

namespace {
using deluge::dsp::fixed::kSincKernelPhaseSteps;
using deluge::dsp::fixed::kSincKernelTaps;
using deluge::dsp::fixed::kSincKernelVariants;
using deluge::dsp::fixed::select_sinc_kernel;

constexpr int32_t kTapsPerVariant = kSincKernelPhaseSteps * kSincKernelTaps;

// Independent re-derivation of the legacy ladder, used to check the variant select() lands on.
// Mirrors the legacy getKernel signed narrowing of the band limit at the √½ midpoint (see below).
int32_t reference_variant(uint32_t phase_increment, uint32_t band_max) {
	int32_t variant = 0;
	uint32_t rate = phase_increment;
	while (rate >= band_max && variant < 6) {
		variant += 2;
		rate >>= 1;
	}
	if (variant < 6 && rate >= (static_cast<int32_t>(band_max) * 0.707)) {
		variant++;
	}
	return variant;
}
} // namespace

// clang-format off
describe sinc_kernel("deluge::dsp::fixed sinc kernel", ${
	// A play rate well below the band limit and below the 0.707 midpoint -> variant 0, whose base
	// is the base of the whole contiguous [7][17][16] table.
	constexpr int32_t kBand = 200000000;
	const int16_t* base = select_sinc_kernel(0, kBand);

	it("copies the windowed-sinc table bit-for-bit from the legacy kernel", _ {
		expect(std::memcmp(base, &windowedSincKernel[0][0][0],
		                   sizeof(int16_t) * kSincKernelVariants * kTapsPerVariant))
		    .to_equal(0);
	});

	it("selects the variant matching the legacy getKernel ladder", _ {
		// One play rate inside each of the 7 oversampling regions (0 / 0.707 / 1 / √2 / 2 / 2√2 / cap).
		const int32_t rates[] = {kBand / 2,     (kBand * 4) / 5, (kBand * 6) / 5, (kBand * 8) / 5,
		                         (kBand * 5) / 2, kBand * 3,      kBand * 10};
		for (int32_t rate : rates) {
			int32_t expected = reference_variant(rate, kBand);
			expect(select_sinc_kernel(rate, kBand)).to_equal(base + expected * kTapsPerVariant);
		}
	});

	it("never selects past the last (sharpest-capped) variant", _ {
		// A rate many octaves above the band limit would walk past variant 6 if unguarded.
		expect(select_sinc_kernel(2000000000, kBand)).to_equal(base + (kSincKernelVariants - 1) * kTapsPerVariant);
	});

	it("preserves the legacy signed-narrowing quirk for band limits above 2^31", _ {
		// A band whose limit has the top bit set reads as negative at the √½ midpoint in the legacy
		// int32 ladder, so the +1 nudge always fires: a sub-limit rate lands on variant 1, not 0.
		// (Unsigned arithmetic would give variant 0 — this pins that we faithfully reproduce legacy.)
		constexpr uint32_t kHighBand = 0x90000000u; // ≥ 2^31
		constexpr uint32_t kSubLimitRate = 0x10000000u;
		expect(reference_variant(kSubLimitRate, kHighBand)).to_equal(1);
		expect(select_sinc_kernel(kSubLimitRate, kHighBand)).to_equal(base + 1 * kTapsPerVariant);
	});
});
// clang-format on

CPPSPEC_MAIN(sinc_kernel);
