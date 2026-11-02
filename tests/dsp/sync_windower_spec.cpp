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

#include "dsp-next/fixed/sync/sync_windower.h"
#include "sync_reference.h"
#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <utility>
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

/// Fills each window with a saw read from its phasor, as an oscillator would, and returns the block and end phase.
template <class Windower>
std::pair<std::array<int32_t, kBlock>, uint32_t> render(Resetter resetter, uint32_t phase, uint32_t increment,
                                                        uint32_t retrigger) {
	std::array<int32_t, kBlock> buffer{};
	Windower windower{Buffer{buffer}, resetter, phase, increment, retrigger};
	while (auto window = windower.next()) {
		for (int32_t& sample : window->output) {
			sample = static_cast<int32_t>(window->phasor.advance().raw());
		}
	}
	return {buffer, windower.phase()};
}

uint32_t xorshift(uint32_t& x) {
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}
} // namespace

// clang-format off
describe sync_windower("deluge::dsp::fixed SyncWindower", ${
	it("yields one full-length window when the resetter never crosses over", _ {
		std::vector<SyncWindower::Window> windows = collect(kSlowResetter);
		expect(windows.size()).to_equal(static_cast<std::size_t>(1));
		expect(windows.front().offset).to_equal(0);
		expect(static_cast<int32_t>(windows.front().output.size())).to_equal(kBlock);
	});

	it("tiles the block with single-sample seam overlaps when it crosses over", _ {
		std::vector<SyncWindower::Window> windows = collect(kFastResetter);
		expect(windows.size() > 1).to_be_truthy();
		expect(windows.front().offset).to_equal(0);
		for (std::size_t k = 0; k + 1 < windows.size(); ++k) {
			int32_t length = static_cast<int32_t>(windows[k].output.size());
			expect(length >= 1).to_be_truthy();
			// every non-final window ends on a crossover; the next overlaps it by exactly one sample
			expect(windows[k + 1].offset).to_equal(windows[k].offset + length - 1);
		}
		// the final window reaches the block end
		expect(windows.back().offset + static_cast<int32_t>(windows.back().output.size())).to_equal(kBlock);
	});

	it("advances the phase by the whole block when there is no crossover", _ {
		std::array<int32_t, kBlock> buffer{};
		SyncWindower windower{Buffer{buffer}, Resetter{0u, kSlowResetter}, 0u, kInc, 0u};
		while (windower.next()) {
		}
		expect(windower.phase()).to_equal(static_cast<uint32_t>(kInc * kBlock));
	});

	// Voice::adjustPitch reports success with a zero increment when its pitch multiply rounds down to nothing, and a
	// pulse-width self-sync takes the oscillator's own. Planning a window divides by the resetter's increment, which
	// traps on the Embassy BSP and raises SIGFPE here.
	it("yields one unsynced window when the resetter has no increment", _ {
		std::vector<SyncWindower::Window> windows = collect(0u);
		expect(windows.size()).to_equal(static_cast<std::size_t>(1));
		expect(static_cast<int32_t>(windows.front().output.size())).to_equal(kBlock);

		std::array<int32_t, kBlock> buffer{};
		SyncWindower windower{Buffer{buffer}, Resetter{0u, 0u}, 0u, kInc, 0u};
		while (windower.next()) {
		}
		expect(windower.phase()).to_equal(static_cast<uint32_t>(kInc * kBlock));
	});

	// The seam reciprocal rounds the increment up to 16 bits before dividing, which wraps to zero above 0xFFFF0000;
	// adjustPitch only rejects increments at or above 2^32, so these are reachable.
	it("tiles the block when the resetter increment wraps the reciprocal's round-up", _ {
		std::vector<SyncWindower::Window> windows = collect(0xFFFFFF00u);
		expect(windows.size() > 1).to_be_truthy();
		expect(windows.back().offset + static_cast<int32_t>(windows.back().output.size())).to_equal(kBlock);
	});
	it("renders the reference's samples and end phase", _ {
		int32_t bad = 0;
		uint32_t x = 0x9E3779B9u;
		for (int i = 0; i < 8192; ++i) {
			// Masters from a few samples per cycle to slower than the block, including increments at or below 2^16
			const Resetter resetter{xorshift(x), xorshift(x) >> (xorshift(x) % 24)};
			// At or below 2^16, and above 0xFFFF0000, the raw reference's resetter reciprocal is negative
			if (resetter.phase_increment <= 65536 || resetter.phase_increment > 0xFFFF0000u) {
				continue;
			}
			const uint32_t phase = xorshift(x);
			const uint32_t increment = xorshift(x) >> (xorshift(x) & 7);
			const uint32_t retrigger = xorshift(x);
			bad += render<SyncWindower>(resetter, phase, increment, retrigger)
			       != render<sync_reference::SyncWindower>(resetter, phase, increment, retrigger);
		}
		expect(bad).to_equal(0);
	});
});
// clang-format on

CPPSPEC_MAIN(sync_windower);
