/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

/// audio_pacing.h — the RZ/A1L render-window pacing policy, as pure arithmetic.
///
/// Relocated verbatim from AudioEngine::routine_() (audio_engine.cpp). Given
/// how many samples the DMA play head has consumed since the last render
/// ("render as far as it has advanced"), choose the next render-window length:
/// double the excess over a small threshold under light load, cap at the ring
/// size, and round to a multiple of 4 so the NEON render paths run full lanes.
///
/// Pure and header-only so the host unit tests can exercise it bit-exactly
/// (tests/unit/audio_pacing_tests.cpp); the BSP render driver (audio_io.cpp)
/// is its only firmware consumer.
#ifndef BSP_RZA1_AUDIO_PACING_H
#define BSP_RZA1_AUDIO_PACING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// `ringFrames` is the output-ring length (SSI_TX_BUFFER_NUM_SAMPLES, 128) and
/// the cap. `rawFrames` must already be reduced modulo the ring.
static inline uint32_t deluge_rza1_pace_window(uint32_t rawFrames, uint32_t ringFrames) {
	// Double the number of samples we're going to do - within some constraints
	const int32_t sampleThreshold = 6; // If too low, it'll lead to bigger audio windows and stuff
	uint32_t numSamples = rawFrames;

	if (numSamples < ringFrames) {
		int32_t samplesOverThreshold = (int32_t)numSamples - sampleThreshold;
		if (samplesOverThreshold > 0) {
			samplesOverThreshold = samplesOverThreshold << 1;
			numSamples = sampleThreshold + samplesOverThreshold;
		}
	}
	if (numSamples > ringFrames) {
		numSamples = ringFrames;
	}

	// Want to round to be doing a multiple of 4 samples, so the NEON functions can be utilized most efficiently.
	// Note - this can take numSamples up as high as the ring size (currently 128).
	if (numSamples >= 3) {
		numSamples = (numSamples + 2) & ~3u;
	}
	return numSamples;
}

#ifdef __cplusplus
}
#endif

#endif // BSP_RZA1_AUDIO_PACING_H
