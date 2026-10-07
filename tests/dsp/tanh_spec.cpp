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

// tanh: the plain (non-antialiased) tanh saturation — the input driven by 2^drive, looked up in kTanhSmallTable and
// shifted down by drive + 2. The values were recorded from the app's getTanHUnknown / getTanH<N> before the move.

#include "dsp-next/fixed/shaping/tanh.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <limits>

using deluge::dsp::fixed::tanh;

namespace {
constexpr std::array<int32_t, 7> kInputs{
    0, 1 << 28, -(1 << 28), 1 << 30, 12345678, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(),
};
} // namespace

// clang-format off
describe tanh_spec("tanh", ${
	it("passes a signal undriven, at a quarter of its level", _ {
		constexpr std::array<int32_t, 7> expected{0, 248250368, -248250368, 517898240, 12345600, -536854528, 536854527};
		for (std::size_t i = 0; i < kInputs.size(); i++) {
			expect(tanh(kInputs[i], 0)).to_equal(expected[i]);
		}
	});
	it("saturates a signal driven by 2^2", _ {
		constexpr std::array<int32_t, 7> expected{0, 129474560, -129474560, 134213631, 12318382, -134213632, 134213631};
		for (std::size_t i = 0; i < kInputs.size(); i++) {
			expect(tanh(kInputs[i], 2)).to_equal(expected[i]);
		}
	});
	it("saturates a signal driven by 2^5", _ {
		constexpr std::array<int32_t, 7> expected{0, 16776703, -16776704, 16776703, 10518343, -16776704, 16776703};
		for (std::size_t i = 0; i < kInputs.size(); i++) {
			expect(tanh(kInputs[i], 5)).to_equal(expected[i]);
		}
	});
	it("saturates a signal driven by 2^8", _ {
		constexpr std::array<int32_t, 7> expected{0, 2097087, -2097088, 2097087, 2097087, -2097088, 2097087};
		for (std::size_t i = 0; i < kInputs.size(); i++) {
			expect(tanh(kInputs[i], 8)).to_equal(expected[i]);
		}
	});
	it("gives a compile-time drive the runtime drive's output", _ {
		for (int32_t input : kInputs) {
			expect(tanh<0>(input)).to_equal(tanh(input, 0));
			expect(tanh<2>(input)).to_equal(tanh(input, 2));
			expect(tanh<5>(input)).to_equal(tanh(input, 5));
		}
	});
	it("saturates a compile-time drive past 2^19 at its own width", _ {
		constexpr std::array<int32_t, 7> expected{0, 63, -64, 63, 63, -64, 63};
		for (std::size_t i = 0; i < kInputs.size(); i++) {
			expect(tanh<23>(kInputs[i])).to_equal(expected[i]);
		}
		expect(tanh<23>(100)).to_equal(58);
		expect(tanh<23>(-100)).to_equal(-59);
		expect(tanh<23>(200)).to_equal(63);
		expect(tanh<23>(-3000)).to_equal(-64);
	});
	it("saturates a runtime drive past 2^19 at 12 bits", _ {
		constexpr std::array<int32_t, 7> expected{0, -1, 0, -1, -1, 0, -1};
		for (std::size_t i = 0; i < kInputs.size(); i++) {
			expect(tanh(kInputs[i], 23)).to_equal(expected[i]);
		}
	});
});

CPPSPEC_MAIN(tanh_spec);
