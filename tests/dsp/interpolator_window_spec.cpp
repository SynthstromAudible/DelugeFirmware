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

// Pins the Interpolator's tap-window feed (pushL / pushR / jumpForward, the NEON `vext` shift) against the
// element-by-element loop it replaces, for every shift distance and over long random feeds the way the sample
// reader and live pitch shifter drive it (shift, then refill the vacated head slots).

#include "dsp-next/fixed/interpolation/interpolator.h"
#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <random>

namespace {
using deluge::dsp::fixed::Interpolator;
using Window = Interpolator::Window;

// The loop Interpolator::shiftWindow used before the NEON shift
void reference_shift(Window& window, int32_t n) {
	for (int32_t i = kInterpolationMaxNumSamples - 1; i >= n; i--) {
		window[i] = window[i - n];
	}
}

Window distinct_window(int16_t base) {
	Window window{};
	for (int32_t i = 0; i < kInterpolationMaxNumSamples; i++) {
		window[i] = static_cast<int16_t>(base + i);
	}
	return window;
}

bool retained_taps_match(const Window& actual, const Window& expected, int32_t n) {
	for (int32_t i = n; i < kInterpolationMaxNumSamples; i++) {
		if (actual[i] != expected[i]) {
			return false;
		}
	}
	return true;
}
} // namespace

// clang-format off
describe interpolator_window("deluge::dsp::fixed Interpolator tap-window shift", ${
	it("keeps every retained tap for each jumpForward distance 0 ... 17", _ {
		for (int32_t n = 0; n <= kInterpolationMaxNumSamples + 1; n++) {
			Interpolator interpolator;
			interpolator.buffer_l = distinct_window(100);
			interpolator.buffer_r = distinct_window(-100);
			Window expected_l = interpolator.buffer_l;
			Window expected_r = interpolator.buffer_r;
			reference_shift(expected_l, n);
			reference_shift(expected_r, n);

			interpolator.jumpForward(n);

			expect(retained_taps_match(interpolator.buffer_l, expected_l, n)).to_be_true();
			expect(retained_taps_match(interpolator.buffer_r, expected_r, n)).to_be_true();
		}
	});

	it("pushL / pushR shift their own window by one and insert at the head", _ {
		Interpolator interpolator;
		interpolator.buffer_l = distinct_window(100);
		interpolator.buffer_r = distinct_window(-100);
		Window expected_l = interpolator.buffer_l;
		Window expected_r = interpolator.buffer_r;

		interpolator.pushL(7);
		reference_shift(expected_l, 1);
		expected_l[0] = 7;
		expect(interpolator.buffer_l == expected_l).to_be_true();
		expect(interpolator.buffer_r == expected_r).to_be_true(); // untouched

		interpolator.pushR(-7);
		reference_shift(expected_r, 1);
		expected_r[0] = -7;
		expect(interpolator.buffer_r == expected_r).to_be_true();
	});

	it("matches the reference over a long random shift-and-refill feed", _ {
		std::mt19937 rng(0x5EEDu); // fixed seed: x86 and ARM draw the same feed
		std::uniform_int_distribution<int32_t> sample(INT16_MIN, INT16_MAX);
		std::uniform_int_distribution<int32_t> distance(1, kInterpolationMaxNumSamples);

		Interpolator interpolator;
		Window expected_l{};
		Window expected_r{};
		for (int32_t step = 0; step < 20000; step++) {
			int32_t n = distance(rng);
			interpolator.jumpForward(n);
			reference_shift(expected_l, n);
			reference_shift(expected_r, n);
			for (int32_t i = n - 1; i >= 0; i--) {
				auto left = static_cast<int16_t>(sample(rng));
				auto right = static_cast<int16_t>(sample(rng));
				interpolator.buffer_l[i] = expected_l[i] = left;
				interpolator.buffer_r[i] = expected_r[i] = right;
			}
			if (interpolator.buffer_l != expected_l || interpolator.buffer_r != expected_r) {
				expect(step).to_equal(-1); // report the first diverging step
				return;
			}
		}
	});
});
// clang-format on

CPPSPEC_MAIN(interpolator_window);
