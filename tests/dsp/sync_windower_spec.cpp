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

#include "dsp-next/core/buffer_view.h"
#include "dsp-next/fixed/sync/sync_windower.h"
#include "sync_reference.h"
#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <optional>
#include <type_traits>
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
	SyncWindower windower{deluge::dsp::typed_view(Buffer{buffer}), Resetter{0u, resetter_increment}, 0u, kInc, 0u};
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
	// The firmware's windower takes the block typed; the reference's (verbatim) takes it raw.
	auto block = [&] {
		if constexpr (std::is_constructible_v<Windower, deluge::dsp::Buffer<deluge::dsp::Q<31>>, Resetter, uint32_t,
		                                      uint32_t, uint32_t>) {
			return deluge::dsp::typed_view(Buffer{buffer});
		}
		else {
			return Buffer{buffer};
		}
	}();
	Windower windower{block, resetter, phase, increment, retrigger};
	while (auto window = windower.next()) {
		for (auto& sample : window->output) {
			if constexpr (std::is_same_v<std::remove_cvref_t<decltype(sample)>, int32_t>) {
				sample = static_cast<int32_t>(window->phasor.advance().raw());
			}
			else {
				sample = deluge::dsp::reinterpret_bits<deluge::dsp::Q<31>>(window->phasor.advance());
			}
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
		SyncWindower windower{deluge::dsp::typed_view(Buffer{buffer}), Resetter{0u, kSlowResetter}, 0u, kInc, 0u};
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
		SyncWindower windower{deluge::dsp::typed_view(Buffer{buffer}), Resetter{0u, 0u}, 0u, kInc, 0u};
		while (windower.next()) {
		}
		expect(windower.phase()).to_equal(static_cast<uint32_t>(kInc * kBlock));
	});

	// Voice::adjustPitch only rejects increments at or above 2^32, so increments above 0xFFFF0000 are reachable.
	it("tiles the block when the resetter increment is within 2^16 of a whole cycle", _ {
		std::vector<SyncWindower::Window> windows = collect(0xFFFFFF00u);
		expect(windows.size() > 1).to_be_truthy();
		expect(windows.back().offset + static_cast<int32_t>(windows.back().output.size())).to_equal(kBlock);
	});
	it("restarts at or before the exact phase, never behind the retrigger phase, at masters of 2^16 and slower", _ {
		// The master is an eighth or a quarter of its increment past the boundary at the crossover sample: the restarted
		// window's phase is the retrigger phase, backed up one increment for the redone crossover sample, plus the phase
		// gained since, exactly that fraction of the audible increment. The reciprocal's 16-bit divisor makes it smaller
		// for these masters, never negative
		constexpr uint32_t kRetrigger = 0x40000000u;
		int32_t bad = 0;
		int32_t restarts = 0;
		for (uint32_t increment : {8u, 255u, 4096u, 65535u, 65536u, 1u << 20}) {
			for (uint32_t ahead : {increment / 8, increment / 4}) {
				std::array<int32_t, kBlock> buffer{};
				SyncWindower windower{deluge::dsp::typed_view(Buffer{buffer}), Resetter{ahead - (increment * 4), increment}, 0u, kInc, kRetrigger};
				const auto exact = static_cast<int64_t>(uint64_t{kInc} * ahead / increment);
				bool first = true;
				while (std::optional<SyncWindower::Window> window = windower.next()) {
					if (!first) {
						const auto gained = static_cast<int32_t>(window->phasor.phase().raw() - kRetrigger + kInc);
						bad += (gained < 0 || gained > exact) ? 1 : 0;
						++restarts;
					}
					first = false;
				}
			}
		}
		expect(restarts).to_be_greater_than(0);
		expect(bad).to_equal(0);
	});
	it("tiles the block at the slowest masters, whose next crossover is 2^31 samples or more away", _ {
		// Resetter increments of 1 and 2, far from their boundary and just before it. A bound on the windows makes a
		// windower that never finishes fail rather than hang
		int32_t bad = 0;
		uint32_t x = 0x6A09E667u;
		for (uint32_t increment : {1u, 2u}) {
			for (int i = 0; i < 64; ++i) {
				const uint32_t phase = i % 2 == 0 ? xorshift(x) : 0u - (xorshift(x) % 40u);
				std::array<int32_t, kBlock> buffer{};
				SyncWindower windower{deluge::dsp::typed_view(Buffer{buffer}), Resetter{phase, increment}, 0u, kInc, 0u};
				int32_t windows = 0;
				int32_t end = 0;
				while (std::optional<SyncWindower::Window> window = windower.next()) {
					const auto length = static_cast<int32_t>(window->output.size());
					bad += (length < 1 || window->offset < 0 || window->offset + length > kBlock) ? 1 : 0;
					end = window->offset + length;
					if (++windows > kBlock + 1) {
						++bad;
						break;
					}
				}
				bad += end != kBlock ? 1 : 0;
			}
		}
		expect(bad).to_equal(0);
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
			       != render<sync_reference::SyncWindower<>>(resetter, phase, increment, retrigger);
		}
		expect(bad).to_equal(0);
	});
	it("renders as the reference with the fixed reciprocal does, at every resetter increment", _ {
		int32_t bad = 0;
		uint32_t x = 0x2545F491u;
		auto check = [&](Resetter resetter) {
			const uint32_t phase = xorshift(x);
			const uint32_t increment = xorshift(x) >> (xorshift(x) & 7);
			const uint32_t retrigger = xorshift(x);
			bad += render<SyncWindower>(resetter, phase, increment, retrigger)
			       != render<sync_reference::SyncWindower<&deluge::dsp::fixed::resetter_reciprocal>>(resetter, phase,
			                                                                                         increment, retrigger);
		};
		// From 3: at 1 or 2 the next crossover is 2^31 samples or more away, which the raw reference cannot count
		for (uint32_t increment : {3u, 255u, 65535u, 65536u, 0xFFFF0001u, 0xFFFFFF00u, 0xFFFFFFFFu}) {
			for (int i = 0; i < 256; ++i) {
				// Phases near the boundary, so that the slow masters cross within the block
				check(Resetter{0u - (xorshift(x) % (increment * 32u + 1u)), increment});
			}
		}
		for (int i = 0; i < 4096; ++i) {
			const Resetter resetter{xorshift(x), xorshift(x) >> (xorshift(x) % 30)};
			if (resetter.phase_increment >= 3) {
				check(resetter);
			}
		}
		expect(bad).to_equal(0);
	});
});
// clang-format on

CPPSPEC_MAIN(sync_windower);
