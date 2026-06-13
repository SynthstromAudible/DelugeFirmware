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

// Behavioural spec for deluge::dsp::fixed::SyncedPhasor (the crude per-sample hard-sync phase
// source), run on x86 (SIMDe) and ARM/QEMU via CTest.

#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/synced_phasor.h"
#include <cppspec.hpp>
#include <cstdint>

namespace {
using deluge::dsp::Phasor;
using deluge::dsp::fixed::SyncedPhasor;

constexpr uint32_t kInc = 0x04000000; // audible increment

constexpr int32_t resetterReciprocal(uint32_t increment) {
	return static_cast<int32_t>(2147483648u / static_cast<uint16_t>((increment + 65535) >> 16));
}
} // namespace

// clang-format off
describe synced_phasor("deluge::dsp::fixed SyncedPhasor", ${
	context("a resetter too slow to wrap within the block", _ {
		it("free-runs identically to a plain Phasor", _ {
			// ~65536 samples per resetter cycle: no reset over a handful of samples.
			SyncedPhasor synced{Phasor{0u, kInc}, Phasor{0u, 0x00010000u}, resetterReciprocal(0x00010000u), 0u};
			Phasor plain{0u, kInc};
			for (int i = 0; i < 8; ++i) {
				expect(synced.advance()).to_equal(plain.advance());
			}
		});
	});

	context("a resetter wrapping every 4 samples", _ {
		it("free-runs until the crossover, then snaps the phase", _ {
			// resetter += 2^30 each step -> wraps on the 4th advance (0 -> reset).
			SyncedPhasor synced{Phasor{0u, kInc}, Phasor{0u, 0x40000000u}, resetterReciprocal(0x40000000u), 0u};
			Phasor plain{0u, kInc};
			// Samples 0..2: no wrap yet, identical to free-running.
			expect(synced.advance()).to_equal(plain.advance());
			expect(synced.advance()).to_equal(plain.advance());
			expect(synced.advance()).to_equal(plain.advance());
			// Sample 3: resetter wraps -> phase snaps, diverging from the free-running ramp.
			uint32_t reset_phase = synced.advance();
			expect(reset_phase != plain.advance()).to_be_truthy();
			// phase() reflects the post-reset value.
			expect(synced.phase()).to_equal(reset_phase);
		});
	});
});
// clang-format on

CPPSPEC_MAIN(synced_phasor);
