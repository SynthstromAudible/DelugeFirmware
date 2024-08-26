/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
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

#include "model/fx/stutterer.h"
#include "dsp/stereo_sample.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"

namespace params = deluge::modulation::params;

Stutterer stutterer{};

void Stutterer::initParams(ParamManager* paramManager) {
	paramManager->getUnpatchedParamSet()->params[params::UNPATCHED_STUTTER_RATE].setCurrentValueBasicForSetup(0);
}

int32_t Stutterer::getStutterRate(ParamManager* paramManager, int32_t magnitude, uint32_t timePerTickInverse) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	int32_t paramValue = unpatchedParams->getValue(params::UNPATCHED_STUTTER_RATE);

	// Quantized Stutter diff
	// Convert to knobPos (range -64 to 64) for easy operation
	int32_t knobPos = unpatchedParams->paramValueToKnobPos(paramValue, nullptr);
	// Add diff "lastQuantizedKnobDiff" (this value will be set if Quantized Stutter is On, zero if not so this will be
	// a no-op)
	knobPos = knobPos + lastQuantizedKnobDiff;
	// Avoid the param to go beyond limits
	if (knobPos < -64) {
		knobPos = -64;
	}
	else if (knobPos > 64) {
		knobPos = 64;
	}
	// Convert back to value range
	paramValue = unpatchedParams->knobPosToParamValue(knobPos, nullptr);

	int32_t rate =
	    getFinalParameterValueExp(paramNeutralValues[params::GLOBAL_DELAY_RATE], cableToExpParamShortcut(paramValue));

	if (sync != 0) {
		rate = multiply_32x32_rshift32(rate, timePerTickInverse);

		// Limit to the biggest number we can store...
		int32_t lShiftAmount = sync + 6 - magnitude;
		int32_t limit = 2147483647 >> lShiftAmount;
		rate = std::min(rate, limit);
		rate <<= lShiftAmount;
	}
	return rate;
}

Error Stutterer::beginStutter(void* source, ParamManagerForTimeline* paramManager, StutterConfig sc, int32_t magnitude,
                              uint32_t timePerTickInverse) {
	stutterConfig = sc;
	currentReverse = stutterConfig.reversed;
	if (stutterConfig.quantized) {
		UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
		int32_t paramValue = unpatchedParams->getValue(params::UNPATCHED_STUTTER_RATE);
		int32_t knobPos = unpatchedParams->paramValueToKnobPos(paramValue, nullptr);
		if (knobPos < -39) {
			knobPos = -16; // 4ths
		}
		else if (knobPos < -14) {
			knobPos = -8; // 8ths
		}
		else if (knobPos < 14) {
			knobPos = 0; // 16ths
		}
		else if (knobPos < 39) {
			knobPos = 8; // 32nds
		}
		else {
			knobPos = 16; // 64ths
		}
		// Save current values for later recovering them
		valueBeforeStuttering = paramValue;
		lastQuantizedKnobDiff = knobPos;

		// When stuttering, we center the value at 0, so the center is the reference for the stutter rate that we
		// selected just before pressing the knob and we use the lastQuantizedKnobDiff value to calculate the relative
		// (real) value
		unpatchedParams->params[params::UNPATCHED_STUTTER_RATE].setCurrentValueBasicForSetup(0);
	}

	// You'd think I should apply "false" here, to make it not add extra space to the buffer, but somehow this seems to
	// sound as good if not better (in terms of ticking / crackling)...
	Error error = buffer.init(getStutterRate(paramManager, magnitude, timePerTickInverse), 0, true);
	if (error == Error::NONE) {
		status = Status::RECORDING;
		sizeLeftUntilRecordFinished = buffer.size();
		stutterSource = source;
	}
	return error;
}
void Stutterer::processStutter(StereoSample* audio, int32_t numSamples, ParamManager* paramManager, int32_t magnitude,
                               uint32_t timePerTickInverse) {
	StereoSample* audioEnd = audio + numSamples;
	StereoSample* thisSample = audio;

	int32_t rate = getStutterRate(paramManager, magnitude, timePerTickInverse);

	buffer.setupForRender(rate);

	if (status == Status::RECORDING) {
		do {
			int32_t strength1;
			int32_t strength2;

			if (buffer.isNative()) {
				buffer.clearAndMoveOn();
				sizeLeftUntilRecordFinished--;
			}
			else {
				strength2 = buffer.advance([&] {
					buffer.clearAndMoveOn();
					sizeLeftUntilRecordFinished--;
				});
				strength1 = 65536 - strength2;
			}

			buffer.write(*thisSample, strength1, strength2);
		} while (++thisSample != audioEnd);

		if (sizeLeftUntilRecordFinished < 0) {
			if (currentReverse) {
				buffer.setCurrent(buffer.end() - 1);
			}
			else {
				buffer.setCurrent(buffer.begin());
			}
			status = Status::PLAYING;
		}
	}
	else { // PLAYING
		do {
			int32_t strength1;
			int32_t strength2;

			if (buffer.isNative()) {
				if (currentReverse) {
					buffer.moveBack(); // move backward in the buffer
				}
				else {
					buffer.moveOn(); // move forward in the buffer
				}
				thisSample->l = buffer.current().l;
				thisSample->r = buffer.current().r;
			}
			else {
				if (currentReverse) {
					strength2 = buffer.retreat([&] { buffer.moveBack(); });
				}
				else {
					strength2 = buffer.advance([&] { buffer.moveOn(); });
				}

				strength1 = 65536 - strength2;

				if (currentReverse) {
					StereoSample* prevPos = &buffer.current() - 1;
					if (prevPos < buffer.begin()) {
						prevPos = buffer.end() - 1; // Wrap around to the end of the buffer
					}
					StereoSample& fromDelay1 = buffer.current();
					StereoSample& fromDelay2 = *prevPos;
					thisSample->l = (multiply_32x32_rshift32(fromDelay1.l, strength1 << 14)
					                 + multiply_32x32_rshift32(fromDelay2.l, strength2 << 14))
					                << 2;
					thisSample->r = (multiply_32x32_rshift32(fromDelay1.r, strength1 << 14)
					                 + multiply_32x32_rshift32(fromDelay2.r, strength2 << 14))
					                << 2;
				}
				else {
					StereoSample* nextPos = &buffer.current() + 1;
					if (nextPos == buffer.end()) {
						nextPos = buffer.begin();
					}
					StereoSample& fromDelay1 = buffer.current();
					StereoSample& fromDelay2 = *nextPos;
					thisSample->l = (multiply_32x32_rshift32(fromDelay1.l, strength1 << 14)
					                 + multiply_32x32_rshift32(fromDelay2.l, strength2 << 14))
					                << 2;
					thisSample->r = (multiply_32x32_rshift32(fromDelay1.r, strength1 << 14)
					                 + multiply_32x32_rshift32(fromDelay2.r, strength2 << 14))
					                << 2;
				}
			}

			// If ping-pong is active and we're at the start or end of the buffer, reverse the direction
			if (stutterConfig.pingPong
			    && ((currentReverse && &buffer.current() == buffer.begin())
			        || (!currentReverse && &buffer.current() == buffer.end() - 1))) {
				currentReverse = !currentReverse;
			}
		} while (++thisSample != audioEnd);
	}
}

// paramManager is optional - if you don't send it, it won't change the stutter rate
void Stutterer::endStutter(ParamManagerForTimeline* paramManager) {
	buffer.discard();
	status = Status::OFF;

	bool automationOccurred = false;

	if (paramManager) {

		UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

		if (stutterConfig.quantized) {
			// Sset back the value it had just before stuttering so orange LEDs are redrawn.
			unpatchedParams->params[params::UNPATCHED_STUTTER_RATE].setCurrentValueBasicForSetup(valueBeforeStuttering);
		}
		else {
			// Regular Stutter FX (if below middle value, reset it back to middle)
			// Normally we shouldn't call this directly, but it's ok because automation isn't allowed for stutter anyway
			if (unpatchedParams->getValue(params::UNPATCHED_STUTTER_RATE) < 0) {
				unpatchedParams->params[params::UNPATCHED_STUTTER_RATE].setCurrentValueBasicForSetup(0);
			}
		}
	}
	// Reset temporary and diff values for Quantized stutter
	lastQuantizedKnobDiff = 0;
	valueBeforeStuttering = 0;
	stutterSource = nullptr;
}
