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
#include "model/sample/sample.h"
#include "util/lookuptables/lookuptables.h"

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

		int32_t noteWithinOctave = (uint16_t)(transpose + 240) % 12;
		int32_t octave = ((uint16_t)(transpose + 120) / 12) - 10;

		neutralPhaseIncrement = noteIntervalTable[noteWithinOctave] >> (6 - octave);

		if (((Sample*)audioFile)->sampleRate != kSampleRate) {
			neutralPhaseIncrement = (uint64_t)neutralPhaseIncrement * ((Sample*)audioFile)->sampleRate / kSampleRate;
		}

		if (cents) {
			int32_t multiplier = interpolateTable(2147483648u + (int32_t)cents * 42949672, 32, centAdjustTableSmall);
			neutralPhaseIncrement = multiply_32x32_rshift32(neutralPhaseIncrement, multiplier) << 2;
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
