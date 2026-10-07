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

// OnePole: the one-pole low-pass section the ladders, the compressor and the granular processor build on. The values
// were recorded from the app's BasicFilterComponent before the move: 16 samples of a ramp at moveability 0x20000000.

#include "dsp-next/fixed/filter/one_pole.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>

using deluge::dsp::fixed::OnePole;

namespace {
constexpr int32_t kMoveability = 0x20000000;

/// A ramp from -2^30 rising by 2^27 per sample.
constexpr int32_t ramp(int32_t i) {
	return -(1 << 30) + (i * (1 << 27));
}
} // namespace

// clang-format off
describe one_pole_spec("OnePole", ${
	it("low-passes a ramp exactly", _ {
		constexpr std::array<int32_t, 16> expected{
		    -268435456, -637534208, -754974720, -746586112, -675282944, -572522496, -454033408, -327680000,
		    -197394432, -65142784, 68091904, 201818112, 335790080, 469884928, 604041216, 738228224};
		OnePole pole;
		for (int32_t i = 0; i < 16; i++) {
			expect(pole.lowPass(ramp(i), kMoveability)).to_equal(expected[i]);
		}
		expect(pole.memory()).to_equal(805326848);
	});
	it("all-passes a ramp exactly", _ {
		constexpr std::array<int32_t, 16> expected{
		    536870912, -335544320, -704643072, -822083584, -813694976, -742391808, -639631360, -521142272,
		    -394788864, -264503296, -132251648, 983040, 134709248, 268681216, 402776064, 536932352};
		OnePole pole;
		for (int32_t i = 0; i < 16; i++) {
			expect(pole.allPass(ramp(i), kMoveability)).to_equal(expected[i]);
		}
		expect(pole.memory()).to_equal(805326848);
	});
	it("feeds back its memory after being affected", _ {
		constexpr std::array<int32_t, 16> expected{
		    -268435456, -369098752, -385875968, -360710144, -314572800, -257949696, -196083712, -131596288,
		    -65798144, 655360, 67436544, 134381568, 201408512, 268476416, 335564800, 402663424};
		OnePole pole;
		for (int32_t i = 0; i < 16; i++) {
			pole.affect(ramp(i), kMoveability);
			expect(pole.feedbackOutput(kMoveability)).to_equal(expected[i]);
		}
		expect(pole.feedbackOutputUnshifted(kMoveability)).to_equal(100665856);
		expect(pole.memory()).to_equal(805326848);
	});
	it("starts silent and resets to silence", _ {
		OnePole pole;
		expect(pole.memory()).to_equal(0);
		(void)pole.lowPass(ramp(3), kMoveability);
		pole.reset();
		expect(pole.memory()).to_equal(0);
	});
});

CPPSPEC_MAIN(one_pole_spec);
