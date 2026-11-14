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

// pan_amplitudes: the left and right amplitudes of a pan position.

#include "dsp-next/fixed/vca/pan.h"

#include <algorithm>
#include <cppspec.hpp>
#include <cstdint>

using deluge::dsp::fixed::pan_amplitudes;

namespace reference {
/// The two channels' amplitudes as raw Q30 words.
struct PanAmplitudes {
	int32_t left;
	int32_t right;
};
// Verbatim: the raw form.
constexpr PanAmplitudes pan_amplitudes(int32_t pan) {
	constexpr int32_t kUnity = 1073741823;
	constexpr int32_t kHalfRange = 1073741824;
	if (pan == 0) {
		return {.left = kUnity, .right = kUnity};
	}
	const int32_t offset = std::clamp(pan, -kHalfRange, kHalfRange);
	return {.left = pan <= 0 ? kUnity : kHalfRange - offset, .right = pan >= 0 ? kUnity : kHalfRange + offset};
}
} // namespace reference

// clang-format off
describe pan_spec("pan_amplitudes", ${
	it("leaves both channels at unity in the centre", _ {
		expect(pan_amplitudes(deluge::dsp::Q<31>::from_raw(0)).left.raw()).to_equal(1073741823);
		expect(pan_amplitudes(deluge::dsp::Q<31>::from_raw(0)).right.raw()).to_equal(1073741823);
	});
	it("lowers the far channel part way at a mid pan", _ {
		expect(pan_amplitudes(deluge::dsp::Q<31>::from_raw(0x10000000)).left.raw()).to_equal(805306368);
		expect(pan_amplitudes(deluge::dsp::Q<31>::from_raw(0x10000000)).right.raw()).to_equal(1073741823);
		expect(pan_amplitudes(deluge::dsp::Q<31>::from_raw(-0x10000000)).left.raw()).to_equal(1073741823);
		expect(pan_amplitudes(deluge::dsp::Q<31>::from_raw(-0x10000000)).right.raw()).to_equal(805306368);
	});
	it("silences the far channel at the ends and beyond", _ {
		expect(pan_amplitudes(deluge::dsp::Q<31>::from_raw(1 << 30)).left.raw()).to_equal(0);
		expect(pan_amplitudes(deluge::dsp::Q<31>::from_raw(-(1 << 30))).right.raw()).to_equal(0);
		expect(pan_amplitudes(deluge::dsp::Q<31>::from_raw(0x7FFFFFFF)).left.raw()).to_equal(0);
	});
	it("gives the reference's amplitudes at every pan", _ {
		int32_t bad = 0;
		auto check = [&](int32_t pan) {
			const auto live = pan_amplitudes(deluge::dsp::Q<31>::from_raw(pan));
			const auto ref = reference::pan_amplitudes(pan);
			bad += (live.left.raw() != ref.left || live.right.raw() != ref.right) ? 1 : 0;
		};
		for (int32_t pan : {INT32_MIN, -(1 << 30) - 1, -(1 << 30), -1, 0, 1, 1 << 30, (1 << 30) + 1, INT32_MAX}) {
			check(pan);
		}
		for (int64_t pan = INT32_MIN; pan <= INT32_MAX; pan += 65521) {
			check(static_cast<int32_t>(pan));
		}
		expect(bad).to_equal(0);
	});
});

CPPSPEC_MAIN(pan_spec);
