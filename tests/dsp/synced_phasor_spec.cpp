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
#include "dsp-next/fixed/sync/synced_phasor.h"
#include <cppspec.hpp>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {
using deluge::dsp::Phasor;
using deluge::dsp::UQ;
using deluge::dsp::fixed::SyncedPhasor;

static_assert(std::is_same_v<decltype(std::declval<Phasor&>().advance()), UQ<32>>);
static_assert(std::is_same_v<decltype(std::declval<SyncedPhasor&>().advance()), UQ<32>>);

constexpr UQ<32> uq32(uint32_t raw) {
	return UQ<32>::from_raw(raw);
}

constexpr UQ<32> kInc = uq32(0x04000000); // audible increment
} // namespace

// clang-format off
describe synced_phasor("deluge::dsp::fixed SyncedPhasor", ${
	context("a plain Phasor", _ {
		it("wraps modulo one cycle", _ {
			Phasor p{uq32(0xFFFF'FFFFu), uq32(2u)};
			expect(p.advance().raw()).to_equal(1u);
			expect(p.phase().raw()).to_equal(1u);
		});
	});

	context("a resetter too slow to wrap within the block", _ {
		it("free-runs identically to a plain Phasor", _ {
			// ~65536 samples per resetter cycle: no reset over a handful of samples.
			SyncedPhasor synced{Phasor{uq32(0), kInc}, Phasor{uq32(0), uq32(0x00010000u)}, uq32(0)};
			Phasor plain{uq32(0), kInc};
			for (int i = 0; i < 8; ++i) {
				expect(synced.advance().raw()).to_equal(plain.advance().raw());
			}
		});
	});

	context("a resetter wrapping every 4 samples", _ {
		it("free-runs until the crossover, then snaps the phase", _ {
			// resetter += 2^30 each step -> wraps on the 4th advance (0 -> reset).
			SyncedPhasor synced{Phasor{uq32(0), kInc}, Phasor{uq32(0), uq32(0x40000000u)}, uq32(0)};
			Phasor plain{uq32(0), kInc};
			// Samples 0..2: no wrap yet, identical to free-running.
			expect(synced.advance().raw()).to_equal(plain.advance().raw());
			expect(synced.advance().raw()).to_equal(plain.advance().raw());
			expect(synced.advance().raw()).to_equal(plain.advance().raw());
			// Sample 3: resetter wraps -> phase snaps, diverging from the free-running ramp.
			const UQ<32> reset_phase = synced.advance();
			expect(reset_phase != plain.advance()).to_be_truthy();
			// phase() reflects the post-reset value.
			expect(synced.phase().raw()).to_equal(reset_phase.raw());
		});
	});
});
// clang-format on

CPPSPEC_MAIN(synced_phasor);
