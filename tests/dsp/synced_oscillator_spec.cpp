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

// Behavioural spec for deluge::dsp::fixed::SyncedOscillator, run on x86 (SIMDe) and ARM/QEMU
// via CTest (every assertion is therefore an x86-vs-NEON cross-check). Uses a small synthetic
// table and a TableOscillator as the wrapped generator; bit-exact equivalence with the legacy
// renderOscSync on the real band-limited tables is the golden harness's job.

#include "dsp-next/fixed/synced_oscillator.h"
#include "dsp-next/fixed/table_oscillator.h"
#include <array>
#include <cppspec.hpp>
#include <cstdint>

namespace {
using deluge::dsp::fixed::Resetter;
using deluge::dsp::fixed::SyncedOscillator;
using deluge::dsp::fixed::Table;
using deluge::dsp::fixed::TableOscillator;

constexpr int32_t kMag = 2;             // index = phase >> 30, in [0, 4)
constexpr uint32_t kInc = 0x08000000;   // audible oscillator phase increment
constexpr std::size_t kBlock = 16;      // samples per render
constexpr std::array<int16_t, 8> kTable{0, 1000, 2000, 3000, 4000, 5000, 6000, 7000};

// The resetter reciprocal, computed exactly as renderOsc does.
constexpr int32_t resetterReciprocal(uint32_t increment) {
	return static_cast<int32_t>(2147483648u / static_cast<uint16_t>((increment + 65535) >> 16));
}

// A plain (un-synced) render of the wrapped oscillator, for comparison.
std::array<int32_t, kBlock> plainRender() {
	TableOscillator osc{Table{kTable, kMag}, 0u, kInc};
	std::array<int32_t, kBlock> out{};
	osc.renderBlock(deluge::dsp::fixed::Buffer{out});
	return out;
}
} // namespace

// clang-format off
describe synced_oscillator("deluge::dsp::fixed SyncedOscillator", ${
	context("a resetter too slow to cross over within the block", _ {
		// ~4096 samples per resetter cycle, so no crossover in 16 samples: pure passthrough.
		it("renders the wrapped oscillator unchanged", _ {
			Resetter slow{0u, 0x00100000u, resetterReciprocal(0x00100000u)};
			SyncedOscillator osc{TableOscillator{Table{kTable, kMag}, 0u, kInc}, 0u, kInc, slow, 0u};
			std::array<int32_t, kBlock> out{};
			osc.renderBlock(deluge::dsp::fixed::Buffer{out});
			expect(out).to_equal(plainRender());
		});

		it("advances the phase by the whole block", _ {
			Resetter slow{0u, 0x00100000u, resetterReciprocal(0x00100000u)};
			SyncedOscillator osc{TableOscillator{Table{kTable, kMag}, 0u, kInc}, 0u, kInc, slow, 0u};
			std::array<int32_t, kBlock> out{};
			osc.renderBlock(deluge::dsp::fixed::Buffer{out});
			expect(osc.phase()).to_equal(static_cast<uint32_t>(kInc * kBlock));
		});
	});

	context("a resetter that crosses over mid-block", _ {
		// 8 samples per resetter cycle: the first crossover lands at sample 8 within the block.
		it("matches the un-synced wave up to the first crossover, then restarts", _ {
			Resetter fast{0u, 0x20000000u, resetterReciprocal(0x20000000u)};
			SyncedOscillator osc{TableOscillator{Table{kTable, kMag}, 0u, kInc}, 0u, kInc, fast, 0u};
			std::array<int32_t, kBlock> out{};
			osc.renderBlock(deluge::dsp::fixed::Buffer{out});

			std::array<int32_t, kBlock> plain = plainRender();
			// Before the crossover sample (index 7) nothing has reset, so the wave is identical.
			expect(out[0]).to_equal(plain[0]);
			expect(out[3]).to_equal(plain[3]);
			expect(out[6]).to_equal(plain[6]);
			// After the restart the synced wave diverges from the free-running one.
			expect(out != plain).to_be_truthy();
		});
	});
});
// clang-format on

CPPSPEC_MAIN(synced_oscillator);
