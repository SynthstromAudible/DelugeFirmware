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
#include "synth/source/input_source.h"
#include "board_config.h"                      // NUM_MONO_INPUT_CHANNELS, SSI_RX_BUFFER_NUM_SAMPLES
#include "memory/memory_allocator_interface.h" // allocMaxSpeed / delugeDealloc (the thin allocator seam)
#include "synth/source/audio_input_port.h"
#include "synth/source/live/live_input_buffer.h"
#include "synth/source/live/live_pitch_shifter.h"
#include "util/fixedpoint.h"
#include <cstring>
#include <new>

extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

using deluge::synth::InputChannel;

namespace {
/// The configured channel (latched at note-on) ⇆ the `OscType` the live-input port + pitch-shifter still speak.
/// Keeps the `OscType` coupling local to this translation unit; the rest of the synth source is `InputChannel`.
InputChannel channelFromOscType(OscType inputType) {
	switch (inputType) {
	case OscType::INPUT_R:
		return InputChannel::RIGHT;
	case OscType::INPUT_STEREO:
		return InputChannel::STEREO;
	default:
		return InputChannel::LEFT;
	}
}
OscType oscTypeForChannel(InputChannel channel) {
	switch (channel) {
	case InputChannel::RIGHT:
		return OscType::INPUT_R;
	case InputChannel::STEREO:
		return OscType::INPUT_STEREO;
	default:
		return OscType::INPUT_L;
	}
}
} // namespace

namespace deluge::synth {

void InputSource::prepareForNoteOn(OscType inputType) {
	channel_ = channelFromOscType(inputType);
}

void InputSource::render(int32_t* oscBuffer, int32_t numSamples, uint32_t phaseIncrement, int32_t sourceAmplitude,
                         int32_t amplitudeIncrement, bool stereoBuffer, bool stereoUnison, int32_t amplitudeL,
                         int32_t amplitudeR, int32_t interpolationBufferSize) {
	// If pitch shifting and we weren't previously...
	if (phaseIncrement != kMaxSampleValue) {

		if (!livePitchShifter_) {

			// A configured-stereo channel folds to the left channel when the host has no stereo input.
			InputChannel channelNow = channel_;
			if (channelNow == InputChannel::STEREO && !in_->stereoInputAvailable()) {
				channelNow = InputChannel::LEFT;
			}
			OscType inputTypeNow = oscTypeForChannel(channelNow);

			LiveInputBuffer* liveInputBuffer = in_->bufferForChannel(inputTypeNow, true);

			if (liveInputBuffer) {

				void* memory = allocMaxSpeed(sizeof(LivePitchShifter));

				if (memory) {
					livePitchShifter_ = new (memory) LivePitchShifter(inputTypeNow, phaseIncrement);
				}
			}
		}
	}

	// If not pitch shifting and we were previously...
	else {
		if (livePitchShifter_ && livePitchShifter_->mayBeRemovedWithoutClick()) {
			livePitchShifter_->~LivePitchShifter();
			delugeDealloc(livePitchShifter_);
			livePitchShifter_ = nullptr;
		}
	}

	// Yes pitch shifting
	if (livePitchShifter_) {
		// With unison stereo spread, oscBuffer is an interleaved stereo buffer. A mono input must be rendered
		// to a temporary mono buffer first and then panned across it, otherwise the mono render would write
		// into the interleaved buffer as if it were mono and produce noise. Either way the shifter reports the
		// hops it performed; the port folds them into the host's CPU-load accounting.
		if (stereoUnison && livePitchShifter_->numChannels == 1) {
			int32_t* renderBuffer = spareRenderingBuffer[2];
			memset(renderBuffer, 0, numSamples * sizeof(int32_t));

			in_->reportHops(livePitchShifter_->render(renderBuffer, numSamples, phaseIncrement, sourceAmplitude,
			                                          amplitudeIncrement, interpolationBufferSize, *in_));

			for (int32_t i = 0; i < numSamples; i++) {
				oscBuffer[(i << 1)] += multiply_32x32_rshift32(renderBuffer[i], amplitudeL) << 2;
				oscBuffer[(i << 1) + 1] += multiply_32x32_rshift32(renderBuffer[i], amplitudeR) << 2;
			}
		}
		else {
			in_->reportHops(livePitchShifter_->render(oscBuffer, numSamples, phaseIncrement, sourceAmplitude,
			                                          amplitudeIncrement, interpolationBufferSize, *in_));
		}
	}

	// No pitch shifting
	else {

		int32_t* __restrict__ oscBufferPos = oscBuffer;
		int32_t const* __restrict__ inputReadPos = in_->ringReadPos();
		int32_t const* const inputRingEnd = in_->ringEnd();
		int32_t sourceAmplitudeThisUnison = sourceAmplitude;

		// Just left, or just right, or if (stereo but there's only the internal, mono mic)
		if (channel_ != InputChannel::STEREO || !in_->stereoInputAvailable()) {

			int32_t channelOffset;
			// If right, but not internal mic
			if (channel_ == InputChannel::RIGHT && in_->stereoInputAvailable()) {
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
					if (inputReadPos >= inputRingEnd) {
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
					if (inputReadPos >= inputRingEnd) {
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
				if (inputReadPos >= inputRingEnd) {
					inputReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
				}
			} while (oscBufferPos != oscBufferEnd);
		}
	}
}

SourceRenderResult InputSource::renderSource(const SourceRenderContext& ctx) {
	// Live input never self-unassigns; channel routing follows the channel latched at note-on (see render()).
	render(ctx.oscBuffer, ctx.numSamples, ctx.phaseIncrement, ctx.sourceAmplitude, ctx.amplitudeIncrement,
	       ctx.stereoBuffer, ctx.stereoUnison, ctx.amplitudeL, ctx.amplitudeR, ctx.interpolationBufferSize);
	return deluge::synth::SourceRenderResult::ACTIVE;
}

void InputSource::unassign() {
	// Matches the legacy VoiceUnisonPartSource::unassign exactly: a dealloc without the destructor (the
	// render-stop path above does call ~LivePitchShifter()). A pre-existing inconsistency — preserved here, not
	// "fixed", since the offline golden does not exercise the lifecycle and the behaviour change is unverified.
	if (livePitchShifter_ != nullptr) {
		delugeDealloc(livePitchShifter_);
		livePitchShifter_ = nullptr;
	}
}

} // namespace deluge::synth
