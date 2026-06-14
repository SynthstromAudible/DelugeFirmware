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

// Spec for deluge::dsp::fixed::SyncWindower — the hard-sync window splitter that SyncedOscillator
// and the wavetable sync oscillator both iterate. It exercises the window tiling in isolation (the
// payoff of making it an object): bit-exact equivalence with the legacy renderOscSync on real waves
// is the golden harness's job; here we pin the structural invariants. Dual-arch (x86 SIMDe + ARM).

#include "dsp-next/fixed/sync_windower.h"
#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <vector>

namespace {
using deluge::dsp::fixed::Buffer;
using deluge::dsp::fixed::Resetter;
using deluge::dsp::fixed::SyncWindower;

constexpr uint32_t kInc = 0x08000000;           // audible oscillator increment (doesn't affect windowing)
constexpr int32_t kBlock = 64;                  // samples per render
constexpr uint32_t kFastResetter = 0x20000000u; // ~8 samples per cycle -> several crossovers in 64
constexpr uint32_t kSlowResetter = 0x00040000u; // ~16k samples per cycle -> no crossover in 64

// Drive the windower to exhaustion and collect the windows it hands out. The buffer stays zeroed;
// only the window geometry (offset/length/phase) matters here, which is independent of contents.
std::vector<SyncWindower::Window> collect(uint32_t resetter_increment) {
	std::array<int32_t, kBlock> buffer{};
	SyncWindower windower{Buffer{buffer}, Resetter{0u, resetter_increment}, 0u, kInc, 0u};
	std::vector<SyncWindower::Window> windows;
	while (std::optional<SyncWindower::Window> window = windower.next()) {
		windows.push_back(*window);
	}
	return windows;
}
} // namespace

// clang-format off
describe sync_windower("deluge::dsp::fixed SyncWindower", ${
	it("yields one full-length window when the resetter never crosses over", _ {
		std::vector<SyncWindower::Window> windows = collect(kSlowResetter);
		expect(windows.size()).to_equal(static_cast<std::size_t>(1));
		expect(windows.front().offset).to_equal(0);
		expect(windows.front().length).to_equal(kBlock);
	});

	it("tiles the block with single-sample seam overlaps when it crosses over", _ {
		std::vector<SyncWindower::Window> windows = collect(kFastResetter);
		expect(windows.size() > 1).to_be_truthy();
		expect(windows.front().offset).to_equal(0);
		for (std::size_t k = 0; k + 1 < windows.size(); ++k) {
			expect(windows[k].length >= 1).to_be_truthy();
			// every non-final window ends on a crossover; the next overlaps it by exactly one sample
			expect(windows[k + 1].offset).to_equal(windows[k].offset + windows[k].length - 1);
		}
		// the final window reaches the block end
		expect(windows.back().offset + windows.back().length).to_equal(kBlock);
	});

	it("advances the phase by the whole block when there is no crossover", _ {
		std::array<int32_t, kBlock> buffer{};
		SyncWindower windower{Buffer{buffer}, Resetter{0u, kSlowResetter}, 0u, kInc, 0u};
		while (windower.next()) {
		}
		expect(windower.phase()).to_equal(static_cast<uint32_t>(kInc * kBlock));
	});
});
// clang-format on

CPPSPEC_MAIN(sync_windower);
