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

#include "modulation/sidechain/sidechain.h"
#include "definitions_cxx.hpp"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "storage/flash_storage.h"
#include "util/lookuptables/lookuptables.h"

SideChain::SideChain() {
	status = EnvelopeStage::OFF;
	lastValue = 2147483647;
	pos = 0;
	attack = getParamFromUserValue(util::to_underlying(deluge::modulation::params::STATIC_SIDECHAIN_ATTACK), 7);
	release = getParamFromUserValue(util::to_underlying(deluge::modulation::params::STATIC_SIDECHAIN_RELEASE), 28);
	pendingHitStrength = 0;

	// I'm so sorry, this is incredibly ugly, but in order to decide the default sync level, we have to look at the
	// current song, or even better the one being preloaded. Default sync level is used obviously for the default synth
	// sound if no SD card inserted, but also some synth presets, possibly just older ones, are saved without this so it
	// can be set to the default at the time of loading.
	Song* song = preLoadedSong;
	if (!song) {
		song = currentSong;
	}
	if (song) {
		syncLevel = (SyncLevel)(7 - (song->insideWorldTickMagnitude + song->insideWorldTickMagnitudeOffsetFromBPM));
	}
	else {
		syncLevel = (SyncLevel)(7 - FlashStorage::defaultMagnitude);
	}
	syncType = SYNC_TYPE_EVEN;
}

void SideChain::cloneFrom(SideChain* other) {
	attack = other->attack;
	release = other->release;
	syncType = other->syncType;
	syncLevel = other->syncLevel;
}

void SideChain::registerHit(int32_t strength) {
	pendingHitStrength = combineHitStrengths(pendingHitStrength, strength);
}

void SideChain::registerHitRetrospectively(int32_t strength, uint32_t numSamplesAgo) {
	pendingHitStrength = 0;
	envelopeOffset = ONE_Q31 - strength;

	uint32_t alteredAttack = getActualAttackRate();
	uint32_t attackStageLengthInSamples = 8388608 / alteredAttack;

	envelopeHeight = ONE_Q31 - envelopeOffset;

	// If we're still in the attack stage...
	if (numSamplesAgo < attackStageLengthInSamples) {
		pos = numSamplesAgo * alteredAttack;
		status = EnvelopeStage::ATTACK;
	}

	// Or if past attack stage...
	else {
		uint32_t numSamplesSinceRelease = numSamplesAgo - attackStageLengthInSamples;
		uint32_t alteredRelease = getActualReleaseRate();
		uint32_t releaseStageLengthInSamples = 8388608 / alteredRelease;

		// If we're still in the release stage...
		if (numSamplesSinceRelease < releaseStageLengthInSamples) {
			pos = numSamplesSinceRelease * alteredRelease;
			status = EnvelopeStage::RELEASE;
		}

		// Or if we're past the release stage...
		else {
			status = EnvelopeStage::OFF;
		}
	}
}

int32_t SideChain::getActualAttackRate() {
	int32_t alteredAttack;
	if (syncLevel == SYNC_LEVEL_NONE) {
		alteredAttack = attack;
	}
	else {
		int32_t rshiftAmount = (9 - syncLevel) - 2;
		alteredAttack = multiply_32x32_rshift32(attack << 11, playbackHandler.getTimePerInternalTickInverse());

		if (rshiftAmount >= 0) {
			alteredAttack >>= rshiftAmount;
		}
		else {
			alteredAttack <<= -rshiftAmount;
		}
	}
	return alteredAttack;
}

int32_t SideChain::getActualReleaseRate() {
	int32_t alteredRelease;
	if (syncLevel == SYNC_LEVEL_NONE) {
		alteredRelease = release;
	}
	else {
		alteredRelease =
		    multiply_32x32_rshift32(release << 13, playbackHandler.getTimePerInternalTickInverse()) >> (9 - syncLevel);
	}
	return alteredRelease;
}

int32_t SideChain::render(uint16_t numSamples, int32_t shapeValue) {

	// Initial hit detected...
	if (pendingHitStrength != 0) {
		int32_t newOffset = ONE_Q31 - pendingHitStrength;

		pendingHitStrength = 0;

		// Only actually do anything if this hit is going to cause a bigger dip than we're already currently
		// experiencing
		if (newOffset < lastValue) {
			envelopeOffset = newOffset;

			// If attack is all the way down, jump directly to release stage
			if (attack == attackRateTable[0] << 2) {
				goto prepareForRelease;
			}

			status = EnvelopeStage::ATTACK;
			envelopeHeight = lastValue - envelopeOffset;
			pos = 0;
		}
	}

	if (status == EnvelopeStage::ATTACK) {
		pos += numSamples * getActualAttackRate();

		if (pos >= 8388608) {
prepareForRelease:
			pos = 0;
			status = EnvelopeStage::RELEASE;
			envelopeHeight = ONE_Q31 - envelopeOffset;
			goto doRelease;
		}
		// lastValue = (multiply_32x32_rshift32(envelopeHeight, decayTable4[pos >> 13]) << 1) + envelopeOffset; // Goes
		// down quickly at first. Bad lastValue = (multiply_32x32_rshift32(envelopeHeight, 2147483647 - (pos << 8)) <<
		// 1) + envelopeOffset; // Straight line
		lastValue = (multiply_32x32_rshift32(envelopeHeight, (ONE_Q31 - getDecay4(8388608 - pos, 23))) << 1)
		            + envelopeOffset; // Goes down slowly at first. Great squishiness
		// lastValue = (multiply_32x32_rshift32(envelopeHeight, (2147483647 - decayTable8[1023 - (pos >> 13)])) << 1) +
		// envelopeOffset; // Even slower to accelerate. Loses punch slightly lastValue =
		// (multiply_32x32_rshift32(envelopeHeight, (sineWave[((pos >> 14) + 256) & 1023] >> 1) + 1073741824) << 1) +
		// envelopeOffset; // Sine wave. Sounds a bit flat lastValue = (multiply_32x32_rshift32(envelopeHeight,
		// 2147483647 - pos * (pos >> 15)) << 1) + envelopeOffset; // Parabola. Not bad, but doesn't quite have
		// punchiness
	}
	else if (status == EnvelopeStage::RELEASE) {
doRelease:
		pos += numSamples * getActualReleaseRate();

		if (pos >= 8388608) {
			status = EnvelopeStage::OFF;
			goto doOff;
		}

		uint32_t positiveShapeValue = (uint32_t)shapeValue + 2147483648;

		int32_t preValue;

		// This would be the super simple case
		// int32_t curvedness16 = (uint32_t)(positiveShapeValue + 32768) >> 16;

		// And this is the better, more complicated case
		int32_t curvedness16 = (positiveShapeValue >> 15) - (pos >> 7);
		if (curvedness16 < 0) {
			preValue = pos << 8;
		}
		else {
			if (curvedness16 > 65536) {
				curvedness16 = 65536;
			}
			int32_t straightness = 65536 - curvedness16;
			preValue = straightness * (pos >> 8) + (getDecay8(8388608 - pos, 23) >> 16) * curvedness16;
		}

		lastValue = ONE_Q31 - envelopeHeight + (multiply_32x32_rshift32(preValue, envelopeHeight) << 1);

		// lastValue = 2147483647 - (multiply_32x32_rshift32(decayTable8[pos >> 13], envelopeHeight) << 1); // Upside
		// down exponential curve lastValue = 2147483647 - (((int64_t)((sineWave[((pos >> 14) + 256) & 1023] >> 1) +
		// 1073741824) * (int64_t)envelopeHeight) >> 31); // Sine wave. Not great lastValue =
		// (multiply_32x32_rshift32(pos * (pos >> 15), envelopeHeight) << 1); // Parabola. Doesn't "punch".
	}
	else { // Off
doOff:
		lastValue = ONE_Q31;
	}

	return lastValue - ONE_Q31;
}
