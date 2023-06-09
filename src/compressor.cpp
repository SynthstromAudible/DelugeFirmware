/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "compressor.h"
#include "definitions.h"
#include "lookuptables.h"
#include "definitions.h"
#include "song.h"
#include "playbackhandler.h"
#include "FlashStorage.h"

Compressor::Compressor() {
	status = ENVELOPE_STAGE_OFF;
	lastValue = 2147483647;
	pos = 0;
	attack = getParamFromUserValue(PARAM_STATIC_COMPRESSOR_ATTACK, 7);
	release = getParamFromUserValue(PARAM_STATIC_COMPRESSOR_RELEASE, 28);
	pendingHitStrength = 0;

	// I'm so sorry, this is incredibly ugly, but in order to decide the default sync level, we have to look at the current song, or even better the one being preloaded.
	// Default sync level is used obviously for the default synth sound if no SD card inserted, but also some synth presets, possibly just older ones,
	// are saved without this so it can be set to the default at the time of loading.
	Song* song = preLoadedSong;
	if (!song) song = currentSong;
	if (song) {
		sync = 7 - (song->insideWorldTickMagnitude + song->insideWorldTickMagnitudeOffsetFromBPM);
	}
	else {
		sync = 7 - FlashStorage::defaultMagnitude;
	}
}

void Compressor::cloneFrom(Compressor* other) {
	attack = other->attack;
	release = other->release;
	sync = other->sync;
}

void Compressor::registerHit(int32_t strength) {
	pendingHitStrength = combineHitStrengths(pendingHitStrength, strength);
}

void Compressor::registerHitRetrospectively(int32_t strength, uint32_t numSamplesAgo) {
	pendingHitStrength = 0;
	envelopeOffset = 2147483647 - strength;

	uint32_t alteredAttack = getActualAttackRate();
	uint32_t attackStageLengthInSamples = 8388608 / alteredAttack;

	envelopeHeight = 2147483647 - envelopeOffset;

	// If we're still in the attack stage...
	if (numSamplesAgo < attackStageLengthInSamples) {
		pos = numSamplesAgo * alteredAttack;
		status = ENVELOPE_STAGE_ATTACK;
	}

	// Or if past attack stage...
	else {
		uint32_t numSamplesSinceRelease = numSamplesAgo - attackStageLengthInSamples;
		uint32_t alteredRelease = getActualReleaseRate();
		uint32_t releaseStageLengthInSamples = 8388608 / alteredRelease;

		// If we're still in the release stage...
		if (numSamplesSinceRelease < releaseStageLengthInSamples) {
			pos = numSamplesSinceRelease * alteredRelease;
			status = ENVELOPE_STAGE_RELEASE;
		}

		// Or if we're past the release stage...
		else {
			status = ENVELOPE_STAGE_OFF;
		}
	}
}

int32_t Compressor::getActualAttackRate() {
	int32_t alteredAttack;
	if (sync == 0) alteredAttack = attack;
	else {
		int rshiftAmount = (9 - sync) - 2;
		alteredAttack = multiply_32x32_rshift32(attack << 11, playbackHandler.getTimePerInternalTickInverse());

		if (rshiftAmount >= 0) alteredAttack >>= rshiftAmount;
		else alteredAttack <<= -rshiftAmount;
	}
	return alteredAttack;
}

int32_t Compressor::getActualReleaseRate() {
	int32_t alteredRelease;
	if (sync == 0) alteredRelease = release;
	else {
		alteredRelease =
		    multiply_32x32_rshift32(release << 13, playbackHandler.getTimePerInternalTickInverse()) >> (9 - sync);
	}
	return alteredRelease;
}

int32_t Compressor::render(uint16_t numSamples, int32_t shapeValue) {

	// Initial hit detected...
	if (pendingHitStrength != 0) {
		int32_t newOffset = 2147483647 - pendingHitStrength;

		pendingHitStrength = 0;

		// Only actually do anything if this hit is going to cause a bigger dip than we're already currently experiencing
		if (newOffset < lastValue) {
			envelopeOffset = newOffset;

			// If attack is all the way down, jump directly to release stage
			if (attack == attackRateTable[0] << 2) {
				goto prepareForRelease;
			}

			status = ENVELOPE_STAGE_ATTACK;
			envelopeHeight = lastValue - envelopeOffset;
			pos = 0;
		}
	}

	if (status == ENVELOPE_STAGE_ATTACK) {
		pos += numSamples * getActualAttackRate();

		if (pos >= 8388608) {
prepareForRelease:
			pos = 0;
			status = ENVELOPE_STAGE_RELEASE;
			envelopeHeight = 2147483647 - envelopeOffset;
			goto doRelease;
		}
		//lastValue = (multiply_32x32_rshift32(envelopeHeight, decayTable4[pos >> 13]) << 1) + envelopeOffset; // Goes down quickly at first. Bad
		//lastValue = (multiply_32x32_rshift32(envelopeHeight, 2147483647 - (pos << 8)) << 1) + envelopeOffset; // Straight line
		lastValue = (multiply_32x32_rshift32(envelopeHeight, (2147483647 - getDecay4(8388608 - pos, 23))) << 1)
		            + envelopeOffset; // Goes down slowly at first. Great squishiness
		//lastValue = (multiply_32x32_rshift32(envelopeHeight, (2147483647 - decayTable8[1023 - (pos >> 13)])) << 1) + envelopeOffset; // Even slower to accelerate. Loses punch slightly
		//lastValue = (multiply_32x32_rshift32(envelopeHeight, (sineWave[((pos >> 14) + 256) & 1023] >> 1) + 1073741824) << 1) + envelopeOffset; // Sine wave. Sounds a bit flat
		//lastValue = (multiply_32x32_rshift32(envelopeHeight, 2147483647 - pos * (pos >> 15)) << 1) + envelopeOffset; // Parabola. Not bad, but doesn't quite have punchiness
	}
	else if (status == ENVELOPE_STAGE_RELEASE) {
doRelease:
		pos += numSamples * getActualReleaseRate();

		if (pos >= 8388608) {
			status = ENVELOPE_STAGE_OFF;
			goto doOff;
		}

		uint32_t positiveShapeValue = (uint32_t)shapeValue + 2147483648;

		int32_t preValue;

		// This would be the super simple case
		//int curvedness16 = (uint32_t)(positiveShapeValue + 32768) >> 16;

		// And this is the better, more complicated case
		int curvedness16 = (positiveShapeValue >> 15) - (pos >> 7);
		if (curvedness16 < 0) {
			preValue = pos << 8;
		}
		else {
			if (curvedness16 > 65536) curvedness16 = 65536;
			int straightness = 65536 - curvedness16;
			preValue = straightness * (pos >> 8) + (getDecay8(8388608 - pos, 23) >> 16) * curvedness16;
		}

		lastValue = 2147483647 - envelopeHeight + (multiply_32x32_rshift32(preValue, envelopeHeight) << 1);

		//lastValue = 2147483647 - (multiply_32x32_rshift32(decayTable8[pos >> 13], envelopeHeight) << 1); // Upside down exponential curve
		//lastValue = 2147483647 - (((int64_t)((sineWave[((pos >> 14) + 256) & 1023] >> 1) + 1073741824) * (int64_t)envelopeHeight) >> 31); // Sine wave. Not great
		//lastValue = (multiply_32x32_rshift32(pos * (pos >> 15), envelopeHeight) << 1); // Parabola. Doesn't "punch".
	}
	else { // Off
doOff:
		lastValue = 2147483647;
	}

	return lastValue - 2147483647;
}
