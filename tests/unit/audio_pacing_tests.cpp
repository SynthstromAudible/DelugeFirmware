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

#include "CppUTest/TestHarness.h"

#include "bsp/rza1/audio_pacing.h"

#include <cstddef>
#include <cstdint>

// The render-window pacing policy was relocated verbatim from
// AudioEngine::routine_() (audio_engine.cpp) into the RZ/A1L BSP
// (src/bsp/rza1/audio_pacing.h) as part of the deluge_app_render boundary
// work. This test pins the relocated arithmetic to the original, which is
// reproduced below exactly as it appeared in routine_() — the regression
// guard that the relocation is bit-identical for every possible input.

namespace {

constexpr size_t kRingFrames = 128; // SSI_TX_BUFFER_NUM_SAMPLES at relocation time

// The original routine_() window adjustment, verbatim (numSamples arrives
// already reduced modulo the ring size).
size_t originalRoutineWindowAdjustment(size_t numSamples) {
	// Double the number of samples we're going to do - within some constraints
	int32_t sampleThreshold = 6; // If too low, it'll lead to bigger audio windows and stuff
	constexpr size_t maxAdjustedNumSamples = kRingFrames;

	if (numSamples < maxAdjustedNumSamples) {
		int32_t samplesOverThreshold = numSamples - sampleThreshold;
		if (samplesOverThreshold > 0) {
			samplesOverThreshold = samplesOverThreshold << 1;
			numSamples = sampleThreshold + samplesOverThreshold;
		}
	}
	numSamples = numSamples < maxAdjustedNumSamples ? numSamples : maxAdjustedNumSamples;

	// Want to round to be doing a multiple of 4 samples, so the NEON functions can be utilized most efficiently.
	if (numSamples >= 3) {
		numSamples = (numSamples + 2) & ~3;
	}
	return numSamples;
}

} // namespace

TEST_GROUP(AudioPacingTests){};

TEST(AudioPacingTests, matchesOriginalForEveryPossibleInput) {
	// rawFrames is always (play head - write cursor) mod ring, so 0..127.
	for (uint32_t raw = 0; raw < kRingFrames; raw++) {
		uint32_t relocated = deluge_rza1_pace_window(raw, kRingFrames);
		size_t original = originalRoutineWindowAdjustment(raw);
		UNSIGNED_LONGS_EQUAL(original, relocated);
	}
}

TEST(AudioPacingTests, neverExceedsRingAndKeepsNeonAlignment) {
	for (uint32_t raw = 0; raw < kRingFrames; raw++) {
		uint32_t frames = deluge_rza1_pace_window(raw, kRingFrames);
		CHECK(frames <= kRingFrames);
		if (frames >= 3) {
			UNSIGNED_LONGS_EQUAL(0, frames % 4);
		}
		// The window covers what the DMA has consumed, except that rounding to
		// the nearest multiple of 4 may leave at most one sample for the next
		// poll (e.g. 5 -> 4).
		CHECK(frames + 1 >= raw || frames == kRingFrames);
	}
}

TEST(AudioPacingTests, knownValues) {
	// A few values worth eyeballing: light load doubles, heavy load caps.
	UNSIGNED_LONGS_EQUAL(0, deluge_rza1_pace_window(0, kRingFrames));
	UNSIGNED_LONGS_EQUAL(4, deluge_rza1_pace_window(4, kRingFrames));    // under threshold: no doubling
	UNSIGNED_LONGS_EQUAL(8, deluge_rza1_pace_window(7, kRingFrames));    // 6 + (7-6)*2 = 8
	UNSIGNED_LONGS_EQUAL(44, deluge_rza1_pace_window(25, kRingFrames));  // 6 + 19*2 = 44
	UNSIGNED_LONGS_EQUAL(128, deluge_rza1_pace_window(70, kRingFrames)); // doubles past the cap
	UNSIGNED_LONGS_EQUAL(128, deluge_rza1_pace_window(127, kRingFrames));
}
