/*
 * Copyright © 2017-2023 Synthstrom Audible Limited, 2026 Synthstrom Audible Limited
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
#include "processing/live/input_engine.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "processing/engines/audio_engine.h"
#include "processing/live/live_input_buffer.h"
#include "processing/live/live_pitch_shifter.h"
#include "util/fixedpoint.h"
#include <new>

extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

void InputEngine::render(OscType oscType, int32_t* oscBuffer, int32_t numSamples, uint32_t phaseIncrement,
                         int32_t sourceAmplitude, int32_t amplitudeIncrement, bool stereoBuffer, bool stereoUnison,
                         int32_t amplitudeL, int32_t amplitudeR, int32_t interpolationBufferSize) {
	OscType inputTypeNow = oscType;
	// If pitch shifting and we weren't previously...
	if (phaseIncrement != kMaxSampleValue) {

		if (!livePitchShifter_) {

			if (inputTypeNow == OscType::INPUT_STEREO && !AudioEngine::lineInPluggedIn && !AudioEngine::micPluggedIn) {
				inputTypeNow = OscType::INPUT_L;
			}

			LiveInputBuffer* liveInputBuffer = AudioEngine::getOrCreateLiveInputBuffer(inputTypeNow, true);

			if (liveInputBuffer) {

				void* memory = deluge::memory::alloc_fast(sizeof(LivePitchShifter));

				if (memory) {
					livePitchShifter_ = new (memory) LivePitchShifter(inputTypeNow, phaseIncrement);
					D_PRINTLN("start pitch shifting");
				}
			}
		}
	}

	// If not pitch shifting and we were previously...
	else {
		if (livePitchShifter_ && livePitchShifter_->mayBeRemovedWithoutClick()) {
			D_PRINTLN("stop pitch shifting");
			livePitchShifter_->~LivePitchShifter();
			delugeDealloc(livePitchShifter_);
			livePitchShifter_ = nullptr;
		}
	}

	// Yes pitch shifting
	if (livePitchShifter_) {
		// With unison stereo spread, oscBuffer is an interleaved stereo buffer. A mono input must be rendered
		// to a temporary mono buffer first and then panned across it, otherwise the mono render would write
		// into the interleaved buffer as if it were mono and produce noise.
		if (stereoUnison && livePitchShifter_->numChannels == 1) {
			int32_t* renderBuffer = spareRenderingBuffer[2];
			memset(renderBuffer, 0, numSamples * sizeof(int32_t));

			livePitchShifter_->render(renderBuffer, numSamples, phaseIncrement, sourceAmplitude, amplitudeIncrement,
			                          interpolationBufferSize);

			for (int32_t i = 0; i < numSamples; i++) {
				oscBuffer[(i << 1)] += multiply_32x32_rshift32(renderBuffer[i], amplitudeL) << 2;
				oscBuffer[(i << 1) + 1] += multiply_32x32_rshift32(renderBuffer[i], amplitudeR) << 2;
			}
		}
		else {
			livePitchShifter_->render(oscBuffer, numSamples, phaseIncrement, sourceAmplitude, amplitudeIncrement,
			                          interpolationBufferSize);
		}
	}

	// No pitch shifting
	else {

		int32_t* __restrict__ oscBufferPos = oscBuffer;
		int32_t const* __restrict__ inputReadPos = (int32_t const*)AudioEngine::inputRingPos;
		int32_t sourceAmplitudeThisUnison = sourceAmplitude;

		// Just left, or just right, or if (stereo but there's only the internal, mono mic)
		if (oscType != OscType::INPUT_STEREO || (!AudioEngine::lineInPluggedIn && !AudioEngine::micPluggedIn)) {

			int32_t channelOffset;
			// If right, but not internal mic
			if (oscType == OscType::INPUT_R && (AudioEngine::lineInPluggedIn || AudioEngine::micPluggedIn)) {
				channelOffset = 1;

				// Or if left or using internal mic
			}
			else {
				channelOffset = 0;
			}

			int32_t sourceAmplitudeNow = sourceAmplitudeThisUnison;

			// With unison stereo spread, oscBuffer is an interleaved stereo buffer and each unison part is
			// panned across it.
			if (stereoUnison) {
				int32_t const* const oscBufferEnd = oscBuffer + numSamples * 2;
				do {
					sourceAmplitudeNow += amplitudeIncrement;

					int32_t sample = multiply_32x32_rshift32(inputReadPos[channelOffset], sourceAmplitudeNow) << 4;
					*(oscBufferPos++) += multiply_32x32_rshift32(sample, amplitudeL) << 2;
					*(oscBufferPos++) += multiply_32x32_rshift32(sample, amplitudeR) << 2;

					inputReadPos += NUM_MONO_INPUT_CHANNELS;
					if (inputReadPos >= AudioEngine::inputRingEnd()) {
						inputReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
					}
				} while (oscBufferPos != oscBufferEnd);
			}
			else {
				int32_t const* const oscBufferEnd = oscBuffer + numSamples;
				do {
					sourceAmplitudeNow += amplitudeIncrement;

					// Mono / left channel (or stereo condensed to mono)
					*(oscBufferPos++) += multiply_32x32_rshift32(inputReadPos[channelOffset], sourceAmplitudeNow) << 4;

					inputReadPos += NUM_MONO_INPUT_CHANNELS;
					if (inputReadPos >= AudioEngine::inputRingEnd()) {
						inputReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
					}
				} while (oscBufferPos != oscBufferEnd);
			}
		}

		// Stereo
		else {

			int32_t numChannelsAfterCondensing = stereoBuffer ? 2 : 1;

			int32_t const* const oscBufferEnd = oscBuffer + numSamples * numChannelsAfterCondensing;

			int32_t sourceAmplitudeNow = sourceAmplitudeThisUnison;
			do {
				sourceAmplitudeNow += amplitudeIncrement;

				int32_t sampleL = inputReadPos[0];
				int32_t sampleR = inputReadPos[1];

				// If condensing to mono, do that now
				if (!stereoBuffer) {
					sampleL = ((sampleL >> 1) + (sampleR >> 1));
				}

				// Mono / left channel (or stereo condensed to mono)
				*(oscBufferPos++) += multiply_32x32_rshift32(sampleL, sourceAmplitudeNow) << 4;

				// Right channel
				if (stereoBuffer) {
					*(oscBufferPos++) += multiply_32x32_rshift32(sampleR, sourceAmplitudeNow) << 4;
				}

				inputReadPos += NUM_MONO_INPUT_CHANNELS;
				if (inputReadPos >= AudioEngine::inputRingEnd()) {
					inputReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
				}
			} while (oscBufferPos != oscBufferEnd);
		}
	}
}

void InputEngine::unassign() {
	// Matches the legacy VoiceUnisonPartSource::unassign exactly: a dealloc without the destructor (the
	// render-stop path above does call ~LivePitchShifter()). A pre-existing inconsistency — preserved here, not
	// "fixed", since the offline golden does not exercise the lifecycle and the behaviour change is unverified.
	if (livePitchShifter_ != nullptr) {
		delugeDealloc(livePitchShifter_);
		livePitchShifter_ = nullptr;
	}
}
