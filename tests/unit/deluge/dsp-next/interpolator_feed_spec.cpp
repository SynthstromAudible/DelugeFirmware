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

// Pins the Interpolator's feeds against the tap loops the sample reader and the live pitch shifter's play head wrote
// before the taps went private: each reference below is that loop over two plain windows. Every operation runs over
// random windows and samples, for every count it is used with, mono and stereo.

#include "dsp-next/fixed/interpolation/interpolator.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace {
using deluge::dsp::fixed::Interpolator;
using Window = Interpolator::Window;
constexpr int32_t kTaps = kInterpolationMaxNumSamples;

struct Windows {
	Window l{};
	Window r{};
	bool operator==(const Windows&) const = default;
};

std::mt19937& rng() {
	static std::mt19937 generator(0xFEEDu); // fixed seed: x86 and ARM draw the same values
	return generator;
}

int16_t random_sample() {
	return static_cast<int16_t>(std::uniform_int_distribution<int32_t>(INT16_MIN, INT16_MAX)(rng()));
}

Windows random_windows() {
	Windows w;
	for (int32_t i = 0; i < kTaps; i++) {
		w.l[i] = random_sample();
		w.r[i] = random_sample();
	}
	return w;
}

std::vector<int16_t> random_samples(std::size_t n) {
	std::vector<int16_t> v(n);
	for (auto& s : v) {
		s = random_sample();
	}
	return v;
}

// An Interpolator holding @p w, loaded through a head feed over every slot.
Interpolator holding(const Windows& w) {
	Interpolator interpolator;
	auto feed = interpolator.fill_head(kTaps);
	for (int32_t i = kTaps - 1; i >= 0; i--) {
		feed.put(w.l[i], w.r[i]);
	}
	return interpolator;
}

Windows held(const Interpolator& interpolator) {
	return {interpolator.window_l(), interpolator.window_r()};
}

// The window shift every reference starts from: the element loop Interpolator::shift_window replaced.
void reference_shift(Window& window, int32_t n) {
	for (int32_t i = kTaps - 1; i >= n; i--) {
		window[i] = window[i - n];
	}
}

// The play head's and the sinc reader's jump: shift both windows, then write slots n-1 … 0 oldest first.
void reference_advance(Windows& w, int32_t n, const std::vector<int16_t>& l, const std::vector<int16_t>& r,
                       bool stereo) {
	reference_shift(w.l, n);
	reference_shift(w.r, n);
	for (int32_t k = 0; k < n; k++) {
		w.l[n - 1 - k] = l[k];
		if (stereo) {
			w.r[n - 1 - k] = r[k];
		}
	}
}

// fillInterpolationBufferForward: slots n-1 … 0, no shift.
void reference_fill_head(Windows& w, int32_t n, const std::vector<int16_t>& l, const std::vector<int16_t>& r,
                         bool stereo) {
	for (int32_t i = n - 1, k = 0; i >= 0; i--, k++) {
		w.l[i] = l[k];
		if (stereo) {
			w.r[i] = r[k];
		}
	}
}

// fillInterpolationBufferWithSilence: slots from … 0, both channels zeroed.
void reference_head_silence(Windows& w, int32_t from) {
	for (int32_t i = from; i >= 0; i--) {
		w.l[i] = 0;
		w.r[i] = 0;
	}
}

// jumpForwardLinear's taps: two fresh taps for a step of two or more, else tap 0 to tap 1 and one fresh tap.
void reference_advance_linear(Windows& w, int32_t n, int16_t l1, int16_t r1, int16_t l0, int16_t r0, bool stereo) {
	if (stereo) {
		if (n >= 2) {
			w.l[1] = l1;
			w.r[1] = r1;
		}
		else {
			w.l[1] = w.l[0];
			w.r[1] = w.r[0];
		}
		w.r[0] = r0;
	}
	else {
		if (n >= 2) {
			w.l[1] = l1;
		}
		else {
			w.l[1] = w.l[0];
		}
	}
	w.l[0] = l0;
}

// fillInterpolationBufferRetrospectively: from slot start toward the oldest, valid samples or zeros on both channels.
void reference_fill_tail(Windows& w, int32_t start, int32_t end, const std::vector<bool>& valid,
                         const std::vector<int16_t>& l, const std::vector<int16_t>& r, bool stereo) {
	for (int32_t i = start, k = 0; i < end; i++, k++) {
		if (valid[k]) {
			w.l[i] = l[k];
			if (stereo) {
				w.r[i] = r[k];
			}
		}
		else {
			w.l[i] = 0;
			w.r[i] = 0;
		}
	}
}

// The reader's resize loops (widening as a move) and its far-end zeroing.
void reference_narrow(Windows& w, int32_t offset, int32_t count, bool stereo) {
	for (int32_t i = 0; i < count; i++) {
		w.l[i] = w.l[i + offset];
		if (stereo) {
			w.r[i] = w.r[i + offset];
		}
	}
}
void reference_widen(Windows& w, int32_t offset, int32_t count, bool stereo) {
	for (int32_t i = count - 1; i >= 0; i--) {
		w.l[i + offset] = w.l[i];
		if (stereo) {
			w.r[i + offset] = w.r[i];
		}
	}
}
void reference_clear(Windows& w, int32_t from, int32_t to, bool stereo) {
	for (int32_t i = from; i < to; i++) {
		w.l[i] = 0;
		if (stereo) {
			w.r[i] = 0;
		}
	}
}

constexpr int32_t kRounds = 200;

} // namespace

// clang-format off
describe interpolator_feed("deluge::dsp::fixed Interpolator feeds", ${
	it("advance writes the vacated head slots oldest first, as the jump loops did", _ {
		for (int32_t round = 0; round < kRounds; round++) {
			for (int32_t n = 0; n <= kTaps; n++) {
				for (const bool stereo : {false, true}) {
					Windows expected = random_windows();
					Interpolator interpolator = holding(expected);
					const auto l = random_samples(n);
					const auto r = random_samples(n);
					reference_advance(expected, n, l, r, stereo);
					auto feed = interpolator.advance(n);
					for (int32_t k = 0; k < n; k++) {
						expect(feed.remaining()).to_equal(static_cast<std::size_t>(n - k));
						if (stereo) {
							feed.put(l[k], r[k]);
						}
						else {
							feed.put(l[k]);
						}
					}
					expect(feed.remaining()).to_equal(std::size_t{0});
					// A mono feed leaves the right window's vacated slots as the shift left them, unspecified: compare the
					// right window only where it is defined.
					if (stereo) {
						expect(held(interpolator) == expected).to_be_true();
					}
					else {
						expect(interpolator.window_l() == expected.l).to_be_true();
						for (int32_t i = n; i < kTaps; i++) {
							expect(interpolator.window_r()[i]).to_equal(expected.r[i]);
						}
					}
				}
			}
		}
	});

	it("fill_head writes slots n-1 ... 0 without a shift, as the forward fill did", _ {
		for (int32_t round = 0; round < kRounds; round++) {
			for (int32_t n = 0; n <= kTaps; n++) {
				for (const bool stereo : {false, true}) {
					Windows expected = random_windows();
					Interpolator interpolator = holding(expected);
					const auto l = random_samples(n);
					const auto r = random_samples(n);
					reference_fill_head(expected, n, l, r, stereo);
					auto feed = interpolator.fill_head(n);
					for (int32_t k = 0; k < n; k++) {
						if (stereo) {
							feed.put(l[k], r[k]);
						}
						else {
							feed.put(l[k]);
						}
					}
					expect(held(interpolator) == expected).to_be_true();
				}
			}
		}
	});

	it("a head feed's silence zeroes both channels, as the silence fill did", _ {
		for (int32_t from = 0; from < kTaps; from++) {
			Windows expected = random_windows();
			Interpolator interpolator = holding(expected);
			reference_head_silence(expected, from);
			auto feed = interpolator.fill_head(from + 1);
			while (feed.remaining() != 0) {
				feed.silence();
			}
			expect(held(interpolator) == expected).to_be_true();
		}
	});

	it("advance_linear moves only the two newest taps, as the linear jump did", _ {
		for (int32_t round = 0; round < kRounds; round++) {
			for (int32_t n = 1; n <= 3; n++) {
				for (const bool stereo : {false, true}) {
					Windows expected = random_windows();
					Interpolator interpolator = holding(expected);
					const int16_t l1 = random_sample();
					const int16_t r1 = random_sample();
					const int16_t l0 = random_sample();
					const int16_t r0 = random_sample();
					reference_advance_linear(expected, n, l1, r1, l0, r0, stereo);
					auto feed = interpolator.advance_linear(n, stereo ? 2 : 1);
					if (stereo) {
						if (n >= 2) {
							feed.put(l1, r1);
						}
						feed.put(l0, r0);
					}
					else {
						if (n >= 2) {
							feed.put(l1);
						}
						feed.put(l0);
					}
					expect(feed.remaining()).to_equal(std::size_t{0});
					expect(held(interpolator) == expected).to_be_true();
				}
			}
		}
	});

	it("fill_tail writes toward the oldest, valid samples or silence, as the retrospective fill did", _ {
		std::bernoulli_distribution valid_draw(0.7);
		for (int32_t round = 0; round < kRounds; round++) {
			for (int32_t start = 0; start <= kTaps; start++) {
				for (const bool stereo : {false, true}) {
					Windows expected = random_windows();
					Interpolator interpolator = holding(expected);
					const auto count = static_cast<std::size_t>(kTaps - start);
					const auto l = random_samples(count);
					const auto r = random_samples(count);
					std::vector<bool> valid(count);
					for (std::size_t k = 0; k < count; k++) {
						valid[k] = valid_draw(rng());
					}
					reference_fill_tail(expected, start, kTaps, valid, l, r, stereo);
					auto feed = interpolator.fill_tail(start);
					for (std::size_t k = 0; k < count; k++) {
						expect(feed.index()).to_equal(static_cast<std::size_t>(start) + k);
						if (!valid[k]) {
							feed.silence();
						}
						else if (stereo) {
							feed.put(l[k], r[k]);
						}
						else {
							feed.put(l[k]);
						}
					}
					expect(held(interpolator) == expected).to_be_true();
				}
			}
		}
	});

	it("narrow, widen and clear move and zero the window as the resize loops do", _ {
		for (int32_t round = 0; round < kRounds; round++) {
			for (const bool stereo : {false, true}) {
				const std::size_t channels = stereo ? 2 : 1;
				// The reader's sizes: kTaps and 2, the offset half their difference
				for (const auto [larger, smaller] : {std::pair{kTaps, 2}, std::pair{kTaps, 8}, std::pair{8, 2}}) {
					const int32_t offset = (larger - smaller) >> 1;
					{
						Windows expected = random_windows();
						Interpolator interpolator = holding(expected);
						reference_narrow(expected, offset, smaller, stereo);
						interpolator.narrow(offset, smaller, channels);
						expect(held(interpolator) == expected).to_be_true();
					}
					{
						Windows expected = random_windows();
						Interpolator interpolator = holding(expected);
						reference_widen(expected, offset, smaller, stereo);
						interpolator.widen(offset, smaller, channels);
						expect(held(interpolator) == expected).to_be_true();
					}
					{
						Windows expected = random_windows();
						Interpolator interpolator = holding(expected);
						reference_clear(expected, larger - offset, larger, stereo);
						interpolator.clear(larger - offset, larger, channels);
						expect(held(interpolator) == expected).to_be_true();
					}
				}
			}
		}
	});

	it("widen moves the window even when the offset is under the count", _ {
		Windows w;
		for (int32_t i = 0; i < kTaps; i++) {
			w.l[i] = static_cast<int16_t>(i + 1);
		}
		Interpolator interpolator = holding(w);
		interpolator.widen(1, 3, 1);
		// Slots 0-2 move to 1-3; slot 0 keeps its value for the owner to refill
		expect(interpolator.window_l()[1]).to_equal(int16_t{1});
		expect(interpolator.window_l()[2]).to_equal(int16_t{2});
		expect(interpolator.window_l()[3]).to_equal(int16_t{3});
		expect(interpolator.window_l()[4]).to_equal(int16_t{5});
	});
});
// clang-format on

CPPSPEC_MAIN(interpolator_feed);
