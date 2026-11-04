/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "model/sample/sample_holder_for_clip.h"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "model/sample/clip_increment.h"
#include "model/sample/sample.h"
#include "util/lookuptables/lookuptables.h"
#include "util/phase_increment_fine_tuner.h"

using deluge::dsp::fixed::kLinearTableInterpolator;

SampleHolderForClip::SampleHolderForClip() {
	transpose = 0;
	cents = 0;
}

SampleHolderForClip::~SampleHolderForClip() {
}

void SampleHolderForClip::setAudioFile(AudioFile* newAudioFile, bool reversed, bool manuallySelected,
                                       int32_t clusterLoadInstruction) {

	SampleHolder::setAudioFile(newAudioFile, reversed, manuallySelected, clusterLoadInstruction);

	recalculateNeutralPhaseIncrement();
}

void SampleHolderForClip::recalculateNeutralPhaseIncrement() {

	if (audioFile) {

		neutralPhaseIncrement = transposed_neutral(transpose, ((Sample*)audioFile)->sampleRate, kSampleRate);

		if (cents) {
			int32_t multiplier = kLinearTableInterpolator.interpolate(
			    2147483648u + PhaseIncrementFineTuner::cents_step(cents), 32, centAdjustTableSmall);
			// The increment read as Q31 (it stays under 2^31) times the cents' Q30 multiplier, moved back to Q31
			neutralPhaseIncrement = mul_as_wrap<deluge::dsp::Q<31>>(deluge::dsp::Q<31>::from_raw(neutralPhaseIncrement),
			                                                        deluge::dsp::Q<30>::from_raw(multiplier))
			                            .raw();
		}
	}
}

void SampleHolderForClip::beenClonedFrom(SampleHolderForClip const* other, bool reversed) {
	transpose = other->transpose;
	cents = other->cents;
	SampleHolder::beenClonedFrom(other, reversed);
}

void SampleHolderForClip::sampleBeenSet(bool reversed, bool manuallySelected) {

	if (manuallySelected && ((Sample*)audioFile)->fileLoopEndSamples
	    && ((Sample*)audioFile)->fileLoopEndSamples <= ((Sample*)audioFile)->lengthInSamples) {

		endPos = ((Sample*)audioFile)->fileLoopEndSamples;

		// Grab loop start from file too, if it's not erroneously late
		if (((Sample*)audioFile)->fileLoopStartSamples < ((Sample*)audioFile)->lengthInSamples
		    && (!((Sample*)audioFile)->fileLoopEndSamples
		        || ((Sample*)audioFile)->fileLoopStartSamples < ((Sample*)audioFile)->fileLoopEndSamples)) {
			startPos =
			    ((Sample*)audioFile)->fileLoopStartSamples; // If it's 0, that'll translate to meaning no loop start
			                                                // pos, which is exactly what we want in that case
		}
	}
}
