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

#include "util/functions.h"
#include "processing/sound/sound.h"
#include "definitions.h"
#include "hid/display/numeric_driver.h"
#include "fatfs/ff.h"
#include "gui/views/view.h"
#include "gui/ui/sound_editor.h"
#include "model/action/action_logger.h"
#include <string.h>
#include "io/uart/uart.h"
#include "hid/encoders.h"
#include "gui/ui/qwerty_ui.h"
#include "hid/display/oled.h"

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "drivers/mtu/mtu.h"
}

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
const uint8_t modButtonX[6] = {0, 0, 1, 1, 2, 3};
const uint8_t modButtonY[6] = {1, 0, 0, 1, 1, 1};
const uint8_t modLedX[6] = {0, 0, 1, 1, 2, 3};
const uint8_t modLedY[6] = {2, 3, 3, 2, 2, 2};
#else
const uint8_t modButtonX[8] = {1, 1, 1, 1, 2, 2, 2, 2};
const uint8_t modButtonY[8] = {0, 1, 2, 3, 0, 1, 2, 3};
const uint8_t modLedX[8] = {1, 1, 1, 1, 2, 2, 2, 2};
const uint8_t modLedY[8] = {0, 1, 2, 3, 0, 1, 2, 3};
#endif

int32_t paramRanges[NUM_PARAMS];
int32_t paramNeutralValues[NUM_PARAMS];

// This is just the range of the user-defined "preset" value, it doesn't apply to the outcome of patch cables
int32_t getParamRange(int p) {
	switch (p) {
	case PARAM_LOCAL_ENV_0_ATTACK:
	case PARAM_LOCAL_ENV_1_ATTACK:
		return 536870912 * 1.5;

	case PARAM_GLOBAL_DELAY_RATE:
		return 536870912;

	case PARAM_LOCAL_PITCH_ADJUST:
	case PARAM_LOCAL_OSC_A_PITCH_ADJUST:
	case PARAM_LOCAL_OSC_B_PITCH_ADJUST:
	case PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST:
	case PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST:
		return 536870912;

	case PARAM_LOCAL_LPF_FREQ:
		return 536870912 * 1.4;

		// For phase width, we have this higher (than I previously did) because these are hibrid params, meaning that with a source (e.g. LFO) patched to them, the might have up to 1073741824 added to them
		// - which would take us to the max user "preset value", which is what we want for phase width
	default:
		return 1073741824;
	}
}

int32_t getParamNeutralValue(int p) {
	switch (p) {
	case PARAM_LOCAL_OSC_A_VOLUME:
	case PARAM_LOCAL_OSC_B_VOLUME:
	case PARAM_GLOBAL_VOLUME_POST_REVERB_SEND:
	case PARAM_LOCAL_NOISE_VOLUME:
	case PARAM_GLOBAL_REVERB_AMOUNT:
	case PARAM_GLOBAL_VOLUME_POST_FX:
	case PARAM_LOCAL_VOLUME:
		return 134217728;

	case PARAM_LOCAL_MODULATOR_0_VOLUME:
	case PARAM_LOCAL_MODULATOR_1_VOLUME:
		return 33554432;

	case PARAM_LOCAL_LPF_FREQ:
		return 2000000;
	case PARAM_LOCAL_HPF_FREQ:
		return 2672947;

	case PARAM_GLOBAL_LFO_FREQ:
	case PARAM_LOCAL_LFO_LOCAL_FREQ:
	case PARAM_GLOBAL_MOD_FX_RATE:
		return 121739; //lfoRateTable[userValue];

	case PARAM_LOCAL_LPF_RESONANCE:
	case PARAM_LOCAL_HPF_RESONANCE:
		return 25 * 10737418; // Room to be quadrupled

	case PARAM_LOCAL_PAN:
	case PARAM_LOCAL_OSC_A_PHASE_WIDTH:
	case PARAM_LOCAL_OSC_B_PHASE_WIDTH:
		return 0;

	case PARAM_LOCAL_ENV_0_ATTACK:
	case PARAM_LOCAL_ENV_1_ATTACK:
		return 4096; //attackRateTable[userValue];

	case PARAM_LOCAL_ENV_0_RELEASE:
	case PARAM_LOCAL_ENV_1_RELEASE:
		return 140 << 9; //releaseRateTable[userValue];

	case PARAM_LOCAL_ENV_0_DECAY:
	case PARAM_LOCAL_ENV_1_DECAY:
		return 70 << 9; //releaseRateTable[userValue] >> 1;

	case PARAM_LOCAL_ENV_0_SUSTAIN:
	case PARAM_LOCAL_ENV_1_SUSTAIN:
	case PARAM_GLOBAL_DELAY_FEEDBACK:
		return 1073741824; //536870912;

	case PARAM_LOCAL_MODULATOR_0_FEEDBACK:
	case PARAM_LOCAL_MODULATOR_1_FEEDBACK:
	case PARAM_LOCAL_CARRIER_0_FEEDBACK:
	case PARAM_LOCAL_CARRIER_1_FEEDBACK:
		return 5931642;

	case PARAM_GLOBAL_DELAY_RATE:
	case PARAM_GLOBAL_ARP_RATE:
	case PARAM_LOCAL_PITCH_ADJUST:
	case PARAM_LOCAL_OSC_A_PITCH_ADJUST:
	case PARAM_LOCAL_OSC_B_PITCH_ADJUST:
	case PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST:
	case PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST:
		return 16777216; // Means we have space to 8x (3-octave-shift) the pitch if we want... (wait, I've since made it 16x smaller)

	case PARAM_GLOBAL_MOD_FX_DEPTH:
		return 526133494; // 2% lower than 536870912

	default:
		return 0;
	}
}

void functionsInit() {

	for (int p = 0; p < NUM_PARAMS; p++) {
		paramRanges[p] = getParamRange(p);
	}

	for (int p = 0; p < NUM_PARAMS; p++) {
		paramNeutralValues[p] = getParamNeutralValue(p);
	}
}

int32_t getFinalParameterValueHybrid(int32_t paramNeutralValue, int32_t patchedValue) {
	// Allows for max output values of +- 1073741824, which the panning code understands as the full range from left to right
	int32_t preLimits = (paramNeutralValue >> 2) + (patchedValue >> 1);
	return signed_saturate<32 - 3>(preLimits) << 2;
}

int32_t getFinalParameterValueVolume(int32_t paramNeutralValue, int32_t patchedValue) {

	// patchedValue's range is ideally +- 536870912, but may get up to 1610612736 due to multiple patch cables having been multiplied

	// No need for max/min here - it's already been taken care of in patchAllCablesToParameter(),
	// ... or if we got here from patchSourceToAllExclusiveCables(), there's no way it could have been too big
	int32_t positivePatchedValue = patchedValue + 536870912;

	// positivePatchedValue's range is ideally 0 ("0") to 1073741824 ("2"), but potentially up to 2147483647 ("4"). 536870912 represents "1".

	// If this parameter is a volume one, then apply a parabola curve to the patched value, at this late stage
	/*
	if (isVolumeParam) {
		// But, our output value can't get bigger than 2147483647 ("4"), which means we have to clip our input off at 1073741824 ("2")
		if (positivePatchedValue >= 1073741824) positivePatchedValue = 2147483647;
		else {
			//int32_t madeSmaller = positivePatchedValue >> 15;
			//positivePatchedValue = (madeSmaller * madeSmaller) << 1;
			positivePatchedValue = (positivePatchedValue >> 15) * (positivePatchedValue >> 14);
		}
	}
	*/

	// This is a temporary (?) fix I've done to allow FM modulator amounts to get past where I clipped off volume params.
	// So now volumes can get higher too. Problem? Not sure.

	// But, our output value can't get bigger than 2147483647 ("4"), which means we have to clip our input off at 1073741824 ("2")
	positivePatchedValue = (positivePatchedValue >> 16) * (positivePatchedValue >> 15);

	//return multiply_32x32_rshift32(positivePatchedValue, paramNeutralValue) << 5;

	// Must saturate, otherwise mod fx depth can easily overflow
	return lshiftAndSaturate<5>(multiply_32x32_rshift32(positivePatchedValue, paramNeutralValue));
}

int32_t getFinalParameterValueLinear(int32_t paramNeutralValue, int32_t patchedValue) {

	// patchedValue's range is ideally +- 536870912, but may get up to 1610612736 due to multiple patch cables having been multiplied

	// No need for max/min here - it's already been taken care of in patchAllCablesToParameter(),
	// ... or if we got here from patchSourceToAllExclusiveCables(), there's no way it could have been too big
	int32_t positivePatchedValue = patchedValue + 536870912;

	// positivePatchedValue's range is ideally 0 ("0") to 1073741824 ("2"), but potentially up to 2147483647 ("4"). 536870912 represents "1".

	// Must saturate, otherwise sustain level can easily overflow
	return lshiftAndSaturate<3>(multiply_32x32_rshift32(positivePatchedValue, paramNeutralValue));
}

int32_t getFinalParameterValueExp(int32_t paramNeutralValue, int32_t patchedValue) {
	return getExp(paramNeutralValue, patchedValue);
}

int32_t getFinalParameterValueExpWithDumbEnvelopeHack(int32_t paramNeutralValue, int32_t patchedValue, int p) {
	// TODO: this is horribly hard-coded, but works for now
	if (p >= PARAM_LOCAL_ENV_0_DECAY && p <= PARAM_LOCAL_ENV_1_RELEASE) {
		return multiply_32x32_rshift32(paramNeutralValue, lookupReleaseRate(patchedValue));
	}
	if (p == PARAM_LOCAL_ENV_0_ATTACK || p == PARAM_LOCAL_ENV_1_ATTACK) {
		patchedValue = -patchedValue;
	}

	return getFinalParameterValueExp(paramNeutralValue, patchedValue);
}

void addAudio(StereoSample* inputBuffer, StereoSample* outputBuffer, int numSamples) {
	StereoSample* inputSample = inputBuffer;
	StereoSample* outputSample = outputBuffer;

	StereoSample* inputBufferEnd = inputBuffer + numSamples;

	do {
		outputSample->l += inputSample->l;
		outputSample->r += inputSample->r;

		outputSample++;
	} while (++inputSample != inputBufferEnd);
}

int32_t cableToLinearParamShortcut(int32_t sourceValue) {
	return sourceValue >> 2;
}

int32_t cableToExpParamShortcut(int32_t sourceValue) {
	return sourceValue >> 2;
}

int refreshTime;
int dimmerInterval = 0;

void setRefreshTime(int newTime) {
	refreshTime = newTime;
	bufferPICPadsUart(PIC_MESSAGE_REFRESH_TIME); // Set refresh rate inverse
	bufferPICPadsUart(refreshTime);
}

void changeRefreshTime(int offset) {
	int newTime = refreshTime + offset;
	if (newTime > 255 || newTime < 1) {
		return;
	}
	setRefreshTime(newTime);
	char buffer[12];
	intToString(refreshTime, buffer);
	numericDriver.displayPopup(buffer);
}

void changeDimmerInterval(int offset) {
	int newInterval = dimmerInterval - offset;
	if (newInterval > 25 || newInterval < 0) {}
	else {
		setDimmerInterval(newInterval);
	}

#if HAVE_OLED
	char text[20];
	strcpy(text, "Brightness: ");
	char* pos = strchr(text, 0);
	intToString((25 - dimmerInterval) << 2, pos);
	pos = strchr(text, 0);
	*(pos++) = '%';
	*pos = 0;
	OLED::popupText(text);
#endif
}

void setDimmerInterval(int newInterval) {
	//Uart::print("dimmerInterval: ");
	//Uart::println(newInterval);
	dimmerInterval = newInterval;

	int newRefreshTime = 23 - newInterval;
	while (newRefreshTime < 6) {
		newRefreshTime++;
		newInterval *= 1.2;
	}

	//Uart::print("newInterval: ");
	//Uart::println(newInterval);

	setRefreshTime(newRefreshTime);

	bufferPICPadsUart(243); // Set dimmer interval
	bufferPICPadsUart(newInterval);
}

char const* sourceToString(uint8_t source) {

	switch (source) {
	case PATCH_SOURCE_LFO_GLOBAL:
		return "lfo1";

	case PATCH_SOURCE_LFO_LOCAL:
		return "lfo2";

	case PATCH_SOURCE_ENVELOPE_0:
		return "envelope1";

	case PATCH_SOURCE_ENVELOPE_1:
		return "envelope2";

	case PATCH_SOURCE_VELOCITY:
		return "velocity";

	case PATCH_SOURCE_NOTE:
		return "note";

	case PATCH_SOURCE_COMPRESSOR:
		return "compressor";

	case PATCH_SOURCE_RANDOM:
		return "random";

	case PATCH_SOURCE_AFTERTOUCH:
		return "aftertouch";

	case PATCH_SOURCE_X:
		return "x";

	case PATCH_SOURCE_Y:
		return "y";

	default:
		return "none";
	}
}

#if HAVE_OLED
char const* getSourceDisplayNameForOLED(int s) {
	switch (s) {
	case PATCH_SOURCE_LFO_GLOBAL:
		return "LFO1";

	case PATCH_SOURCE_LFO_LOCAL:
		return "LFO2";

	case PATCH_SOURCE_ENVELOPE_0:
		return "Envelope 1";

	case PATCH_SOURCE_ENVELOPE_1:
		return "Envelope 2";

	case PATCH_SOURCE_VELOCITY:
		return "Velocity";

	case PATCH_SOURCE_NOTE:
		return "Note";

	case PATCH_SOURCE_COMPRESSOR:
		return "Sidechain";

	case PATCH_SOURCE_RANDOM:
		return "Random";

	case PATCH_SOURCE_AFTERTOUCH:
		return "Aftertouch";

	case PATCH_SOURCE_X:
		return "MPE X";

	case PATCH_SOURCE_Y:
		return "MPE Y";

	default:
		__builtin_unreachable();
		return NULL;
	}
}

char const* getPatchedParamDisplayNameForOled(int p) {

	// These can basically be 13 chars long, or 14 if the last one is a dot.
	switch (p) {

	case PARAM_LOCAL_OSC_A_VOLUME:
		return "Osc1 level";

	case PARAM_LOCAL_OSC_B_VOLUME:
		return "Osc2 level";

	case PARAM_LOCAL_VOLUME:
		return "Level";

	case PARAM_LOCAL_NOISE_VOLUME:
		return "Noise level";

	case PARAM_LOCAL_OSC_A_PHASE_WIDTH:
		return "Osc1 PW";

	case PARAM_LOCAL_OSC_B_PHASE_WIDTH:
		return "Osc2 PW";

	case PARAM_LOCAL_OSC_A_WAVE_INDEX:
		return "Osc1 wave pos.";

	case PARAM_LOCAL_OSC_B_WAVE_INDEX:
		return "Osc2 wave pos.";

	case PARAM_LOCAL_LPF_RESONANCE:
		return "LPF resonance";

	case PARAM_LOCAL_HPF_RESONANCE:
		return "HPF resonance";

	case PARAM_LOCAL_PAN:
		return "Pan";

	case PARAM_LOCAL_MODULATOR_0_VOLUME:
		return "FM mod1 level";

	case PARAM_LOCAL_MODULATOR_1_VOLUME:
		return "FM mod2 level";

	case PARAM_LOCAL_LPF_FREQ:
		return "LPF frequency";

	case PARAM_LOCAL_PITCH_ADJUST:
		return "Pitch";

	case PARAM_LOCAL_OSC_A_PITCH_ADJUST:
		return "Osc1 pitch";

	case PARAM_LOCAL_OSC_B_PITCH_ADJUST:
		return "Osc2 pitch";

	case PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST:
		return "FM mod1 pitch";

	case PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST:
		return "FM mod2 pitch";

	case PARAM_LOCAL_HPF_FREQ:
		return "HPF frequency";

	case PARAM_LOCAL_LFO_LOCAL_FREQ:
		return "LFO2 rate";

	case PARAM_LOCAL_ENV_0_ATTACK:
		return "Env1 attack";

	case PARAM_LOCAL_ENV_0_DECAY:
		return "Env1 decay";

	case PARAM_LOCAL_ENV_0_SUSTAIN:
		return "Env1 sustain";

	case PARAM_LOCAL_ENV_0_RELEASE:
		return "Env1 release";

	case PARAM_LOCAL_ENV_1_ATTACK:
		return "Env2 attack";

	case PARAM_LOCAL_ENV_1_DECAY:
		return "Env2 decay";

	case PARAM_LOCAL_ENV_1_SUSTAIN:
		return "Env2 sustain";

	case PARAM_LOCAL_ENV_1_RELEASE:
		return "Env2 release";

	case PARAM_GLOBAL_LFO_FREQ:
		return "LFO1 rate";

	case PARAM_GLOBAL_VOLUME_POST_FX:
		return "Level";

	case PARAM_GLOBAL_VOLUME_POST_REVERB_SEND:
		return "Level";

	case PARAM_GLOBAL_DELAY_RATE:
		return "Delay rate";

	case PARAM_GLOBAL_DELAY_FEEDBACK:
		return "Delay amount";

	case PARAM_GLOBAL_REVERB_AMOUNT:
		return "Reverb amount";

	case PARAM_GLOBAL_MOD_FX_RATE:
		return "Mod-FX rate";

	case PARAM_GLOBAL_MOD_FX_DEPTH:
		return "Mod-FX depth";

	case PARAM_GLOBAL_ARP_RATE:
		return "Arp. rate";

	case PARAM_LOCAL_MODULATOR_0_FEEDBACK:
		return "Mod1 feedback";

	case PARAM_LOCAL_MODULATOR_1_FEEDBACK:
		return "Mod2 feedback";

	case PARAM_LOCAL_CARRIER_0_FEEDBACK:
		return "Carrier1 feed.";

	case PARAM_LOCAL_CARRIER_1_FEEDBACK:
		return "Carrier2 feed.";

	default:
		__builtin_unreachable();
		return NULL;
	}
}
#endif

uint8_t stringToSource(char const* string) {
	for (int s = 0; s < NUM_PATCH_SOURCES; s++) {
		if (!strcmp(string, sourceToString(s))) {
			return s;
		}
	}
	return PATCH_SOURCE_NONE;
}

bool paramNeedsLPF(int p, bool fromAutomation) {
	switch (p) {

	// For many params, particularly volumes, we do want the param LPF if the user adjusted it,
	// so we don't get stepping, but if it's from step automation, we do want it to adjust instantly,
	// so the new step is instantly at the right volume
	case PARAM_GLOBAL_VOLUME_POST_FX:
	case PARAM_GLOBAL_VOLUME_POST_REVERB_SEND:
	case PARAM_GLOBAL_REVERB_AMOUNT:
	case PARAM_LOCAL_VOLUME:
	case PARAM_LOCAL_PAN:
	case PARAM_LOCAL_LPF_FREQ:
	case PARAM_LOCAL_HPF_FREQ:
	case PARAM_LOCAL_OSC_A_VOLUME:
	case PARAM_LOCAL_OSC_B_VOLUME:
	case PARAM_LOCAL_OSC_A_WAVE_INDEX:
	case PARAM_LOCAL_OSC_B_WAVE_INDEX:
		return !fromAutomation;

	case PARAM_LOCAL_MODULATOR_0_VOLUME:
	case PARAM_LOCAL_MODULATOR_1_VOLUME:
	case PARAM_LOCAL_MODULATOR_0_FEEDBACK:
	case PARAM_LOCAL_MODULATOR_1_FEEDBACK:
	case PARAM_LOCAL_CARRIER_0_FEEDBACK:
	case PARAM_LOCAL_CARRIER_1_FEEDBACK:
	case PARAM_GLOBAL_MOD_FX_DEPTH:
	case PARAM_GLOBAL_DELAY_FEEDBACK:
		return true;

	default:
		return false;
	}
}

char halfByteToHexChar(uint8_t thisHalfByte) {
	if (thisHalfByte < 10) {
		return 48 + thisHalfByte;
	}
	else {
		return 55 + thisHalfByte;
	}
}

char hexCharToHalfByte(unsigned char hexChar) {
	if (hexChar >= 65) {
		return hexChar - 55;
	}
	else {
		return hexChar - 48;
	}
}

void intToHex(uint32_t number, char* output, int numChars) {
	output[numChars] = 0;
	for (int i = numChars - 1; i >= 0; i--) {
		output[i] = halfByteToHexChar(number & 15);
		number >>= 4;
	}
}

uint32_t hexToInt(char const* string) {
	int32_t output = 0;
	while (*string) {
		output <<= 4;
		output |= hexCharToHalfByte(*string);
		string++;
	}
	return output;
}

// length must be >0
uint32_t hexToIntFixedLength(char const* __restrict__ hexChars, int length) {
	uint32_t output = 0;
	char const* const endChar = hexChars + length;
	do {
		output <<= 4;
		output |= hexCharToHalfByte(*hexChars);
		hexChars++;
	} while (hexChars != endChar);

	return output;
}

const float dbIntervals[] = {
    24, // Not really real
    12.1, 7, 5, 3.9, 3.2, 2.6, 2.4, 2, 1.8, 1.7, 1.5, 1.4, 1.3, 1.2, 1.1,
};

int32_t shiftVolumeByDB(int32_t oldValue, float offset) {
	uint32_t oldValuePositive = (uint32_t)oldValue + 2147483648;

	uint32_t currentInterval = oldValuePositive >> 28;

	if (currentInterval >= 1) {

		uint32_t newValuePositive;

		int howFarUpInterval = oldValuePositive & 268435455;
		float howFarUpIntervalFloat = howFarUpInterval / 268435456;

doInterval:

		float dbThisInterval = dbIntervals[currentInterval];

		// How many more dB can we get before we reach the top end of this interval?
		float dbLeftThisInterval = ((float)1 - howFarUpIntervalFloat) * dbThisInterval;

		// If we finish in this interval...
		if (dbLeftThisInterval > offset) {
			float amountOfRemainingDBWeWant = offset / dbLeftThisInterval;
			float newHowFarUpIntervalFloat = howFarUpIntervalFloat + amountOfRemainingDBWeWant;

			uint32_t newHowFarUpInterval = newHowFarUpIntervalFloat * 268435456;
			newValuePositive = (currentInterval << 28) + newHowFarUpInterval;
		}

		// Or if we need more...
		else {
			currentInterval++;

			if (currentInterval == 16) {
				newValuePositive = 4294967295u;
			}
			else {
				offset -= dbLeftThisInterval;
				howFarUpIntervalFloat = 0;
				goto doInterval;
			}
		}

		return newValuePositive - 2147483648u;
	}
	else {
		return oldValue;
	}
}

int32_t interpolateTable(uint32_t input, int numBitsInInput, const uint16_t* table, int numBitsInTableSize) {
	int whichValue = input >> (numBitsInInput - numBitsInTableSize);
	int value1 = table[whichValue];
	int value2 = table[whichValue + 1];

	int rshiftAmount = numBitsInInput - 15 - numBitsInTableSize;
	uint32_t rshifted;
	if (rshiftAmount >= 0) {
		rshifted = input >> rshiftAmount;
	}
	else {
		rshifted = input << (-rshiftAmount);
	}

	int strength2 = rshifted & 32767;
	int strength1 = 32768 - strength2;
	return value1 * strength1 + value2 * strength2;
}

uint32_t interpolateTableInverse(int32_t tableValueBig, int numBitsInLookupOutput, const uint16_t* table,
                                 int numBitsInTableSize) {
	int tableValue = tableValueBig >> 15;
	int tableSize = 1 << numBitsInTableSize;

	int tableDirection = (table[0] < table[tableSize]) ? 1 : -1;

	// Check we're not off either end of the table
	if ((tableValue - table[0]) * tableDirection <= 0) {
		return 0;
	}
	if ((tableValue - table[tableSize]) * tableDirection >= 0) {
		return (1 << numBitsInLookupOutput) - 1;
	}

	int rangeStart = 0;
	int rangeEnd = tableSize;

	while (rangeStart + 1 < rangeEnd) {

		int examinePos = (rangeStart + rangeEnd) >> 1;
		if ((tableValue - table[examinePos]) * tableDirection >= 0) {
			rangeStart = examinePos;
		}
		else {
			rangeEnd = examinePos;
		}
	}

	uint32_t output = rangeStart << (numBitsInLookupOutput - numBitsInTableSize);
	output += (uint64_t)(tableValueBig - ((uint16_t)table[rangeStart] << 15))
	          * (1 << (numBitsInLookupOutput - numBitsInTableSize))
	          / (((uint16_t)table[rangeStart + 1] - (uint16_t)table[rangeStart]) << 15);

	return output;
}

int32_t getDecay8(uint32_t input, uint8_t numBitsInInput) {
	return interpolateTable(input, numBitsInInput, decayTableSmall8);
}

int32_t getDecay4(uint32_t input, uint8_t numBitsInInput) {
	return interpolateTable(input, numBitsInInput, decayTableSmall4);
}

int32_t getExp(int32_t presetValue, int32_t adjustment) {

	int32_t magnitudeIncrease = (adjustment >> 26) + 2;

	// Do "fine" adjustment - change less than one doubling
	int32_t adjustedPresetValue =
	    multiply_32x32_rshift32(presetValue, interpolateTable(adjustment & 67108863, 26, expTableSmall));

	return increaseMagnitudeAndSaturate(adjustedPresetValue, magnitudeIncrease);
}

int32_t quickLog(uint32_t input) {

	uint32_t magnitude = getMagnitudeOld(input);
	uint32_t inputLSBs = increaseMagnitude(input, 26 - magnitude);

	return (magnitude << 25) + (inputLSBs & ~((uint32_t)1 << 26));
}

bool memIsNumericChars(char const* mem, int size) {
	for (int i = 0; i < size; i++) {
		if (*mem < 48 || *mem >= 58) {
			return false;
		}
		mem++;
	}
	return true;
}

bool stringIsNumericChars(char const* str) {
	return memIsNumericChars(str, strlen(str));
}

char const* getThingName(uint8_t instrumentType) {
	if (instrumentType == INSTRUMENT_TYPE_SYNTH) {
		return "SYNT";
	}
	else if (instrumentType == INSTRUMENT_TYPE_KIT) {
		return "KIT";
	}
	else {
		return "SONG";
	}
}

void byteToHex(uint8_t number, char* buffer) {
	buffer[0] = halfByteToHexChar(number >> 4);
	buffer[1] = halfByteToHexChar(number & 15);
	buffer[2] = 0;
}

uint8_t hexToByte(char const* firstChar) {
	uint8_t value = 0;

	if (*firstChar >= 48 && *firstChar < 58) {
		value += *firstChar - 48;
	}
	else {
		value += *firstChar - 55;
	}

	*firstChar++;
	value <<= 4;

	if (*firstChar >= 48 && *firstChar < 58) {
		value += *firstChar - 48;
	}
	else {
		value += *firstChar - 55;
	}

	return value;
}

// May give wrong result for -2147483648
int32_t stringToInt(char const* __restrict__ string) {
	uint32_t number = 0;
	bool isNegative = (*string == '-');
	if (isNegative) {
		string++;
	}

	while (*string >= '0' && *string <= '9') {
		number *= 10;
		number += (*string - '0');
		string++;
	}

	if (isNegative) {
		if (number >= 2147483648) {
			return -2147483648;
		}
		else {
			return -(int32_t)number;
		}
	}
	else {
		return number;
	}
}

int32_t stringToUIntOrError(char const* __restrict__ mem) {
	uint32_t number = 0;
	while (*mem) {
		if (*mem < '0' || *mem > '9') {
			return -1;
		}
		number *= 10;
		number += (*mem - '0');
		mem++;
	}

	return number;
}

int32_t memToUIntOrError(char const* __restrict__ mem, char const* const memEnd) {
	uint32_t number = 0;
	while (mem != memEnd) {
		if (*mem < '0' || *mem > '9') {
			return -1;
		}
		number *= 10;
		number += (*mem - '0');
		mem++;
	}

	return number;
}

void getInstrumentPresetFilename(char const* filePrefix, int16_t presetNumber, int8_t presetSubslotNumber,
                                 char* fileName) {
	strcpy(fileName, filePrefix);
	intToString(presetNumber, &fileName[strlen(fileName)], 3);
	if (presetSubslotNumber != -1) {
		char* prefixPos = fileName + strlen(fileName);
		*prefixPos = presetSubslotNumber + 65;
		*(prefixPos + 1) = 0;
	}
	strcat(fileName, ".XML");
}

char const* oscTypeToString(unsigned int oscType) {
	switch (oscType) {
	case OSC_TYPE_SQUARE:
		return "square";

	case OSC_TYPE_SAW:
		return "saw";

	case OSC_TYPE_ANALOG_SAW_2:
		return "analogSaw";

	case OSC_TYPE_ANALOG_SQUARE:
		return "analogSquare";

	case OSC_TYPE_SINE:
		return "sine";

	case OSC_TYPE_TRIANGLE:
		return "triangle";

	case OSC_TYPE_SAMPLE:
		return "sample";

	case OSC_TYPE_WAVETABLE:
		return "wavetable";

	case OSC_TYPE_INPUT_L:
		return "inLeft";

	case OSC_TYPE_INPUT_R:
		return "inRight";

	case OSC_TYPE_INPUT_STEREO:
		return "inStereo";

	case NUM_OSC_TYPES ... 0xFFFFFFFF:
		__builtin_unreachable();
	}
}

int stringToOscType(char const* string) {

	if (!strcmp(string, "square")) {
		return OSC_TYPE_SQUARE;
	}
	else if (!strcmp(string, "analogSquare")) {
		return OSC_TYPE_ANALOG_SQUARE;
	}
	else if (!strcmp(string, "analogSaw")) {
		return OSC_TYPE_ANALOG_SAW_2;
	}
	else if (!strcmp(string, "saw")) {
		return OSC_TYPE_SAW;
	}
	else if (!strcmp(string, "sine")) {
		return OSC_TYPE_SINE;
	}
	else if (!strcmp(string, "sample")) {
		return OSC_TYPE_SAMPLE;
	}
	else if (!strcmp(string, "wavetable")) {
		return OSC_TYPE_WAVETABLE;
	}
	else if (!strcmp(string, "inLeft")) {
		return OSC_TYPE_INPUT_L;
	}
	else if (!strcmp(string, "inRight")) {
		return OSC_TYPE_INPUT_R;
	}
	else if (!strcmp(string, "inStereo")) {
		return OSC_TYPE_INPUT_STEREO;
	}
	else {
		return OSC_TYPE_TRIANGLE;
	}
}

char const* lfoTypeToString(int oscType) {
	switch (oscType) {
	case LFO_TYPE_SQUARE:
		return "square";

	case LFO_TYPE_SAW:
		return "saw";

	case LFO_TYPE_SINE:
		return "sine";

	case LFO_TYPE_SAH:
		return "sah";

	case LFO_TYPE_RWALK:
		return "rwalk";

	default:
		return "triangle";
	}
}

int stringToLFOType(char const* string) {
	if (!strcmp(string, "square")) {
		return LFO_TYPE_SQUARE;
	}
	else if (!strcmp(string, "saw")) {
		return LFO_TYPE_SAW;
	}
	else if (!strcmp(string, "sine")) {
		return LFO_TYPE_SINE;
	}
	else if (!strcmp(string, "sah")) {
		return LFO_TYPE_SAH;
	}
	else if (!strcmp(string, "rwalk")) {
		return LFO_TYPE_RWALK;
	}
	else {
		return LFO_TYPE_TRIANGLE;
	}
}

char const* synthModeToString(int synthMode) {
	switch (synthMode) {
	case SYNTH_MODE_FM:
		return "fm";

	case SYNTH_MODE_RINGMOD:
		return "ringmod";

	default:
		return "subtractive";
	}
}

int stringToSynthMode(char const* string) {
	if (!strcmp(string, "fm")) {
		return SYNTH_MODE_FM;
	}
	else if (!strcmp(string, "ringmod")) {
		return SYNTH_MODE_RINGMOD;
	}
	else {
		return SYNTH_MODE_SUBTRACTIVE;
	}
}

char const* polyphonyModeToString(int synthMode) {
	switch (synthMode) {
	case POLYPHONY_MONO:
		return "mono";

	case POLYPHONY_AUTO:
		return "auto";

	case POLYPHONY_LEGATO:
		return "legato";

	case POLYPHONY_CHOKE:
		return "choke";

	default: //case POLYPHONY_POLY:
		return "poly";
	}
}

int stringToPolyphonyMode(char const* string) {
	if (!strcmp(string, "mono")) {
		return POLYPHONY_MONO;
	}
	else if (!strcmp(string, "auto")) {
		return POLYPHONY_AUTO;
	}
	else if (!strcmp(string, "0")) {
		return POLYPHONY_AUTO; // Old firmware, pre June 2017
	}
	else if (!strcmp(string, "legato")) {
		return POLYPHONY_LEGATO;
	}
	else if (!strcmp(string, "choke")) {
		return POLYPHONY_CHOKE;
	}
	else if (!strcmp(string, "2")) {
		return POLYPHONY_CHOKE; // Old firmware, pre June 2017
	}
	else {
		return POLYPHONY_POLY;
	}
}

char const* fxTypeToString(int fxType) {
	switch (fxType) {
	case MOD_FX_TYPE_FLANGER:
		return "flanger";

	case MOD_FX_TYPE_CHORUS:
		return "chorus";

	case MOD_FX_TYPE_CHORUS_STEREO:
		return "StereoChorus";

	case MOD_FX_TYPE_PHASER:
		return "phaser";

	default:
		return "none";
	}
}

int stringToFXType(char const* string) {
	if (!strcmp(string, "flanger")) {
		return MOD_FX_TYPE_FLANGER;
	}
	else if (!strcmp(string, "chorus")) {
		return MOD_FX_TYPE_CHORUS;
	}
	else if (!strcmp(string, "StereoChorus")) {
		return MOD_FX_TYPE_CHORUS_STEREO;
	}
	else if (!strcmp(string, "phaser")) {
		return MOD_FX_TYPE_PHASER;
	}
	else {
		return MOD_FX_TYPE_NONE;
	}
}

char const* modFXParamToString(int fxType) {
	switch (fxType) {
	case MOD_FX_PARAM_DEPTH:
		return "depth";

	case MOD_FX_PARAM_FEEDBACK:
		return "feedback";

	default:
		return "offset";
	}
}

int stringToModFXParam(char const* string) {
	if (!strcmp(string, "depth")) {
		return MOD_FX_PARAM_DEPTH;
	}
	else if (!strcmp(string, "feedback")) {
		return MOD_FX_PARAM_FEEDBACK;
	}
	else {
		return MOD_FX_PARAM_OFFSET;
	}
}

char const* filterTypeToString(int fxType) {
	switch (fxType) {
	case FILTER_TYPE_HPF:
		return "hpf";

	case FILTER_TYPE_EQ:
		return "eq";

	default:
		return "lpf";
	}
}

int stringToFilterType(char const* string) {
	if (!strcmp(string, "hpf")) {
		return FILTER_TYPE_HPF;
	}
	else if (!strcmp(string, "eq")) {
		return FILTER_TYPE_EQ;
	}
	else {
		return FILTER_TYPE_LPF;
	}
}

char const* arpModeToString(int mode) {
	switch (mode) {
	case ARP_MODE_UP:
		return "up";

	case ARP_MODE_DOWN:
		return "down";

	case ARP_MODE_BOTH:
		return "both";

	case ARP_MODE_RANDOM:
		return "random";

	default:
		return "off";
	}
}

int stringToArpMode(char const* string) {
	if (!strcmp(string, "up")) {
		return ARP_MODE_UP;
	}
	else if (!strcmp(string, "down")) {
		return ARP_MODE_DOWN;
	}
	else if (!strcmp(string, "both")) {
		return ARP_MODE_BOTH;
	}
	else if (!strcmp(string, "random")) {
		return ARP_MODE_RANDOM;
	}
	else {
		return ARP_MODE_OFF;
	}
}

char const* lpfTypeToString(int lpfType) {
	switch (lpfType) {
	case LPF_MODE_12DB:
		return "12dB";

	case LPF_MODE_TRANSISTOR_24DB_DRIVE:
		return "24dBDrive";

	case LPF_MODE_SVF:
		return "SVF";

	default:
		return "24dB";
	}
}

int stringToLPFType(char const* string) {
	if (!strcmp(string, "24dB")) {
		return LPF_MODE_TRANSISTOR_24DB;
	}
	else if (!strcmp(string, "24dBDrive")) {
		return LPF_MODE_TRANSISTOR_24DB_DRIVE;
	}
	else if (!strcmp(string, "SVF")) {
		return LPF_MODE_SVF;
	}
	else {
		return LPF_MODE_12DB;
	}
}

char const* inputChannelToString(int inputChannel) {
	switch (inputChannel) {
	case AUDIO_INPUT_CHANNEL_LEFT:
		return "left";

	case AUDIO_INPUT_CHANNEL_RIGHT:
		return "right";

	case AUDIO_INPUT_CHANNEL_STEREO:
		return "stereo";

	case AUDIO_INPUT_CHANNEL_BALANCED:
		return "balanced";

	case AUDIO_INPUT_CHANNEL_MIX:
		return "mix";

	case AUDIO_INPUT_CHANNEL_OUTPUT:
		return "output";

	default: // AUDIO_INPUT_CHANNEL_NONE
		return "none";
	}
}

int stringToInputChannel(char const* string) {
	if (!strcmp(string, "left")) {
		return AUDIO_INPUT_CHANNEL_LEFT;
	}
	else if (!strcmp(string, "right")) {
		return AUDIO_INPUT_CHANNEL_RIGHT;
	}
	else if (!strcmp(string, "stereo")) {
		return AUDIO_INPUT_CHANNEL_STEREO;
	}
	else if (!strcmp(string, "balanced")) {
		return AUDIO_INPUT_CHANNEL_BALANCED;
	}
	else if (!strcmp(string, "mix")) {
		return AUDIO_INPUT_CHANNEL_MIX;
	}
	else if (!strcmp(string, "output")) {
		return AUDIO_INPUT_CHANNEL_OUTPUT;
	}
	else {
		return AUDIO_INPUT_CHANNEL_NONE;
	}
}

char const* sequenceDirectionModeToString(int sequenceDirectionMode) {
	switch (sequenceDirectionMode) {
	case SEQUENCE_DIRECTION_FORWARD:
		return "forward";

	case SEQUENCE_DIRECTION_REVERSE:
		return "reverse";

	case SEQUENCE_DIRECTION_PINGPONG:
		return "pingpong";

	case SEQUENCE_DIRECTION_OBEY_PARENT:
		return "none";

	default:
		__builtin_unreachable();
		return "";
	}
}

int stringToSequenceDirectionMode(char const* string) {
	if (!strcmp(string, "reverse")) {
		return SEQUENCE_DIRECTION_REVERSE;
	}
	else if (!strcmp(string, "pingpong")) {
		return SEQUENCE_DIRECTION_PINGPONG;
	}
	else if (!strcmp(string, "obeyParent")) {
		return SEQUENCE_DIRECTION_OBEY_PARENT;
	}
	else {
		return SEQUENCE_DIRECTION_FORWARD;
	}
}

char const* getInstrumentFolder(uint8_t instrumentType) {
	if (instrumentType == INSTRUMENT_TYPE_SYNTH) {
		return "SYNTHS";
	}
	else if (instrumentType == INSTRUMENT_TYPE_KIT) {
		return "KITS";
	}
	else {
		return "SONGS";
	}
}

void getThingFilename(char const* thingName, int16_t currentSlot, int8_t currentSubSlot, char* buffer) {
	strcpy(buffer, thingName);
	intToString(currentSlot, &buffer[strlen(thingName)], 3);
	if (currentSubSlot != -1) {
		buffer[strlen(thingName) + 3] = currentSubSlot + 65;
		buffer[strlen(thingName) + 4] = 0;
	}

	strcat(buffer, ".XML");
}

bool isAudioFilename(char const* filename) {
	char* dotPos = strrchr(filename, '.');
	return (dotPos != 0
	        && (!strcasecmp(dotPos, ".WAV") || !strcasecmp(dotPos, ".AIF") || !strcasecmp(dotPos, ".AIFF")));
}

bool isAiffFilename(char const* filename) {
	char* dotPos = strrchr(filename, '.');
	return (dotPos != 0 && (!strcasecmp(dotPos, ".AIF") || !strcasecmp(dotPos, ".AIFF")));
}

int32_t lookupReleaseRate(int32_t input) {
	int32_t magnitude = 24;
	int32_t whichValue = input >> magnitude;                           // 25
	int32_t howMuchFurther = (input << (31 - magnitude)) & 2147483647; // 6
	whichValue += 32;                                                  // Put it in the range 0 to 64
	if (whichValue < 0) {
		return releaseRateTable64[0];
	}
	else if (whichValue >= 64) {
		return releaseRateTable64[64];
	}
	int32_t value1 = releaseRateTable64[whichValue];
	int32_t value2 = releaseRateTable64[whichValue + 1];
	return (multiply_32x32_rshift32(value2, howMuchFurther)
	        + multiply_32x32_rshift32(value1, 2147483647 - howMuchFurther))
	       << 1;
}

// Gets param *preset* value. Should be labelled better
int32_t getParamFromUserValue(uint8_t p, int8_t userValue) {
	int32_t positive;

	switch (p) {
	case PARAM_STATIC_COMPRESSOR_ATTACK:
		return attackRateTable[userValue] * 4;

	case PARAM_STATIC_COMPRESSOR_RELEASE:
		return releaseRateTable[userValue] * 8;

	case PARAM_LOCAL_OSC_A_PHASE_WIDTH:
	case PARAM_LOCAL_OSC_B_PHASE_WIDTH:
		return (uint32_t)userValue * (85899345 >> 1);

	case PARAM_STATIC_PATCH_CABLE:
	case PARAM_STATIC_COMPRESSOR_VOLUME:
		return userValue * 21474836;

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_BASS:
	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_TREBLE:
		if (userValue == -50) {
			return -2147483648;
		}
		if (userValue == 0) {
			return 0;
		}
		return userValue * 42949672;

	default:
		return (uint32_t)userValue * 85899345 - 2147483648;
	}
}

int getLookupIndexFromValue(int32_t value, const int32_t* table, int maxIndex) {
	int i;
	uint32_t bestDistance = 0xFFFFFFFF;
	int closestIndex;
	for (i = 0; i <= maxIndex; i++) { // No need to actually test the max value itself
		int64_t thisDistance = (int64_t)value - (int64_t)table[i];
		if (thisDistance < 0) {
			thisDistance = -thisDistance;
		}
		if (thisDistance < bestDistance) {
			bestDistance = thisDistance;
			closestIndex = i;
		}
	}
	return closestIndex;
}

uint32_t rshift_round(uint32_t value, unsigned int rshift) {
	return (value + (1 << (rshift - 1))) >> rshift; // I never was quite 100% sure of this...
}

int32_t rshift_round_signed(int32_t value, unsigned int rshift) {
	return (value + (1 << (rshift - 1))) >> rshift; // I never was quite 100% sure of this...
}

int32_t instantTan(int32_t input) {
	int32_t whichValue = input >> 25;                   // 25
	int32_t howMuchFurther = (input << 6) & 2147483647; // 6
	int32_t value1 = tanTable[whichValue];
	int32_t value2 = tanTable[whichValue + 1];
	return (multiply_32x32_rshift32(value2, howMuchFurther)
	        + multiply_32x32_rshift32(value1, 2147483647 - howMuchFurther))
	       << 1;
}

int32_t combineHitStrengths(int32_t strength1, int32_t strength2) {
	// Ideally, we'd do pythagoras on these. But to save computation time, we'll just go half way between the biggest one and the sum
	uint32_t sum = (uint32_t)strength1 + (uint32_t)strength2;
	sum = getMin(sum, (uint32_t)2147483647);
	int32_t maxOne = getMax(strength1, strength2);
	return (maxOne >> 1) + (sum >> 1);
}

uint32_t z = 362436069, w = 521288629, jcong = 380116160;

int random(int upperLimit) {
	return (uint16_t)(CONG >> 16) % (upperLimit + 1);
}

bool shouldDoPanning(int32_t panAmount, int32_t* amplitudeL, int32_t* amplitudeR) {
	if (panAmount == 0) {
		return false;
	}

	int32_t panOffset = getMax((int32_t)-1073741824, (int32_t)(getMin((int32_t)1073741824, (int32_t)panAmount)));
	*amplitudeR = (panAmount >= 0) ? 1073741823 : (1073741824 + panOffset);
	*amplitudeL = (panAmount <= 0) ? 1073741823 : (1073741824 - panOffset);
	return true;
}

void hueToRGB(int32_t hue, unsigned char* rgb) {
	hue = (uint16_t)(hue + 1920) % 192;

	for (int c = 0; c < 3; c++) {
		int channelDarkness;
		if (c == 0) {
			if (hue < 64) {
				channelDarkness = hue;
			}
			else {
				channelDarkness = getMin(64, std::abs(192 - hue));
			}
		}
		else {
			channelDarkness = getMin(64, std::abs(c * 64 - hue));
		}

		if (channelDarkness < 64) {
			rgb[c] = ((uint32_t)getSine(((channelDarkness << 3) + 256) & 1023, 10) + 2147483648u) >> 24;
		}
		else {
			rgb[c] = 0;
		}
	}
}

#define PASTEL_RANGE 230

void hueToRGBPastel(int32_t hue, unsigned char* rgb) {
	hue = (uint16_t)(hue + 1920) % 192;

	for (int c = 0; c < 3; c++) {
		int channelDarkness;
		if (c == 0) {
			if (hue < 64) {
				channelDarkness = hue;
			}
			else {
				channelDarkness = getMin(64, std::abs(192 - hue));
			}
		}
		else {
			channelDarkness = getMin(64, std::abs(c * 64 - hue));
		}

		if (channelDarkness < 64) {
			uint32_t basicValue = (uint32_t)getSine(((channelDarkness << 3) + 256) & 1023, 10)
			                      + 2147483648u; // Goes all the way up to 4294967295
			uint32_t flipped = 4294967295 - basicValue;
			uint32_t flippedScaled = (flipped >> 8) * PASTEL_RANGE;
			rgb[c] = (4294967295 - flippedScaled) >> 24;
		}
		else {
			rgb[c] = 256 - PASTEL_RANGE;
		}
	}
}

uint32_t getLFOInitialPhaseForNegativeExtreme(uint8_t waveType) {
	switch (waveType) {
	case LFO_TYPE_SAW:
		return 2147483648u;

	case LFO_TYPE_SINE:
		return 3221225472u;

	default:
		return 0;
	}
}

uint32_t getLFOInitialPhaseForZero(uint8_t waveType) {
	switch (waveType) {
	case LFO_TYPE_TRIANGLE:
		return 1073741824;

	default:
		return 0;
	}
}

uint32_t getOscInitialPhaseForZero(uint8_t waveType) {
	switch (waveType) {
	case OSC_TYPE_TRIANGLE:
		return 1073741824;

	default:
		return 0;
	}
}

const int32_t pythagTable[257] = {
    1073741824, 1073750016, 1073774592, 1073815549, 1073872888, 1073946604, 1074036696, 1074143157, 1074265984,
    1074405171, 1074560712, 1074732599, 1074920825, 1075125381, 1075346257, 1075583445, 1075836932, 1076106708,
    1076392760, 1076695075, 1077013639, 1077348439, 1077699458, 1078066682, 1078450093, 1078849675, 1079265409,
    1079697276, 1080145258, 1080609334, 1081089484, 1081585686, 1082097918, 1082626157, 1083170380, 1083730563,
    1084306681, 1084898708, 1085506620, 1086130388, 1086769986, 1087425386, 1088096559, 1088783476, 1089486107,
    1090204422, 1090938390, 1091687979, 1092453157, 1093233892, 1094030150, 1094841897, 1095669100, 1096511721,
    1097369728, 1098243082, 1099131748, 1100035689, 1100954867, 1101889243, 1102838780, 1103803438, 1104783177,
    1105777957, 1106787739, 1107812480, 1108852139, 1109906675, 1110976045, 1112060206, 1113159115, 1114272729,
    1115401003, 1116543893, 1117701353, 1118873340, 1120059807, 1121260708, 1122475997, 1123705628, 1124949552,
    1126207724, 1127480095, 1128766616, 1130067241, 1131381920, 1132710604, 1134053244, 1135409791, 1136780194,
    1138164404, 1139562371, 1140974043, 1142399370, 1143838302, 1145290786, 1146756771, 1148236205, 1149729037,
    1151235215, 1152754686, 1154287397, 1155833296, 1157392330, 1158964447, 1160549592, 1162147713, 1163758756,
    1165382668, 1167019394, 1168668882, 1170331077, 1172005924, 1173693371, 1175393362, 1177105843, 1178830760,
    1180568058, 1182317683, 1184079580, 1185853694, 1187639971, 1189438356, 1191248793, 1193071229, 1194905608,
    1196751875, 1198609975, 1200479854, 1202361457, 1204254728, 1206159612, 1208076055, 1210004001, 1211943397,
    1213894186, 1215856315, 1217829727, 1219814369, 1221810186, 1223817123, 1225835126, 1227864139, 1229904109,
    1231954981, 1234016700, 1236089213, 1238172465, 1240266402, 1242370970, 1244486115, 1246611783, 1248747921,
    1250894475, 1253051391, 1255218616, 1257396097, 1259583780, 1261781612, 1263989541, 1266207514, 1268435477,
    1270673379, 1272921166, 1275178788, 1277446191, 1279723323, 1282010133, 1284306569, 1286612580, 1288928113,
    1291253119, 1293587545, 1295931341, 1298284456, 1300646840, 1303018442, 1305399211, 1307789099, 1310188054,
    1312596028, 1315012970, 1317438832, 1319873563, 1322317116, 1324769441, 1327230490, 1329700214, 1332178565,
    1334665495, 1337160955, 1339664900, 1342177280, 1344698049, 1347227159, 1349764564, 1352310217, 1354864072,
    1357426081, 1359996200, 1362574382, 1365160581, 1367754753, 1370356851, 1372966831, 1375584648, 1378210257,
    1380843613, 1383484673, 1386133393, 1388789728, 1391453635, 1394125071, 1396803992, 1399490356, 1402184119,
    1404885240, 1407593675, 1410309382, 1413032321, 1415762448, 1418499723, 1421244103, 1423995549, 1426754019,
    1429519473, 1432291869, 1435071169, 1437857331, 1440650316, 1443450084, 1446256597, 1449069814, 1451889697,
    1454716207, 1457549306, 1460388955, 1463235115, 1466087750, 1468946821, 1471812291, 1474684123, 1477562279,
    1480446723, 1483337417, 1486234326, 1489137413, 1492046642, 1494961978, 1497883384, 1500810825, 1503744266,
    1506683672, 1509629008, 1512580239, 1515537331, 1518500250,
};

int32_t fastPythag(int32_t x, int32_t y) {

	// Make both numbers positive
	if (x < 0) {
		x = -x;
	}
	if (y < 0) {
		y = -y;
	}

	// Make sure x is bigger
	if (y > x) {
		int32_t a = y;
		y = x;
		x = a;
	}

	int32_t divisor = x >> 8;
	if (divisor == 0) {
		return 0;
	}

	int32_t ratio = y / divisor;

	return multiply_32x32_rshift32_rounded(x, pythagTable[ratio]) << 2;
}

const int16_t lanczosKernel[257] = {
    32767, 32753, 32711, 32641, 32544, 32419, 32266, 32087, 31880, 31647, 31388, 31103, 30793, 30458, 30099, 29717,
    29311, 28884, 28434, 27964, 27474, 26964, 26435, 25889, 25326, 24748, 24154, 23546, 22925, 22291, 21647, 20992,
    20328, 19656, 18977, 18291, 17600, 16905, 16207, 15507, 14806, 14105, 13404, 12706, 12010, 11318, 10631, 9950,
    9275,  8607,  7948,  7298,  6658,  6028,  5410,  4804,  4211,  3631,  3066,  2515,  1980,  1460,  956,   470,
    0,     -452,  -886,  -1303, -1700, -2080, -2440, -2782, -3104, -3407, -3691, -3956, -4202, -4429, -4637, -4826,
    -4997, -5149, -5283, -5399, -5498, -5579, -5644, -5691, -5723, -5739, -5740, -5726, -5698, -5656, -5601, -5533,
    -5453, -5361, -5259, -5146, -5023, -4891, -4751, -4602, -4446, -4284, -4115, -3941, -3761, -3578, -3391, -3200,
    -3007, -2812, -2616, -2419, -2222, -2024, -1828, -1632, -1439, -1247, -1058, -872,  -689,  -510,  -336,  -165,
    0,     160,   315,   465,   608,   746,   877,   1002,  1120,  1232,  1336,  1434,  1525,  1609,  1686,  1756,
    1819,  1875,  1925,  1967,  2003,  2032,  2055,  2071,  2081,  2086,  2084,  2077,  2064,  2046,  2023,  1995,
    1963,  1926,  1886,  1841,  1793,  1742,  1687,  1630,  1570,  1508,  1444,  1379,  1311,  1243,  1173,  1103,
    1032,  961,   890,   820,   749,   679,   610,   542,   475,   410,   346,   283,   222,   164,   107,   52,
    0,     -50,   -98,   -143,  -186,  -226,  -263,  -298,  -330,  -360,  -387,  -411,  -433,  -452,  -468,  -482,
    -494,  -503,  -510,  -515,  -517,  -518,  -516,  -513,  -508,  -501,  -492,  -483,  -471,  -459,  -445,  -430,
    -415,  -398,  -381,  -364,  -346,  -327,  -309,  -290,  -271,  -252,  -234,  -215,  -197,  -180,  -163,  -146,
    -130,  -115,  -101,  -87,   -74,   -63,   -52,   -42,   -33,   -25,   -19,   -13,   -8,    -5,    -2,    -1,
    0,
};

#define LANCZOS_A 4

int32_t doLanczos(int32_t* data, int32_t pos, uint32_t posWithinPos, int memoryNumElements) {

	int32_t strengthL[LANCZOS_A];
	int32_t strengthR[LANCZOS_A];

	for (int i = 0; i < LANCZOS_A; i++) {
		strengthL[i] = interpolateTableSigned(16777216 * i + posWithinPos, 26, lanczosKernel, 8);
		strengthR[i] = interpolateTableSigned(16777216 * (i + 1) - posWithinPos, 26, lanczosKernel, 8);
	}

	int howManyLeft = getMin((int32_t)LANCZOS_A, (int32_t)(pos + 1));
	int howManyRight = getMin((int32_t)LANCZOS_A, (int32_t)(memoryNumElements - pos));

	int32_t value = 0;
	for (int i = 0; i < howManyLeft; i++) {
		value += multiply_32x32_rshift32_rounded(strengthL[i], data[pos - i]);
	}
	for (int i = 0; i < howManyRight; i++) {
		value += multiply_32x32_rshift32_rounded(strengthR[i], data[pos + 1 + i]);
	}

	// In a "perfect" world we'd <<1 after this, but no real need since loudness is going to get normalized anyway, and actually we'd probably get some overflows if we did...

	return value;
}

int32_t doLanczosCircular(int32_t* data, int32_t pos, uint32_t posWithinPos, int memoryNumElements) {

	int32_t strengthL[LANCZOS_A];
	int32_t strengthR[LANCZOS_A];

	for (int i = 0; i < LANCZOS_A; i++) {
		strengthL[i] = interpolateTableSigned(16777216 * i + posWithinPos, 26, lanczosKernel, 8);
		strengthR[i] = interpolateTableSigned(16777216 * (i + 1) - posWithinPos, 26, lanczosKernel, 8);
	}

	int32_t value = 0;
	for (int i = 0; i < LANCZOS_A; i++) {
		value += multiply_32x32_rshift32_rounded(strengthL[i],
		                                         data[(pos - i + memoryNumElements) & (memoryNumElements - 1)]);
	}
	for (int i = 0; i < LANCZOS_A; i++) {
		value += multiply_32x32_rshift32_rounded(strengthR[i], data[(pos + 1 + i) & (memoryNumElements - 1)]);
	}

	// In a "perfect" world we'd <<1 after this, but no real need since loudness is going to get normalized anyway, and actually we'd probably get some overflows if we did...

	return value;
}

struct ComparativeNoteNumber {
	int noteNumber;
	int stringLength;
};

// Returns 100000 if the string is not a note name.
// The returned number is *not* a MIDI note. It's arbitrary, used for comparisons only.
// noteChar has been made lowercase, which is why we can't just take it from the string.
ComparativeNoteNumber getComparativeNoteNumberFromChars(char const* string, char noteChar, bool octaveStartsFromA) {
	char const* stringStart = string;
	ComparativeNoteNumber toReturn;

	toReturn.noteNumber = noteChar - 'a';

	if (!octaveStartsFromA) {
		toReturn.noteNumber -= 2;
		if (toReturn.noteNumber < 0) {
			toReturn.noteNumber += 7;
		}
	}

	toReturn.noteNumber *= 3; // To make room for flats and sharps, below.

	string++;
	if (*string == 'b') {
		toReturn.noteNumber--;
		string++;
	}
	else if (*string == '#') {
		toReturn.noteNumber++;
		string++;
	}

	bool numberIsNegative = false;
	if (*string == '-') {
		numberIsNegative = true;
		string++;
	}

	if (*string < '1'
	    || *string
	           > '9') { // There has to be at least some number there if we're to consider this a note name. And it can't start with 0.
		toReturn.noteNumber = 100000;
		toReturn.stringLength = 0;
		return toReturn;
	}

	int number = *string - '0';
	string++;

	while (true) {

		if (*string >= '0' && *string <= '9') {
			number *= 10;
			number += *string - '0';
			string++;
		}
		else {
			if (numberIsNegative) {
				number = -number;
			}
			toReturn.noteNumber += number * 36;
			toReturn.stringLength = string - stringStart;
			return toReturn;
		}
	}
}

// You must set this at some point before calling strcmpspecial. This isn't implemented as an argument because
// sometimes you want to set it way up the call tree, and passing it all the way down is a pain.
bool shouldInterpretNoteNames;

bool octaveStartsFromA; // You must set this if setting shouldInterpretNoteNames to true.

// Returns positive if first > second
// Returns negative if first < second
int strcmpspecial(char const* first, char const* second) {

	int resultIfGetToEndOfBothStrings = 0;

	while (true) {
		bool firstIsFinished = (*first == 0);
		bool secondIsFinished = (*second == 0);

		if (firstIsFinished && secondIsFinished) {
			return resultIfGetToEndOfBothStrings; // If both are finished
		}

		if (firstIsFinished || secondIsFinished) { // If just one is finished
			return (int)*first - (int)*second;
		}

		bool firstIsNumber = (*first >= '0' && *first <= '9');
		bool secondIsNumber = (*second >= '0' && *second <= '9');

		// If they're both numbers...
		if (firstIsNumber && secondIsNumber) {

			// If we haven't yet seen a differing number of leading zeros in a number, see if that exists here.
			if (!resultIfGetToEndOfBothStrings) {
				char const* firstHere = first;
				char const* secondHere = second;
				while (true) {
					char firstChar = *firstHere;
					char secondChar = *secondHere;
					bool firstDigitIsLeadingZero = (firstChar == '0');
					bool secondDigitIsLeadingZero = (secondChar == '0');

					if (firstDigitIsLeadingZero && secondDigitIsLeadingZero) { // If both are zeros, look at next chars.
						firstHere++;
						secondHere++;
						continue;
					}

					//else if (!firstDigitIsLeadingZero || !secondDigitIsLeadingZero) break;	// If both are not zeros, we're done.
					// Actually, the same end result is achieved without that line.

					// If we're still here, one is a leading zero and the other isn't.
					resultIfGetToEndOfBothStrings =
					    (int)firstChar - (int)secondChar; // Will still end up as zero when that needs to happen.
					break;
				}
			}

			int firstNumber = *first - '0';
			int secondNumber = *second - '0';
			first++;
			second++;

			while (*first >= '0' && *first <= '9') {
				firstNumber *= 10;
				firstNumber += *first - '0';
				first++;
			}

			while (*second >= '0' && *second <= '9') {
				secondNumber *= 10;
				secondNumber += *second - '0';
				second++;
			}

			int difference = firstNumber - secondNumber;
			if (difference) {
				return difference;
			}
		}

		// Otherwise, if not both numbers...
		else {

			char firstChar = *first;
			char secondChar = *second;

			// Make lowercase
			if (firstChar >= 'A' && firstChar <= 'Z') {
				firstChar += 32;
			}
			if (secondChar >= 'A' && secondChar <= 'Z') {
				secondChar += 32;
			}

			// If we're doing note ordering...
			if (shouldInterpretNoteNames) {
				ComparativeNoteNumber firstResult, secondResult;
				firstResult.noteNumber = 100000;
				firstResult.stringLength = 0;
				secondResult.noteNumber = 100000;
				secondResult.stringLength = 0;

				if (firstChar >= 'a' && firstChar <= 'g') {
					firstResult = getComparativeNoteNumberFromChars(first, firstChar, octaveStartsFromA);
				}

				if (secondChar >= 'a' && secondChar <= 'g') {
					secondResult = getComparativeNoteNumberFromChars(second, secondChar, octaveStartsFromA);
				}

				if (firstResult.noteNumber == secondResult.noteNumber) {
					if (!firstResult.stringLength && !secondResult.stringLength) {
						goto doNormal;
					}
					first += firstResult.stringLength;
					second += secondResult.stringLength;
				}
				else {
					return firstResult.noteNumber - secondResult.noteNumber;
				}
			}

			else {
doNormal:
				// If they're the same, carry on
				if (firstChar == secondChar) {
					first++;
					second++;
				}

				// Otherwise...
				else {
					// Dot then underscore comes first
					if (firstChar == '.') {
						return -1;
					}
					else if (secondChar == '.') {
						return 1; // We know they're not both the same - see above.
					}

					if (firstChar == '_') {
						return -1;
					}
					else if (secondChar == '_') {
						return 1;
					}

					return (int)firstChar - (int)secondChar;
				}
			}
		}
	}
}

bool charCaseEqual(char firstChar, char secondChar) {
	// Make lowercase
	if (firstChar >= 'A' && firstChar <= 'Z') {
		firstChar += 32;
	}
	if (secondChar >= 'A' && secondChar <= 'Z') {
		secondChar += 32;
	}

	return (firstChar == secondChar);
}

int memcasecmp(char const* first, char const* second, int size) {
	for (int i = 0; i < size; i++) {
		char firstChar = *first;
		char secondChar = *second;

		if (firstChar != secondChar) {

			// Make lowercase
			if (firstChar >= 'A' && firstChar <= 'Z') {
				firstChar += 32;
			}
			if (secondChar >= 'A' && secondChar <= 'Z') {
				secondChar += 32;
			}

			// If they're the same, carry on
			if (firstChar != secondChar) {
				return (firstChar > secondChar) ? 1 : -1;
			}
		}

		first++;
		second++;
	}
	return 0;
}

int stringToFirmwareVersion(char const* firmwareVersionString) {
	if (!strcmp(firmwareVersionString, "1.2.0")) {
		return FIRMWARE_1P2P0;
	}
	else if (!strcmp(firmwareVersionString, "1.3.0-pretest")) {
		return FIRMWARE_1P3P0_PRETEST;
	}
	else if (!strcmp(firmwareVersionString, "1.3.0-beta")) {
		return FIRMWARE_1P3P0_BETA;
	}
	else if (!strcmp(firmwareVersionString, "1.3.0")) {
		return FIRMWARE_1P3P0;
	}
	else if (!strcmp(firmwareVersionString, "1.3.1")) {
		return FIRMWARE_1P3P1;
	}
	else if (!strcmp(firmwareVersionString, "1.3.2")) {
		return FIRMWARE_1P3P2;
	}
	else if (!strcmp(firmwareVersionString, "1.4.0-pretest")) {
		return FIRMWARE_1P4P0_PRETEST;
	}
	else if (!strcmp(firmwareVersionString, "1.4.0-beta")) {
		return FIRMWARE_1P4P0_BETA;
	}
	else if (!strcmp(firmwareVersionString, "1.4.0")) {
		return FIRMWARE_1P4P0;
	}
	else if (!strcmp(firmwareVersionString, "1.5.0-pretest")) {
		return FIRMWARE_1P5P0_PREBETA;
	}
	else if (!strcmp(firmwareVersionString, "2.0.0-beta")) {
		return FIRMWARE_2P0P0_BETA;
	}
	else if (!strcmp(firmwareVersionString, "2.0.0")) {
		return FIRMWARE_2P0P0;
	}
	else if (!strcmp(firmwareVersionString, "2.0.1-beta")) {
		return FIRMWARE_2P0P1_BETA;
	}
	else if (!strcmp(firmwareVersionString, "2.0.1")) {
		return FIRMWARE_2P0P1;
	}
	else if (!strcmp(firmwareVersionString, "2.0.2-beta")) {
		return FIRMWARE_2P0P2_BETA;
	}
	else if (!strcmp(firmwareVersionString, "2.0.2")) {
		return FIRMWARE_2P0P2;
	}
	else if (!strcmp(firmwareVersionString, "2.0.3")) {
		return FIRMWARE_2P0P3;
	}
	else if (!strcmp(firmwareVersionString, "2.1.0-beta")) {
		return FIRMWARE_2P1P0_BETA;
	}
	else if (!strcmp(firmwareVersionString, "2.1.0")) {
		return FIRMWARE_2P1P0;
	}
	else if (!strcmp(firmwareVersionString, "2.1.1-beta")) {
		return FIRMWARE_2P1P1_BETA;
	}
	else if (!strcmp(firmwareVersionString, "2.1.1")) {
		return FIRMWARE_2P1P1;
	}
	else if (!strcmp(firmwareVersionString, "2.1.2-beta")) {
		return FIRMWARE_2P1P2_BETA;
	}
	else if (!strcmp(firmwareVersionString, "2.1.2")) {
		return FIRMWARE_2P1P2;
	}
	else if (!strcmp(firmwareVersionString, "2.1.3-beta")) {
		return FIRMWARE_2P1P3_BETA;
	}
	else if (!strcmp(firmwareVersionString, "2.1.3")) {
		return FIRMWARE_2P1P3;
	}
	else if (!strcmp(firmwareVersionString, "2.1.4-beta")) {
		return FIRMWARE_2P1P4_BETA;
	}
	else if (!strcmp(firmwareVersionString, "2.1.4")) {
		return FIRMWARE_2P1P4;
	}
	else if (!strcmp(firmwareVersionString, "2.2.0-alpha")) { // Same as next one
		return FIRMWARE_3P0P0_ALPHA;
	}
	else if (!strcmp(firmwareVersionString, "3.0.0-alpha")) {
		return FIRMWARE_3P0P0_ALPHA;
	}
	else if (!strcmp(firmwareVersionString, "3.0.0-beta")) {
		return FIRMWARE_3P0P0_BETA;
	}
	else if (!strcmp(firmwareVersionString, "3.0.0")) {
		return FIRMWARE_3P0P0;
	}
	else if (!strcmp(firmwareVersionString, "3.0.1-beta")) {
		return FIRMWARE_3P0P1_BETA;
	}
	else if (!strcmp(firmwareVersionString, "3.0.1")) {
		return FIRMWARE_3P0P1;
	}
	else if (!strcmp(firmwareVersionString, "3.0.2")) {
		return FIRMWARE_3P0P2;
	}
	else if (!strcmp(firmwareVersionString, "3.0.3-alpha")) {
		return FIRMWARE_3P0P3_ALPHA;
	}
	else if (!strcmp(firmwareVersionString, "3.0.3-beta")) {
		return FIRMWARE_3P0P3_BETA;
	}
	else if (!strcmp(firmwareVersionString, "3.0.3")) {
		return FIRMWARE_3P0P3;
	}
	else if (!strcmp(firmwareVersionString, "3.0.4")) {
		return FIRMWARE_3P0P4;
	}
	else if (!strcmp(firmwareVersionString, "3.0.5-beta")) {
		return FIRMWARE_3P0P5_BETA;
	}
	else if (!strcmp(firmwareVersionString, "3.0.5")) {
		return FIRMWARE_3P0P5;
	}
	else if (!strcmp(firmwareVersionString, "3.1.0-alpha")) {
		return FIRMWARE_3P1P0_ALPHA;
	}
	else if (!strcmp(firmwareVersionString, "3.1.0-alpha2")) {
		return FIRMWARE_3P1P0_ALPHA2;
	}
	else if (!strcmp(firmwareVersionString, "3.1.0-beta")) {
		return FIRMWARE_3P1P0_BETA;
	}
	else if (!strcmp(firmwareVersionString, "3.1.0")) {
		return FIRMWARE_3P1P0;
	}
	else if (!strcmp(firmwareVersionString, "3.1.1-beta")) {
		return FIRMWARE_3P1P1_BETA;
	}
	else if (!strcmp(firmwareVersionString, "3.1.1")) {
		return FIRMWARE_3P1P1;
	}
	else if (!strcmp(firmwareVersionString, "3.1.2-beta")) {
		return FIRMWARE_3P1P2_BETA;
	}
	else if (!strcmp(firmwareVersionString, "3.1.2")) {
		return FIRMWARE_3P1P2;
	}
	else if (!strcmp(firmwareVersionString, "3.1.3-beta")) {
		return FIRMWARE_3P1P3_BETA;
	}
	else if (!strcmp(firmwareVersionString, "3.1.3")) {
		return FIRMWARE_3P1P3;
	}
	else if (!strcmp(firmwareVersionString, "3.1.4-beta")) {
		return FIRMWARE_3P1P4_BETA;
	}
	else if (!strcmp(firmwareVersionString, "3.1.4")) {
		return FIRMWARE_3P1P4;
	}
	else if (!strcmp(firmwareVersionString, "3.1.5-beta")) {
		return FIRMWARE_3P1P5_BETA;
	}
	else if (!strcmp(firmwareVersionString, "3.1.5")) {
		return FIRMWARE_3P1P5;
	}
	else if (!strcmp(firmwareVersionString, "3.2.0-alpha")) {
		return FIRMWARE_3P2P0_ALPHA;
	}
	else if (!strcmp(firmwareVersionString, "4.0.0-beta")) {
		return FIRMWARE_4P0P0_BETA;
	}
	else if (!strcmp(firmwareVersionString, "4.0.0")) {
		return FIRMWARE_4P0P0;
	}
	else if (!strcmp(firmwareVersionString, "4.0.1-beta")) {
		return FIRMWARE_4P0P1_BETA;
	}
	else if (!strcmp(firmwareVersionString, "4.0.1")) {
		return FIRMWARE_4P0P1;
	}
	else if (!strcmp(firmwareVersionString, "4.1.0-alpha")) {
		return FIRMWARE_4P1P0_ALPHA;
	}
	else if (!strcmp(firmwareVersionString, "4.1.0-beta")) {
		return FIRMWARE_4P1P0_BETA;
	}
	else if (!strcmp(firmwareVersionString, "4.1.0")) {
		return FIRMWARE_4P1P0;
	}
	else if (!strcmp(firmwareVersionString, "4.1.1-alpha")) {
		return FIRMWARE_4P1P1_ALPHA;
	}
	else if (!strcmp(firmwareVersionString, "4.1.1")) {
		return FIRMWARE_4P1P1;
	}
	else if (!strcmp(firmwareVersionString, "4.1.2")) {
		return FIRMWARE_4P1P2;
	}
	else if (!strcmp(firmwareVersionString, "4.1.3-alpha")) {
		return FIRMWARE_4P1P3_ALPHA;
	}
	else if (!strcmp(firmwareVersionString, "4.1.3-beta")) {
		return FIRMWARE_4P1P3_BETA;
	}
	else if (!strcmp(firmwareVersionString, "4.1.3")) {
		return FIRMWARE_4P1P3;
	}
	else if (!strcmp(firmwareVersionString, "4.1.4-alpha")) {
		return FIRMWARE_4P1P4_ALPHA;
	}
	else if (!strcmp(firmwareVersionString, "4.1.4-beta")) {
		return FIRMWARE_4P1P4_BETA;
	}
	else if (!strcmp(firmwareVersionString, "4.1.4")) {
		return FIRMWARE_4P1P4;
	}

	else {
		return FIRMWARE_TOO_NEW;
	}
}

int howMuchMoreMagnitude(unsigned int to, unsigned int from) {
	return getMagnitudeOld(to) - getMagnitudeOld(from);
}

void noteCodeToString(int noteCode, char* buffer, int* getLengthWithoutDot) {
	char* thisChar = buffer;
	int octave = (noteCode) / 12 - 2;
	int noteCodeWithinOctave = (uint16_t)(noteCode + 120) % (uint8_t)12;

	*thisChar = noteCodeToNoteLetter[noteCodeWithinOctave];
	thisChar++;
	if (noteCodeIsSharp[noteCodeWithinOctave]) {
		*thisChar = HAVE_OLED ? '#' : '.';
		thisChar++;
	}
	intToString(octave, thisChar, 1);

	if (getLengthWithoutDot) {
		*getLengthWithoutDot = strlen(buffer);
		if (noteCodeIsSharp[noteCodeWithinOctave]) {
			(*getLengthWithoutDot)--;
		}
	}
}

void seedRandom() {
	jcong = *TCNT[TIMER_SYSTEM_FAST];
}

#define UnsignedToFloat(u) (((double)((long)(u - 2147483647L - 1))) + 2147483648.0)

double ConvertFromIeeeExtended(unsigned char* bytes /* LCN */) {
	double f;
	int expon;
	unsigned long hiMant, loMant;

	expon = ((bytes[0] & 0x7F) << 8) | (bytes[1] & 0xFF);
	hiMant = ((unsigned long)(bytes[2] & 0xFF) << 24) | ((unsigned long)(bytes[3] & 0xFF) << 16)
	         | ((unsigned long)(bytes[4] & 0xFF) << 8) | ((unsigned long)(bytes[5] & 0xFF));
	loMant = ((unsigned long)(bytes[6] & 0xFF) << 24) | ((unsigned long)(bytes[7] & 0xFF) << 16)
	         | ((unsigned long)(bytes[8] & 0xFF) << 8) | ((unsigned long)(bytes[9] & 0xFF));

	if (expon == 0 && hiMant == 0 && loMant == 0) {
		f = 0;
	}
	else {
		if (expon == 0x7FFF) { /* Infinity or NaN */
			f = HUGE_VAL;
		}
		else {
			expon -= 16383;
			f = ldexp(UnsignedToFloat(hiMant), expon -= 31);
			f += ldexp(UnsignedToFloat(loMant), expon -= 32);
		}
	}

	if (bytes[0] & 0x80) {
		return -f;
	}
	else {
		return f;
	}
}

// Divisor must be positive. Rounds towards negative infinity
int32_t divide_round_negative(int32_t dividend, int32_t divisor) {
	if (dividend >= 0) {
		return (uint32_t)dividend / (uint32_t)divisor;
	}
	else {
		return -((uint32_t)(-dividend - 1) / (uint32_t)divisor) - 1;
	}
}

int getWhichKernel(int32_t phaseIncrement) {
	if (phaseIncrement < 17268826) {
		return 0; // That allows us to go half a semitone up
	}
	else {
		int whichKernel = 1;
		while (phaseIncrement >= 32599202) { // 11.5 semitones up
			phaseIncrement >>= 1;
			whichKernel += 2;
			if (whichKernel == 5) {
				break;
			}
		}

		if (phaseIncrement >= 23051117) { // 5.5 semitones up
			whichKernel++;
		}

		return whichKernel;
	}
}

void dissectIterationDependence(int probability, int* getDivisor, int* getWhichIterationWithinDivisor) {
	int value = (probability & 127) - NUM_PROBABILITY_VALUES - 1;
	int whichRepeat;

	int tryingWhichDivisor;

	for (tryingWhichDivisor = 2; tryingWhichDivisor <= 8; tryingWhichDivisor++) {
		if (value < tryingWhichDivisor) {
			*getWhichIterationWithinDivisor = value;
			break;
		}

		value -= tryingWhichDivisor;
	}

	*getDivisor = tryingWhichDivisor;
}

int encodeIterationDependence(int divisor, int iterationWithinDivisor) {
	int value = iterationWithinDivisor;
	for (int i = 2; i < divisor; i++) {
		value += i;
	}
	return value + 1 + NUM_PROBABILITY_VALUES;
}

int getHowManyCharsAreTheSame(char const* a, char const* b) {
	int count = 0;
	while (true) {
		char charA = *a;
		if (!charA) {
			break;
		}
		char charB = *b;

		// Make uppercase
		if (charA >= 'a' && charA <= 'z') {
			charA -= 32;
		}
		if (charB >= 'a' && charB <= 'z') {
			charB -= 32;
		}

		if (charA != charB) {
			break;
		}

		count++;
		a++;
		b++;
	}
	return count;
}

void greyColourOut(const uint8_t* input, uint8_t* output, int32_t greyProportion) {
	int totalColour;
	totalColour = (int)input[0] + input[1] + input[2]; // max 765

	for (int colour = 0; colour < 3; colour++) {

		int colourValue = input[colour];

		colourValue = rshift_round((uint32_t)colourValue * (uint32_t)(8421504 - greyProportion)
		                               + ((int32_t)totalColour * (greyProportion >> 5)),
		                           23);
		if (colourValue >= 256) {
			colourValue = 255;
		}

		output[colour] = colourValue;
	}
}

void dimColour(uint8_t colour[3]) {
	for (int c = 0; c < 3; c++) {
		if (colour[c] >= 64) {
			colour[c] = 50;
		}
		else {
			colour[c] = 5;
		}
	}
}

bool shouldAbortLoading() {
	return (currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED
	        && (Encoders::encoders[ENCODER_SELECT].detentPos || QwertyUI::predictionInterrupted));
}

// Must supply a char[5] buffer. Or char[30] for OLED.
void getNoteLengthNameFromMagnitude(char* text, int32_t magnitude, bool clarifyPerColumn) {
#if HAVE_OLED
	if (magnitude < 0) {
		uint32_t division = (uint32_t)1 << (0 - magnitude);
		intToString(division, text);
		char* writePos = strchr(text, 0);
		char const* suffix = (*(writePos - 1) == '2') ? "nd" : "th";
		strcpy(writePos, suffix);
		strcpy(writePos + 2, "-notes");
	}
	else {
		uint32_t numBars = (uint32_t)1 << magnitude;
		intToString(numBars, text);
		if (clarifyPerColumn) {
			if (numBars == 1) {
				strcat(text, " bar (per column)");
			}
			else {
				strcat(text, " bars (per column)");
			}
		}
		else {
			strcat(text, "-bar");
		}
	}
#else
	if (magnitude < 0) {
		uint32_t division = (uint32_t)1 << (0 - magnitude);
		if (division <= 9999) {
			intToString(division, text);
			if (division == 2 || division == 32) {
				strcat(text, "ND");
			}
			else if (division <= 99) {
				strcat(text, "TH");
			}
			else if (division <= 999) {
				strcat(text, "T");
			}
		}
		else {
			strcpy(text, "TINY");
		}
	}
	else {
		uint32_t numBars = (uint32_t)1 << magnitude;
		if (numBars <= 9999) {
			intToString(numBars, text);
			uint8_t length = strlen(text);
			if (length == 1) {
				strcat(text, "BAR");
			}
			else if (length <= 3) {
				strcat(text, "B");
			}
		}
		else {
			strcpy(text, "BIG");
		}
	}
#endif
}

char const* getFileNameFromEndOfPath(char const* filePathChars) {
	char const* slashPos = strrchr(filePathChars, '/');
	return slashPos ? (slashPos + 1) : filePathChars;
}

bool doesFilenameFitPrefixFormat(char const* fileName, char const* filePrefix, int prefixLength) {

	if (memcasecmp(fileName, filePrefix, prefixLength)) {
		return false;
	}

	char* dotAddress = strrchr(fileName, '.');
	if (!dotAddress) {
		return false;
	}

	int dotPos = (uint32_t)dotAddress - (uint32_t)fileName;
	if (dotPos < prefixLength + 3) {
		return false;
	}

	if (!memIsNumericChars(&fileName[prefixLength], 3)) {
		return false;
	}

	return true;
}

int fresultToDelugeErrorCode(FRESULT result) {
	switch (result) {
	case FR_OK:
		return NO_ERROR;

	case FR_NO_FILESYSTEM:
		return ERROR_SD_CARD_NO_FILESYSTEM;

	case FR_NO_FILE:
		return ERROR_FILE_NOT_FOUND;

	case FR_NO_PATH:
		return ERROR_FOLDER_DOESNT_EXIST;

	case FR_WRITE_PROTECTED:
		return ERROR_WRITE_PROTECTED;

	case FR_NOT_ENOUGH_CORE:
		return ERROR_INSUFFICIENT_RAM;

	case FR_EXIST:
		return ERROR_FILE_ALREADY_EXISTS;

	default:
		return ERROR_SD_CARD;
	}
}

// This is the same packing format as used by Sequential synthesizers
// See the "Packed Data Format" section of any DSI or sequential manual.

int pack_8bit_to_7bit(uint8_t* dst, int dst_size, uint8_t* src, int src_len) {
	int packets = (src_len + 6) / 7;
	int missing = (7 * packets - src_len); // allow incomplete packets
	int out_len = 8 * packets - missing;
	if (out_len > dst_size)
		return 0;

	for (int i = 0; i < packets; i++) {
		int ipos = 7 * i;
		int opos = 8 * i;
		memset(dst + opos, 0, 8);
		for (int j = 0; j < 7; j++) {
			// incomplete packet
			if (!(ipos + j < src_len))
				break;
			dst[opos + 1 + j] = src[ipos + j] & 0x7f;
			if (src[ipos + j] & 0x80) {
				dst[opos] |= (1 << j);
			}
		}
	}
	return out_len;
}

int unpack_7bit_to_8bit(uint8_t* dst, int dst_size, uint8_t* src, int src_len) {
	int packets = (src_len + 7) / 8;
	int missing = (8 * packets - src_len);
	if (missing == 7) { // this would be weird
		packets--;
		missing = 0;
	}
	int out_len = 7 * packets - missing;
	if (out_len > dst_size)
		return 0;
	for (int i = 0; i < packets; i++) {
		int ipos = 8 * i;
		int opos = 7 * i;
		memset(dst + opos, 0, 7);
		for (int j = 0; j < 7; j++) {
			if (!(j + 1 + ipos < src_len))
				break;
			dst[opos + j] = src[ipos + 1 + j] & 0x7f;
			if (src[ipos] & (1 << j)) {
				dst[opos + j] |= 0x80;
			}
		}
	}
	return 8 * packets;
}

char miscStringBuffer[FILENAME_BUFFER_SIZE] __attribute__((aligned(CACHE_LINE_SIZE)));
char shortStringBuffer[64] __attribute__((aligned(CACHE_LINE_SIZE)));
