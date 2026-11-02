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
#include "sync_reference.h"
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

/// A deterministic spread of words.
struct XorShift {
	uint32_t x = 0x9E3779B9u;
	uint32_t operator()() {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		return x;
	}
};

/// Whether the raw reference's reciprocal is right for @p increment: at or below 2^16, and above 0xFFFF0000, it is
/// negative.
constexpr bool reference_holds(uint32_t increment) {
	return increment == 0 || (increment > 65536 && increment <= 0xFFFF0000u);
}

/// Resetter increments a sweep can't miss: zero, the reciprocal's round-up edges, the slowest masters and the top.
constexpr uint32_t kEdgeIncrements[] = {0u,          1u,         65535u, 65536u, 65537u, 131072u, 0x10000u << 4,
                                        0xFFFF0000u, 0xFFFFFFFFu};
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

	context("a master at either end of the increments", _ {
		it("has a positive reciprocal for every increment", _ {
			int32_t bad = 0;
			for (uint32_t increment = 1; increment <= 65536; ++increment) {
				bad += deluge::dsp::fixed::resetter_reciprocal(increment) <= 0;
			}
			for (uint32_t increment = 0xFFFF0000u; increment != 0; ++increment) {
				bad += deluge::dsp::fixed::resetter_reciprocal(increment) <= 0;
			}
			expect(bad).to_equal(0);
		});
		it("resets within one audible increment of the exact phase", _ {
			// The master wraps on the first step with an overshoot of a quarter, a half or all but one of its
			// increment; the exact reset is the retrigger phase plus that fraction of the audible increment.
			constexpr uint32_t kAudible = 0x04000000u;
			constexpr uint32_t kRetrigger = 0x40000000u;
			int32_t bad = 0;
			auto check = [&](uint32_t resetter_increment) {
				for (uint32_t overshoot : {resetter_increment / 4, resetter_increment / 2, resetter_increment - 1}) {
					SyncedPhasor synced{Phasor{uq32(0), uq32(kAudible)},
					                    Phasor{uq32(overshoot - resetter_increment), uq32(resetter_increment)},
					                    uq32(kRetrigger)};
					const double exact = kRetrigger + (static_cast<double>(overshoot) * kAudible / resetter_increment);
					const double error = static_cast<double>(synced.advance().raw()) - exact;
					bad += (error < -1.0 * kAudible || error > 1.0 * kAudible) ? 1 : 0;
				}
			};
			for (uint32_t increment = 1; increment <= 65536; ++increment) {
				check(increment);
			}
			for (uint32_t increment : {65537u, 1u << 20, 1u << 26, 0x7FFFFFFFu}) {
				check(increment);
			}
			expect(bad).to_equal(0);
		});
	});

	context("against the raw reference", _ {
		it("computes the resetter reciprocal as the reference does", _ {
			int32_t bad = 0;
			for (uint32_t increment : kEdgeIncrements) {
				if (reference_holds(increment)) {
					bad += deluge::dsp::fixed::resetter_reciprocal(increment)
					       != sync_reference::resetter_reciprocal(increment);
				}
			}
			XorShift next;
			for (int i = 0; i < (1 << 16); ++i) {
				const uint32_t increment = next();
				if (!reference_holds(increment)) {
					continue;
				}
				bad += deluge::dsp::fixed::resetter_reciprocal(increment) != sync_reference::resetter_reciprocal(increment);
			}
			expect(bad).to_equal(0);
		});
		it("resets to the reference's phase at every crossover", _ {
			int32_t bad = 0;
			XorShift next;
			auto run = [&](uint32_t increment, uint32_t resetter_increment, uint32_t phase, uint32_t resetter,
			               uint32_t retrigger) {
				if (!reference_holds(resetter_increment)) {
					return;
				}
				SyncedPhasor synced{Phasor{uq32(phase), uq32(increment)}, Phasor{uq32(resetter), uq32(resetter_increment)},
				                    uq32(retrigger)};
				sync_reference::SyncedPhasor reference{phase, increment, resetter, resetter_increment, retrigger};
				for (int i = 0; i < 64; ++i) {
					bad += synced.advance().raw() != reference.advance();
				}
			};
			for (uint32_t resetter_increment : kEdgeIncrements) {
				for (int i = 0; i < 64; ++i) {
					run(next(), resetter_increment, next(), next(), next());
				}
			}
			for (int i = 0; i < 4096; ++i) {
				// Masters from a few samples per cycle to slower than the block, audible pitches across the range
				run(next() >> (next() & 15), next() >> (next() & 15), next(), next(), next());
			}
			expect(bad).to_equal(0);
		});
	});
});
// clang-format on

CPPSPEC_MAIN(synced_phasor);
