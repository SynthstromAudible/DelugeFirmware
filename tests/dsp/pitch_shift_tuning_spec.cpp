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

// The live pitch shifter's tuning (dsp-next/fixed/pitch_shift/pitch_shift_tuning.h) against the block of the old hop
// end that read it, copied below with its tables (each coarse one given the slot after it that the old code read past
// at +24, with no weight): at every knot, between knots, on both sides of each table's bounds, and past ±2 octaves.

#include "dsp-next/fixed/curves/quick_log.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "dsp-next/fixed/pitch_shift/pitch_shift_tuning.h"

#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <vector>

namespace {
using deluge::dsp::fixed::kLinearTableInterpolator;
using deluge::dsp::fixed::quick_log;
using Tuning = deluge::dsp::fixed::pitch_shift::PitchShiftTuning;

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): the old tables, verbatim
const int16_t kMinSearchFine[] = {8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 14, 16, 17};
const int16_t kMinSearchCoarse[] = {10, 10, 10, 17, 20, 0};
const int16_t kMaxSearchFine[] = {8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 10, 11, 12, 14, 16, 18, 21};
const int16_t kMaxSearchCoarse[] = {15, 15, 15, 21, 20, 0};
const int16_t kPercThresholdFine[] = {18, 18, 18, 18, 24, 30, 25, 35, 130, 40, 40, 45, 50, 47, 45, 42, 40};
const int16_t kPercThresholdCoarse[] = {15, 18, 130, 40, 20, 0};
const int16_t kCrossfadeFine[] = {30, 31, 32, 34, 35, 30, 25, 10, 10, 15, 15, 22, 30, 16, 15, 13, 40};
const int16_t kCrossfadeCoarse[] = {30, 30, 10, 40, 20, 0};
const int16_t kMaxHopLengthFine[] = {20, 27, 35, 42, 50, 60, 70, 90, 140, 140, 140, 140, 140, 140, 140, 140, 140};
const int16_t kMaxHopLengthCoarse[] = {10, 20, 140, 140, 140, 0};
const int16_t kRandomFine[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 25, 76, 50, 57, 65, 72, 80};
const int16_t kRandomCoarse[] = {0, 0, 0, 80, 80, 0};
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

// The old hop end's tuning block, verbatim but for its outputs
Tuning::Coefficients reference(int32_t phase_increment) {
	Tuning::Coefficients c;
	int32_t pitch_log = quick_log(phase_increment);
	const auto& t = kLinearTableInterpolator;
	if (pitch_log >= (800 << 20) && pitch_log < (864 << 20)) {
		const int32_t position = pitch_log - (800 << 20);
		c.min_search = t.interpolate_signed(position, 26, kMinSearchFine, 4) >> 9;
		c.max_search = t.interpolate_signed(position, 26, kMaxSearchFine, 4) >> 9;
		c.percussive_threshold = t.interpolate_signed(position, 26, kPercThresholdFine, 4) >> 16;
		c.crossfade_length = t.interpolate_signed(position, 26, kCrossfadeFine, 4) >> 12;
		c.max_hop_length = (t.interpolate_signed(position, 26, kMaxHopLengthFine, 4) >> 16) * 100;
		c.random_element = t.interpolate_signed(position, 26, kRandomFine, 4);
	}
	else {
		if (pitch_log > (896 << 20)) {
			pitch_log = (896 << 20);
		}
		else if (pitch_log < (768 << 20)) {
			pitch_log = (768 << 20);
		}
		const int32_t position = pitch_log - (768 << 20);
		c.min_search = t.interpolate_signed(position, 27, kMinSearchCoarse, 2) >> 9;
		c.max_search = t.interpolate_signed(position, 27, kMaxSearchCoarse, 2) >> 9;
		c.percussive_threshold = t.interpolate_signed(position, 27, kPercThresholdCoarse, 2) >> 16;
		c.crossfade_length = t.interpolate_signed(position, 27, kCrossfadeCoarse, 2) >> 12;
		c.max_hop_length = (t.interpolate_signed(position, 27, kMaxHopLengthCoarse, 2) >> 16) * 100;
		c.random_element = t.interpolate_signed(position, 27, kRandomCoarse, 2);
	}
	return c;
}

bool same(const Tuning::Coefficients& a, const Tuning::Coefficients& b) {
	return a.min_search == b.min_search && a.max_search == b.max_search
	       && a.percussive_threshold == b.percussive_threshold && a.crossfade_length == b.crossfade_length
	       && a.max_hop_length == b.max_hop_length && a.random_element == b.random_element;
}

// The phase increment @p semitones from unshifted, 8.24, rounded
int32_t phase_at(double semitones) {
	return static_cast<int32_t>(std::lround(16777216.0 * std::exp2(semitones / 12.0)));
}
} // namespace

// clang-format off
describe pitch_shift_tuning("deluge::dsp::fixed::pitch_shift::PitchShiftTuning", ${
	it("reads every knot and the points between them as the old hop end did", _ {
		std::vector<int32_t> phases;
		for (double s = -36.0; s <= 36.0; s += 0.125) {
			phases.push_back(phase_at(s));
		}
		bool all = true;
		for (const int32_t p : phases) {
			all = all && same(Tuning::coefficients({.phase_increment = p}), reference(p));
		}
		expect(all).to_be_true();
	});

	it("reads either side of each table's bounds, ±1 and ±2 octaves exactly, as the old hop end did", _ {
		bool all = true;
		for (const int32_t octave : {4194304, 8388608, 16777216, 33554432, 67108864}) {
			for (int32_t d = -64; d <= 64; d++) {
				const int32_t p = octave + d;
				all = all && same(Tuning::coefficients({.phase_increment = p}), reference(p));
			}
		}
		expect(all).to_be_true();
	});

	it("reads the coarse tables' last knots at +24 and beyond", _ {
		const Tuning::Coefficients at24 = Tuning::coefficients({.phase_increment = 67108864});
		expect(at24.max_hop_length).to_equal(14000);
		expect(at24.random_element).to_equal(80 << 16);
		expect(same(Tuning::coefficients({.phase_increment = 0x7FFFFFFF}), at24)).to_be_true();
	});

	it("picks the fine tables inside ±1 octave and the coarse ones outside", _ {
		// One semitone up: two thirds of the way from the fine table's unshifted knot (0) to the next (15)
		const Tuning::Coefficients up1 = Tuning::coefficients({.phase_increment = 17774841});
		expect(up1.random_element >> 16).to_be_greater_than(5);
		expect(up1.random_element >> 16).to_be_less_than(15);
		// Three semitones below an octave down: between the coarse table's −24 knot (1000) and its −12 knot (2000)
		const Tuning::Coefficients down15 = Tuning::coefficients({.phase_increment = phase_at(-15)});
		expect(down15.max_hop_length).to_be_greater_than(1000);
		expect(down15.max_hop_length).to_be_less_than(2000);
	});
});
// clang-format on

CPPSPEC_MAIN(pitch_shift_tuning);
