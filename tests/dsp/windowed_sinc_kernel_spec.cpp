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

// Pins the two variant picks of the shared deluge::dsp::fixed::windowed_sinc_kernel: select()
// reproduces the legacy getKernel octave-walk (the wavetable renderer's band-limited choice), and
// variant() indexes a variant by number (sample playback's getWhichKernel choice) into the one
// contiguous [7][17][16] table. The table *values* are pinned end-to-end by the wavetable/ and
// sample/ golden fixtures (and were byte-verified against the legacy table before the two copies
// were unified). Run on x86 (SIMDe) and ARM/QEMU via CTest — pure index logic, dual-arch for parity.

#include "dsp-next/fixed/windowed_sinc_kernel.h"
#include <cppspec.hpp>
#include <cstdint>

namespace {
using deluge::dsp::fixed::kSincKernelPhaseSteps;
using deluge::dsp::fixed::kSincKernelTaps;
using deluge::dsp::fixed::kSincKernelVariants;
using deluge::dsp::fixed::windowed_sinc_kernel;

constexpr int32_t kTapsPerVariant = kSincKernelPhaseSteps * kSincKernelTaps;

// Independent re-derivation of the legacy getKernel ladder, used to check the variant select()
// lands on. Mirrors the legacy signed narrowing of the band limit at the √½ midpoint (see below).
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
describe windowed_sinc_kernel_spec("deluge::dsp::fixed windowed_sinc_kernel", ${
	// variant(0) is the base of the whole contiguous [7][17][16] table.
	const int16_t* base = windowed_sinc_kernel.variant(0);

	it("indexes each variant at its contiguous block base", _ {
		for (int32_t which = 0; which < kSincKernelVariants; ++which) {
			expect(windowed_sinc_kernel.variant(which)).to_equal(base + which * kTapsPerVariant);
		}
	});

	it("selects the variant matching the legacy getKernel ladder", _ {
		// A play rate well below the band limit and below the 0.707 midpoint -> variant 0.
		constexpr int32_t kBand = 200000000;
		// One play rate inside each of the 7 oversampling regions (0 / 0.707 / 1 / √2 / 2 / 2√2 / cap).
		const int32_t rates[] = {kBand / 2,     (kBand * 4) / 5, (kBand * 6) / 5, (kBand * 8) / 5,
		                         (kBand * 5) / 2, kBand * 3,      kBand * 10};
		for (int32_t rate : rates) {
			int32_t expected = reference_variant(rate, kBand);
			expect(windowed_sinc_kernel.select(rate, kBand)).to_equal(windowed_sinc_kernel.variant(expected));
		}
	});

	it("never selects past the last (sharpest-capped) variant", _ {
		// A rate many octaves above the band limit would walk past variant 6 if unguarded.
		constexpr int32_t kBand = 200000000;
		expect(windowed_sinc_kernel.select(2000000000, kBand))
		    .to_equal(windowed_sinc_kernel.variant(kSincKernelVariants - 1));
	});

	it("preserves the legacy signed-narrowing quirk for band limits above 2^31", _ {
		// A band whose limit has the top bit set reads as negative at the √½ midpoint in the legacy
		// int32 ladder, so the +1 nudge always fires: a sub-limit rate lands on variant 1, not 0.
		// (Unsigned arithmetic would give variant 0 — this pins that we faithfully reproduce legacy.)
		constexpr uint32_t kHighBand = 0x90000000u; // ≥ 2^31
		constexpr uint32_t kSubLimitRate = 0x10000000u;
		expect(reference_variant(kSubLimitRate, kHighBand)).to_equal(1);
		expect(windowed_sinc_kernel.select(kSubLimitRate, kHighBand)).to_equal(windowed_sinc_kernel.variant(1));
	});
});
// clang-format on

CPPSPEC_MAIN(windowed_sinc_kernel_spec);
