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
#include "definitions_cxx.hpp"
#include "fatfs/ff.h"
#include "gui/colour/colour.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/ui/qwerty_ui.h"
#include "hid/display/display.h"
#include "hid/encoders.h"
#include "modulation/arpeggiator.h"
#include "processing/sound/sound.h"
#include <cmath>
#include <string.h>

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "drivers/mtu/mtu.h"
}

namespace params = deluge::modulation::params;
namespace encoders = deluge::hid::encoders;
using namespace deluge;
using params::kNumParams;

const uint8_t modButtonX[8] = {1, 1, 1, 1, 2, 2, 2, 2};
const uint8_t modButtonY[8] = {0, 1, 2, 3, 0, 1, 2, 3};
const uint8_t modLedX[8] = {1, 1, 1, 1, 2, 2, 2, 2};
const uint8_t modLedY[8] = {0, 1, 2, 3, 0, 1, 2, 3};

int32_t paramRanges[kNumParams];
int32_t paramNeutralValues[kNumParams];

// This is just the range of the user-defined "preset" value, it doesn't apply to the outcome of patch cables
int32_t getParamRange(int32_t p) {
	switch (p) {
	case params::LOCAL_ENV_0_ATTACK:
	case params::LOCAL_ENV_1_ATTACK:
		return 536870912 * 1.5;

	case params::GLOBAL_DELAY_RATE:
		return 536870912;

	case params::LOCAL_PITCH_ADJUST:
	case params::LOCAL_OSC_A_PITCH_ADJUST:
	case params::LOCAL_OSC_B_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
		return 536870912;

	case params::LOCAL_LPF_FREQ:
		return 536870912 * 1.4;

		// For phase width, we have this higher (than I previously did) because these are hibrid params, meaning that
		// with a source (e.g. LFO) patched to them, the might have up to 1073741824 added to them
		// - which would take us to the max user "preset value", which is what we want for phase width
	default:
		return 1073741824;
	}
}

int32_t getParamNeutralValue(int32_t p) {
	switch (p) {
	case params::LOCAL_OSC_A_VOLUME:
	case params::LOCAL_OSC_B_VOLUME:
	case params::GLOBAL_VOLUME_POST_REVERB_SEND:
	case params::LOCAL_NOISE_VOLUME:
	case params::GLOBAL_REVERB_AMOUNT:
	case params::GLOBAL_VOLUME_POST_FX:
	case params::LOCAL_VOLUME:
		return 134217728;

	case params::LOCAL_MODULATOR_0_VOLUME:
	case params::LOCAL_MODULATOR_1_VOLUME:
		return 33554432;

	case params::LOCAL_LPF_FREQ:
		return 2000000;
	case params::LOCAL_HPF_FREQ:
		return 2672947;

	case params::GLOBAL_LFO_FREQ:
	case params::LOCAL_LFO_LOCAL_FREQ:
	case params::GLOBAL_MOD_FX_RATE:
		return 121739; // lfoRateTable[userValue];

	case params::LOCAL_LPF_RESONANCE:
	case params::LOCAL_HPF_RESONANCE:
	case params::LOCAL_LPF_MORPH:
	case params::LOCAL_HPF_MORPH:
	case params::LOCAL_FOLD:
		return 25 * 10737418; // Room to be quadrupled

	case params::LOCAL_PAN:
	case params::LOCAL_OSC_A_PHASE_WIDTH:
	case params::LOCAL_OSC_B_PHASE_WIDTH:
		return 0;

	case params::LOCAL_ENV_0_ATTACK:
	case params::LOCAL_ENV_1_ATTACK:
		return 4096; // attackRateTable[userValue];

	case params::LOCAL_ENV_0_RELEASE:
	case params::LOCAL_ENV_1_RELEASE:
		return 140 << 9; // releaseRateTable[userValue];

	case params::LOCAL_ENV_0_DECAY:
	case params::LOCAL_ENV_1_DECAY:
		return 70 << 9; // releaseRateTable[userValue] >> 1;

	case params::LOCAL_ENV_0_SUSTAIN:
	case params::LOCAL_ENV_1_SUSTAIN:
	case params::GLOBAL_DELAY_FEEDBACK:
		return 1073741824; // 536870912;

	case params::LOCAL_MODULATOR_0_FEEDBACK:
	case params::LOCAL_MODULATOR_1_FEEDBACK:
	case params::LOCAL_CARRIER_0_FEEDBACK:
	case params::LOCAL_CARRIER_1_FEEDBACK:
		return 5931642;

	case params::GLOBAL_DELAY_RATE:
	case params::GLOBAL_ARP_RATE:
	case params::LOCAL_PITCH_ADJUST:
	case params::LOCAL_OSC_A_PITCH_ADJUST:
	case params::LOCAL_OSC_B_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
		return kMaxSampleValue; // Means we have space to 8x (3-octave-shift) the pitch if we want... (wait, I've since
		                        // made it 16x smaller)

	case params::GLOBAL_MOD_FX_DEPTH:
		return 526133494; // 2% lower than 536870912

	default:
		return 0;
	}
}

void functionsInit() {

	for (int32_t p = 0; p < kNumParams; p++) {
		paramRanges[p] = getParamRange(p);
	}

	for (int32_t p = 0; p < kNumParams; p++) {
		paramNeutralValues[p] = getParamNeutralValue(p);
	}
}

int32_t getFinalParameterValueHybrid(int32_t paramNeutralValue, int32_t patchedValue) {
	// Allows for max output values of +- 1073741824, which the panning code understands as the full range from left to
	// right
	int32_t preLimits = (paramNeutralValue >> 2) + (patchedValue >> 1);
	return signed_saturate<32 - 3>(preLimits) << 2;
}

int32_t getFinalParameterValueVolume(int32_t paramNeutralValue, int32_t patchedValue) {

	// patchedValue's range is ideally +- 536870912, but may get up to 1610612736 due to multiple patch cables having
	// been multiplied

	// No need for max/min here - it's already been taken care of in patchAllCablesToParameter(),
	// ... or if we got here from patchSourceToAllExclusiveCables(), there's no way it could have been too big
	int32_t positivePatchedValue = patchedValue + 536870912;

	// positivePatchedValue's range is ideally 0 ("0") to 1073741824 ("2"), but potentially up to 2147483647 ("4").
	// 536870912 represents "1".

	// If this parameter is a volume one, then apply a parabola curve to the patched value, at this late stage
	/*
	if (isVolumeParam) {
	    // But, our output value can't get bigger than 2147483647 ("4"), which means we have to clip our input off at
	1073741824 ("2") if (positivePatchedValue >= 1073741824) positivePatchedValue = 2147483647; else {
	        //int32_t madeSmaller = positivePatchedValue >> 15;
	        //positivePatchedValue = (madeSmaller * madeSmaller) << 1;
	        positivePatchedValue = (positivePatchedValue >> 15) * (positivePatchedValue >> 14);
	    }
	}
	*/

	// This is a temporary (?) fix I've done to allow FM modulator amounts to get past where I clipped off volume
	// params. So now volumes can get higher too. Problem? Not sure.

	// But, our output value can't get bigger than 2147483647 ("4"), which means we have to clip our input off at
	// 1073741824 ("2")
	positivePatchedValue = (positivePatchedValue >> 16) * (positivePatchedValue >> 15);

	// return multiply_32x32_rshift32(positivePatchedValue, paramNeutralValue) << 5;

	// Must saturate, otherwise mod fx depth can easily overflow
	return lshiftAndSaturate<5>(multiply_32x32_rshift32(positivePatchedValue, paramNeutralValue));
}

int32_t getFinalParameterValueLinear(int32_t paramNeutralValue, int32_t patchedValue) {

	// patchedValue's range is ideally +- 536870912, but may get up to 1610612736 due to multiple patch cables having
	// been multiplied

	// No need for max/min here - it's already been taken care of in patchAllCablesToParameter(),
	// ... or if we got here from patchSourceToAllExclusiveCables(), there's no way it could have been too big
	int32_t positivePatchedValue = patchedValue + 536870912;

	// positivePatchedValue's range is ideally 0 ("0") to 1073741824 ("2"), but potentially up to 2147483647 ("4").
	// 536870912 represents "1".

	// Must saturate, otherwise sustain level can easily overflow
	return lshiftAndSaturate<3>(multiply_32x32_rshift32(positivePatchedValue, paramNeutralValue));
}

int32_t getFinalParameterValueExp(int32_t paramNeutralValue, int32_t patchedValue) {
	return getExp(paramNeutralValue, patchedValue);
}

int32_t getFinalParameterValueExpWithDumbEnvelopeHack(int32_t paramNeutralValue, int32_t patchedValue, int32_t p) {
	// TODO: this is horribly hard-coded, but works for now
	if (p >= params::LOCAL_ENV_0_DECAY && p <= params::LOCAL_ENV_1_RELEASE) {
		return multiply_32x32_rshift32(paramNeutralValue, lookupReleaseRate(patchedValue));
	}
	if (p == params::LOCAL_ENV_0_ATTACK || p == params::LOCAL_ENV_1_ATTACK) {
		patchedValue = -patchedValue;
	}

	return getFinalParameterValueExp(paramNeutralValue, patchedValue);
}

void addAudio(StereoSample* inputBuffer, StereoSample* outputBuffer, int32_t numSamples) {
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

char const* sourceToString(PatchSource source) {
	switch (source) {
	case PatchSource::LFO_GLOBAL:
		return "lfo1";

	case PatchSource::LFO_LOCAL:
		return "lfo2";

	case PatchSource::ENVELOPE_0:
		return "envelope1";

	case PatchSource::ENVELOPE_1:
		return "envelope2";

	case PatchSource::VELOCITY:
		return "velocity";

	case PatchSource::NOTE:
		return "note";

	case PatchSource::SIDECHAIN:
		return "compressor";

	case PatchSource::RANDOM:
		return "random";

	case PatchSource::AFTERTOUCH:
		return "aftertouch";

	case PatchSource::X:
		return "x";

	case PatchSource::Y:
		return "y";

	default:
		return "none";
	}
}

char const* getSourceDisplayNameForOLED(PatchSource s) {
	using enum l10n::String;
	auto lang = l10n::chosenLanguage;

	switch (s) {
	case PatchSource::LFO_GLOBAL:
		return l10n::get(STRING_FOR_PATCH_SOURCE_LFO_GLOBAL);

	case PatchSource::LFO_LOCAL:
		return l10n::get(STRING_FOR_PATCH_SOURCE_LFO_LOCAL);

	case PatchSource::ENVELOPE_0:
		return l10n::get(STRING_FOR_PATCH_SOURCE_ENVELOPE_0);

	case PatchSource::ENVELOPE_1:
		return l10n::get(STRING_FOR_PATCH_SOURCE_ENVELOPE_1);

	case PatchSource::VELOCITY:
		return l10n::get(STRING_FOR_PATCH_SOURCE_VELOCITY);

	case PatchSource::NOTE:
		return l10n::get(STRING_FOR_PATCH_SOURCE_NOTE);

	case PatchSource::SIDECHAIN:
		return l10n::get(STRING_FOR_PATCH_SOURCE_SIDECHAIN);

	case PatchSource::RANDOM:
		return l10n::get(STRING_FOR_PATCH_SOURCE_RANDOM);

	case PatchSource::AFTERTOUCH:
		return l10n::get(STRING_FOR_PATCH_SOURCE_AFTERTOUCH);

	case PatchSource::X:
		return l10n::get(STRING_FOR_PATCH_SOURCE_X);

	case PatchSource::Y:
		return l10n::get(STRING_FOR_PATCH_SOURCE_Y);

	default:
		return "none";
	}
}

PatchSource stringToSource(char const* string) {
	for (int32_t s = 0; s < kNumPatchSources; s++) {
		auto patchSource = static_cast<PatchSource>(s);
		if (!strcmp(string, sourceToString(patchSource))) {
			return patchSource;
		}
	}
	return PatchSource::NONE;
}

// all should be four chars, to fit a fixed column layout
char const* sourceToStringShort(PatchSource source) {
	switch (source) {
	case PatchSource::LFO_GLOBAL:
		return "lfo1";

	case PatchSource::LFO_LOCAL:
		return "lfo2";

	case PatchSource::ENVELOPE_0:
		return "env1";

	case PatchSource::ENVELOPE_1:
		return "env2";

	case PatchSource::VELOCITY:
		return "velo";

	case PatchSource::NOTE:
		return "note";

	case PatchSource::SIDECHAIN:
		return "comp";

	case PatchSource::RANDOM:
		return "rand";

	case PatchSource::AFTERTOUCH:
		return "pres";

	case PatchSource::X:
		return "mpeX";

	case PatchSource::Y:
		return "mpeY";

	default:
		return "----";
	}
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

		int32_t howFarUpInterval = oldValuePositive & 268435455;
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

int32_t interpolateTable(uint32_t input, int32_t numBitsInInput, const uint16_t* table, int32_t numBitsInTableSize) {
	int32_t whichValue = input >> (numBitsInInput - numBitsInTableSize);
	int32_t value1 = table[whichValue];
	int32_t value2 = table[whichValue + 1];

	int32_t rshiftAmount = numBitsInInput - 15 - numBitsInTableSize;
	uint32_t rshifted;
	if (rshiftAmount >= 0) {
		rshifted = input >> rshiftAmount;
	}
	else {
		rshifted = input << (-rshiftAmount);
	}

	int32_t strength2 = rshifted & 32767;
	int32_t strength1 = 32768 - strength2;
	return value1 * strength1 + value2 * strength2;
}

uint32_t interpolateTableInverse(int32_t tableValueBig, int32_t numBitsInLookupOutput, const uint16_t* table,
                                 int32_t numBitsInTableSize) {
	int32_t tableValue = tableValueBig >> 15;
	int32_t tableSize = 1 << numBitsInTableSize;

	int32_t tableDirection = (table[0] < table[tableSize]) ? 1 : -1;

	// Check we're not off either end of the table
	if ((tableValue - table[0]) * tableDirection <= 0) {
		return 0;
	}
	if ((tableValue - table[tableSize]) * tableDirection >= 0) {
		return (1 << numBitsInLookupOutput) - 1;
	}

	int32_t rangeStart = 0;
	int32_t rangeEnd = tableSize;

	while (rangeStart + 1 < rangeEnd) {

		int32_t examinePos = (rangeStart + rangeEnd) >> 1;
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

bool memIsNumericChars(char const* mem, int32_t size) {
	for (int32_t i = 0; i < size; i++) {
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

char const* getThingName(OutputType outputType) {
	if (outputType == OutputType::SYNTH) {
		return "SYNT";
	}
	else if (outputType == OutputType::KIT) {
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

char const* oscTypeToString(OscType oscType) {
	switch (oscType) {
	case OscType::SQUARE:
		return "square";

	case OscType::SAW:
		return "saw";

	case OscType::ANALOG_SAW_2:
		return "analogSaw";

	case OscType::ANALOG_SQUARE:
		return "analogSquare";

	case OscType::SINE:
		return "sine";

	case OscType::TRIANGLE:
		return "triangle";

	case OscType::SAMPLE:
		return "sample";

	case OscType::WAVETABLE:
		return "wavetable";

	case OscType::INPUT_L:
		return "inLeft";

	case OscType::INPUT_R:
		return "inRight";

	case OscType::INPUT_STEREO:
		return "inStereo";

	case OscType::DX7:
		return "dx7";

	default:
		__builtin_unreachable();
	}
}

OscType stringToOscType(char const* string) {

	if (!strcmp(string, "square")) {
		return OscType::SQUARE;
	}
	else if (!strcmp(string, "analogSquare")) {
		return OscType::ANALOG_SQUARE;
	}
	else if (!strcmp(string, "analogSaw")) {
		return OscType::ANALOG_SAW_2;
	}
	else if (!strcmp(string, "saw")) {
		return OscType::SAW;
	}
	else if (!strcmp(string, "sine")) {
		return OscType::SINE;
	}
	else if (!strcmp(string, "sample")) {
		return OscType::SAMPLE;
	}
	else if (!strcmp(string, "wavetable")) {
		return OscType::WAVETABLE;
	}
	else if (!strcmp(string, "inLeft")) {
		return OscType::INPUT_L;
	}
	else if (!strcmp(string, "inRight")) {
		return OscType::INPUT_R;
	}
	else if (!strcmp(string, "inStereo")) {
		return OscType::INPUT_STEREO;
	}
	else if (!strcmp(string, "dx7")) {
		return OscType::DX7;
	}
	else {
		return OscType::TRIANGLE;
	}
}

char const* lfoTypeToString(LFOType oscType) {
	switch (oscType) {
	case LFOType::SQUARE:
		return "square";

	case LFOType::SAW:
		return "saw";

	case LFOType::SINE:
		return "sine";

	case LFOType::SAMPLE_AND_HOLD:
		return "sah";

	case LFOType::RANDOM_WALK:
		return "rwalk";

	default:
		return "triangle";
	}
}

LFOType stringToLFOType(char const* string) {
	if (!strcmp(string, "square")) {
		return LFOType::SQUARE;
	}
	else if (!strcmp(string, "saw")) {
		return LFOType::SAW;
	}
	else if (!strcmp(string, "sine")) {
		return LFOType::SINE;
	}
	else if (!strcmp(string, "sah")) {
		return LFOType::SAMPLE_AND_HOLD;
	}
	else if (!strcmp(string, "rwalk")) {
		return LFOType::RANDOM_WALK;
	}
	else {
		return LFOType::TRIANGLE;
	}
}

char const* synthModeToString(SynthMode synthMode) {
	switch (synthMode) {
	case SynthMode::FM:
		return "fm";

	case SynthMode::RINGMOD:
		return "ringmod";

	default:
		return "subtractive";
	}
}

SynthMode stringToSynthMode(char const* string) {
	if (!strcmp(string, "fm")) {
		return SynthMode::FM;
	}
	else if (!strcmp(string, "ringmod")) {
		return SynthMode::RINGMOD;
	}
	else {
		return SynthMode::SUBTRACTIVE;
	}
}

char const* polyphonyModeToString(PolyphonyMode synthMode) {
	switch (synthMode) {
	case PolyphonyMode::MONO:
		return "mono";

	case PolyphonyMode::AUTO:
		return "auto";

	case PolyphonyMode::LEGATO:
		return "legato";

	case PolyphonyMode::CHOKE:
		return "choke";

	default: // case PolyphonyMode::POLY:
		return "poly";
	}
}

PolyphonyMode stringToPolyphonyMode(char const* string) {
	if (!strcmp(string, "mono")) {
		return PolyphonyMode::MONO;
	}
	else if (!strcmp(string, "auto")) {
		return PolyphonyMode::AUTO;
	}
	else if (!strcmp(string, "0")) {
		return PolyphonyMode::AUTO; // Old firmware, pre June 2017
	}
	else if (!strcmp(string, "legato")) {
		return PolyphonyMode::LEGATO;
	}
	else if (!strcmp(string, "choke")) {
		return PolyphonyMode::CHOKE;
	}
	else if (!strcmp(string, "2")) {
		return PolyphonyMode::CHOKE; // Old firmware, pre June 2017
	}
	else {
		return PolyphonyMode::POLY;
	}
}

char const* fxTypeToString(ModFXType fxType) {
	switch (fxType) {
	case ModFXType::FLANGER:
		return "flanger";

	case ModFXType::CHORUS:
		return "chorus";

	case ModFXType::CHORUS_STEREO:
		return "StereoChorus";
	case ModFXType::GRAIN:
		return "grainFX";

	case ModFXType::PHASER:
		return "phaser";

	default:
		return "none";
	}
}

ModFXType stringToFXType(char const* string) {
	if (!strcmp(string, "flanger")) {
		return ModFXType::FLANGER;
	}
	else if (!strcmp(string, "chorus")) {
		return ModFXType::CHORUS;
	}
	else if (!strcmp(string, "StereoChorus")) {
		return ModFXType::CHORUS_STEREO;
	}
	else if (!strcmp(string, "grainFX")) {
		return ModFXType::GRAIN;
	}
	else if (!strcmp(string, "phaser")) {
		return ModFXType::PHASER;
	}
	else {
		return ModFXType::NONE;
	}
}

char const* modFXParamToString(ModFXParam fxType) {
	switch (fxType) {
	case ModFXParam::DEPTH:
		return "depth";

	case ModFXParam::FEEDBACK:
		return "feedback";

	default:
		return "offset";
	}
}

ModFXParam stringToModFXParam(char const* string) {
	if (!strcmp(string, "depth")) {
		return ModFXParam::DEPTH;
	}
	else if (!strcmp(string, "feedback")) {
		return ModFXParam::FEEDBACK;
	}
	else {
		return ModFXParam::OFFSET;
	}
}

char const* filterTypeToString(FilterType fxType) {
	switch (fxType) {
	case FilterType::HPF:
		return "hpf";

	case FilterType::EQ:
		return "eq";

	default:
		return "lpf";
	}
}

FilterType stringToFilterType(char const* string) {
	if (!strcmp(string, "hpf")) {
		return FilterType::HPF;
	}
	else if (!strcmp(string, "eq")) {
		return FilterType::EQ;
	}
	else {
		return FilterType::LPF;
	}
}

ArpMode oldModeToArpMode(OldArpMode oldMode) {
	if (oldMode != OldArpMode::OFF) {
		return ArpMode::ARP;
	}
	else {
		return ArpMode::OFF;
	}
}

ArpNoteMode oldModeToArpNoteMode(OldArpMode oldMode) {
	switch (oldMode) {
	case OldArpMode::DOWN:
		return ArpNoteMode::DOWN;
	case OldArpMode::RANDOM:
		return ArpNoteMode::RANDOM;
	default:
		return ArpNoteMode::UP;
	}
}

ArpOctaveMode oldModeToArpOctaveMode(OldArpMode oldMode) {
	switch (oldMode) {
	case OldArpMode::DOWN:
		return ArpOctaveMode::DOWN;
	case OldArpMode::BOTH:
		return ArpOctaveMode::ALTERNATE;
	case OldArpMode::RANDOM:
		return ArpOctaveMode::RANDOM;
	default:
		return ArpOctaveMode::UP;
	}
}

char const* oldArpModeToString(OldArpMode mode) {
	switch (mode) {
	case OldArpMode::UP:
		return "up";

	case OldArpMode::DOWN:
		return "down";

	case OldArpMode::BOTH:
		return "both";

	case OldArpMode::RANDOM:
		return "random";

	default:
		return "off";
	}
}

OldArpMode stringToOldArpMode(char const* string) {
	if (!strcmp(string, "up")) {
		return OldArpMode::UP;
	}
	else if (!strcmp(string, "down")) {
		return OldArpMode::DOWN;
	}
	else if (!strcmp(string, "both")) {
		return OldArpMode::BOTH;
	}
	else if (!strcmp(string, "random")) {
		return OldArpMode::RANDOM;
	}
	else {
		return OldArpMode::OFF;
	}
}

char const* arpModeToString(ArpMode mode) {
	switch (mode) {
	case ArpMode::ARP:
		return "arp";

	default:
		return "off";
	}
}

ArpMode stringToArpMode(char const* string) {
	if (!strcmp(string, "arp")) {
		return ArpMode::ARP;
	}
	else {
		return ArpMode::OFF;
	}
}

char const* arpNoteModeToString(ArpNoteMode mode) {
	switch (mode) {
	case ArpNoteMode::DOWN:
		return "down";

	case ArpNoteMode::UP_DOWN:
		return "upDown";

	case ArpNoteMode::AS_PLAYED:
		return "asPlayed";

	case ArpNoteMode::RANDOM:
		return "random";

	default:
		return "up";
	}
}

ArpNoteMode stringToArpNoteMode(char const* string) {
	if (!strcmp(string, "down")) {
		return ArpNoteMode::DOWN;
	}
	else if (!strcmp(string, "upDown")) {
		return ArpNoteMode::UP_DOWN;
	}
	else if (!strcmp(string, "asPlayed")) {
		return ArpNoteMode::AS_PLAYED;
	}
	else if (!strcmp(string, "random")) {
		return ArpNoteMode::RANDOM;
	}
	else {
		return ArpNoteMode::UP;
	}
}

char const* arpOctaveModeToString(ArpOctaveMode mode) {
	switch (mode) {
	case ArpOctaveMode::DOWN:
		return "down";

	case ArpOctaveMode::RANDOM:
		return "random";

	default:
		return "up";
	}
}

ArpOctaveMode stringToArpOctaveMode(char const* string) {
	if (!strcmp(string, "down")) {
		return ArpOctaveMode::DOWN;
	}
	else if (!strcmp(string, "upDown")) {
		return ArpOctaveMode::RANDOM;
	}
	else if (!strcmp(string, "alt")) {
		return ArpOctaveMode::ALTERNATE;
	}
	else if (!strcmp(string, "random")) {
		return ArpOctaveMode::RANDOM;
	}
	else {
		return ArpOctaveMode::UP;
	}
}

char const* arpMpeModSourceToString(ArpMpeModSource modSource) {
	switch (modSource) {
	case ArpMpeModSource::MPE_Y:
		return "y";

	case ArpMpeModSource::AFTERTOUCH:
		return "z";

	default:
		return "off";
	}
}

ArpMpeModSource stringToArpMpeModSource(char const* string) {
	if (!strcmp(string, "y")) {
		return ArpMpeModSource::MPE_Y;
	}
	else if (!strcmp(string, "z")) {
		return ArpMpeModSource::AFTERTOUCH;
	}
	else {
		return ArpMpeModSource::OFF;
	}
}

char const* inputChannelToString(AudioInputChannel inputChannel) {
	switch (inputChannel) {
	case AudioInputChannel::LEFT:
		return "left";

	case AudioInputChannel::RIGHT:
		return "right";

	case AudioInputChannel::STEREO:
		return "stereo";

	case AudioInputChannel::BALANCED:
		return "balanced";

	case AudioInputChannel::MIX:
		return "mix";

	case AudioInputChannel::OUTPUT:
		return "output";

	default: // AudioInputChannel::NONE
		return "none";
	}
}

AudioInputChannel stringToInputChannel(char const* string) {
	if (!strcmp(string, "left")) {
		return AudioInputChannel::LEFT;
	}
	else if (!strcmp(string, "right")) {
		return AudioInputChannel::RIGHT;
	}
	else if (!strcmp(string, "stereo")) {
		return AudioInputChannel::STEREO;
	}
	else if (!strcmp(string, "balanced")) {
		return AudioInputChannel::BALANCED;
	}
	else if (!strcmp(string, "mix")) {
		return AudioInputChannel::MIX;
	}
	else if (!strcmp(string, "output")) {
		return AudioInputChannel::OUTPUT;
	}
	else {
		return AudioInputChannel::NONE;
	}
}

char const* sequenceDirectionModeToString(SequenceDirection sequenceDirectionMode) {
	switch (sequenceDirectionMode) {
	case SequenceDirection::FORWARD:
		return "forward";

	case SequenceDirection::REVERSE:
		return "reverse";

	case SequenceDirection::PINGPONG:
		return "pingpong";

	case SequenceDirection::OBEY_PARENT:
		return "none";

	default:
		__builtin_unreachable();
		return "";
	}
}

SequenceDirection stringToSequenceDirectionMode(char const* string) {
	if (!strcmp(string, "reverse")) {
		return SequenceDirection::REVERSE;
	}
	else if (!strcmp(string, "pingpong")) {
		return SequenceDirection::PINGPONG;
	}
	else if (!strcmp(string, "obeyParent")) {
		return SequenceDirection::OBEY_PARENT;
	}
	else {
		return SequenceDirection::FORWARD;
	}
}

char const* launchStyleToString(LaunchStyle launchStyle) {
	switch (launchStyle) {
	case LaunchStyle::DEFAULT:
		return "default";

	case LaunchStyle::FILL:
		return "fill";

	case LaunchStyle::ONCE:
		return "once";

	default:
		__builtin_unreachable();
		return "";
	}
}

LaunchStyle stringToLaunchStyle(char const* string) {
	if (!strcmp(string, "fill")) {
		return LaunchStyle::FILL;
	}
	else if (!strcmp(string, "once")) {
		return LaunchStyle::ONCE;
	}
	else {
		return LaunchStyle::DEFAULT;
	}
}

char const* getInstrumentFolder(OutputType outputType) {
	if (outputType == OutputType::SYNTH) {
		return "SYNTHS";
	}
	else if (outputType == OutputType::KIT) {
		return "KITS";
	}
	else if (outputType == OutputType::MIDI_OUT) {
		return "MIDI";
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
	case params::STATIC_SIDECHAIN_ATTACK:
		return attackRateTable[userValue] * 4;

	case params::STATIC_SIDECHAIN_RELEASE:
		return releaseRateTable[userValue] * 8;

	case params::LOCAL_OSC_A_PHASE_WIDTH:
	case params::LOCAL_OSC_B_PHASE_WIDTH:
		return (uint32_t)userValue * (85899345 >> 1);

	case params::PATCH_CABLE:
	case params::STATIC_SIDECHAIN_VOLUME:
		return userValue * 21474836;

	case params::UNPATCHED_START + params::UNPATCHED_BASS:
	case params::UNPATCHED_START + params::UNPATCHED_TREBLE:
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

int32_t getLookupIndexFromValue(int32_t value, const int32_t* table, int32_t maxIndex) {
	int32_t i;
	uint32_t bestDistance = 0xFFFFFFFF;
	int32_t closestIndex;
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
	// Ideally, we'd do pythagoras on these. But to save computation time, we'll just go half way between the biggest
	// one and the sum
	uint32_t sum = (uint32_t)strength1 + (uint32_t)strength2;
	sum = std::min(sum, (uint32_t)2147483647);
	int32_t maxOne = std::max(strength1, strength2);
	return (maxOne >> 1) + (sum >> 1);
}

uint32_t z = 362436069, w = 521288629, jcong = 380116160;

int32_t random(int32_t upperLimit) {
	return (uint16_t)(CONG >> 16) % (upperLimit + 1);
}

bool shouldDoPanning(int32_t panAmount, int32_t* amplitudeL, int32_t* amplitudeR) {
	if (panAmount == 0) {
		*amplitudeR = 1073741823;
		*amplitudeL = 1073741823;
		return false;
	}

	int32_t panOffset = std::max((int32_t)-1073741824, (int32_t)(std::min((int32_t)1073741824, (int32_t)panAmount)));
	*amplitudeR = (panAmount >= 0) ? 1073741823 : (1073741824 + panOffset);
	*amplitudeL = (panAmount <= 0) ? 1073741823 : (1073741824 - panOffset);
	return true;
}

uint32_t getLFOInitialPhaseForNegativeExtreme(LFOType waveType) {
	switch (waveType) {
	case LFOType::SAW:
		return 2147483648u;

	case LFOType::SINE:
		return 3221225472u;

	default:
		return 0;
	}
}

uint32_t getLFOInitialPhaseForZero(LFOType waveType) {
	switch (waveType) {
	case LFOType::TRIANGLE:
		return 1073741824;

	default:
		return 0;
	}
}

uint32_t getOscInitialPhaseForZero(OscType waveType) {
	switch (waveType) {
	case OscType::TRIANGLE:
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

int32_t doLanczos(int32_t* data, int32_t pos, uint32_t posWithinPos, int32_t memoryNumElements) {

	int32_t strengthL[LANCZOS_A];
	int32_t strengthR[LANCZOS_A];

	for (int32_t i = 0; i < LANCZOS_A; i++) {
		strengthL[i] = interpolateTableSigned(kMaxSampleValue * i + posWithinPos, 26, lanczosKernel, 8);
		strengthR[i] = interpolateTableSigned(kMaxSampleValue * (i + 1) - posWithinPos, 26, lanczosKernel, 8);
	}

	int32_t howManyLeft = std::min((int32_t)LANCZOS_A, (int32_t)(pos + 1));
	int32_t howManyRight = std::min((int32_t)LANCZOS_A, (int32_t)(memoryNumElements - pos));

	int32_t value = 0;
	for (int32_t i = 0; i < howManyLeft; i++) {
		value += multiply_32x32_rshift32_rounded(strengthL[i], data[pos - i]);
	}
	for (int32_t i = 0; i < howManyRight; i++) {
		value += multiply_32x32_rshift32_rounded(strengthR[i], data[pos + 1 + i]);
	}

	// In a "perfect" world we'd <<1 after this, but no real need since loudness is going to get normalized anyway, and
	// actually we'd probably get some overflows if we did...

	return value;
}

int32_t doLanczosCircular(int32_t* data, int32_t pos, uint32_t posWithinPos, int32_t memoryNumElements) {

	int32_t strengthL[LANCZOS_A];
	int32_t strengthR[LANCZOS_A];

	for (int32_t i = 0; i < LANCZOS_A; i++) {
		strengthL[i] = interpolateTableSigned(kMaxSampleValue * i + posWithinPos, 26, lanczosKernel, 8);
		strengthR[i] = interpolateTableSigned(kMaxSampleValue * (i + 1) - posWithinPos, 26, lanczosKernel, 8);
	}

	int32_t value = 0;
	for (int32_t i = 0; i < LANCZOS_A; i++) {
		value += multiply_32x32_rshift32_rounded(strengthL[i],
		                                         data[(pos - i + memoryNumElements) & (memoryNumElements - 1)]);
	}
	for (int32_t i = 0; i < LANCZOS_A; i++) {
		value += multiply_32x32_rshift32_rounded(strengthR[i], data[(pos + 1 + i) & (memoryNumElements - 1)]);
	}

	// In a "perfect" world we'd <<1 after this, but no real need since loudness is going to get normalized anyway, and
	// actually we'd probably get some overflows if we did...

	return value;
}

struct ComparativeNoteNumber {
	int32_t noteNumber;
	int32_t stringLength;
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

	if (*string < '1' || *string > '9') { // There has to be at least some number there if we're to consider this a note
		                                  // name. And it can't start with 0.
		toReturn.noteNumber = 100000;
		toReturn.stringLength = 0;
		return toReturn;
	}

	int32_t number = *string - '0';
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
int32_t strcmpspecial(char const* first, char const* second) {

	int32_t resultIfGetToEndOfBothStrings = 0;

	while (true) {
		bool firstIsFinished = (*first == 0);
		bool secondIsFinished = (*second == 0);

		if (firstIsFinished && secondIsFinished) {
			return resultIfGetToEndOfBothStrings; // If both are finished
		}

		if (firstIsFinished || secondIsFinished) { // If just one is finished
			return (int32_t)*first - (int32_t)*second;
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

					// else if (!firstDigitIsLeadingZero || !secondDigitIsLeadingZero) break;	// If both are not
					// zeros, we're done.
					//  Actually, the same end result is achieved without that line.

					// If we're still here, one is a leading zero and the other isn't.
					resultIfGetToEndOfBothStrings =
					    (int32_t)firstChar
					    - (int32_t)secondChar; // Will still end up as zero when that needs to happen.
					break;
				}
			}

			int32_t firstNumber = *first - '0';
			int32_t secondNumber = *second - '0';
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

			int32_t difference = firstNumber - secondNumber;
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

					return (int32_t)firstChar - (int32_t)secondChar;
				}
			}
		}
	}
}

char* replace_char(const char* str, char find, char replace) {
	char* copy = new char[sizeof(char) * strlen(str) + 1];

	strcpy(copy, str);
	char* current_pos = strchr(copy, find);
	while (current_pos) {
		*current_pos = replace;
		current_pos = strchr(current_pos, find);
	}
	return copy;
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

int32_t memcasecmp(char const* first, char const* second, int32_t size) {
	for (int32_t i = 0; i < size; i++) {
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

int32_t howMuchMoreMagnitude(uint32_t to, uint32_t from) {
	return getMagnitudeOld(to) - getMagnitudeOld(from);
}

void noteCodeToString(int32_t noteCode, char* buffer, int32_t* getLengthWithoutDot) {
	char* thisChar = buffer;
	int32_t octave = (noteCode) / 12 - 2;
	int32_t noteCodeWithinOctave = (uint16_t)(noteCode + 120) % (uint8_t)12;

	*thisChar = noteCodeToNoteLetter[noteCodeWithinOctave];
	thisChar++;
	if (noteCodeIsSharp[noteCodeWithinOctave]) {
		*thisChar = display->haveOLED() ? '#' : '.';
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

void concatenateLines(const char* lines[], size_t numLines, char* resultString) {
	const size_t maxCharsPerLine = 19;
	size_t resultIndex = 0;

	for (size_t i = 0; i < numLines; ++i) {
		const char* currentLine = lines[i];
		size_t lineLength = std::strlen(currentLine);

		// Copy the current line to the result string
		std::strncpy(&resultString[resultIndex], currentLine, lineLength);
		resultIndex += lineLength;

		// Add a line break after each line except the last one
		if (i < numLines - 1) {
			resultString[resultIndex] = '\n';
			resultIndex++;
		}
	}

	// Null-terminate the result string
	resultString[resultIndex] = '\0';
}

void seedRandom() {
	jcong = *TCNT[TIMER_SYSTEM_FAST];
}

#define UnsignedToFloat(u) (((double)((long)(u - 2147483647L - 1))) + 2147483648.0)

double ConvertFromIeeeExtended(unsigned char* bytes /* LCN */) {
	double f;
	int32_t expon;
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

int32_t getWhichKernel(int32_t phaseIncrement) {
	if (phaseIncrement < 17268826) {
		return 0; // That allows us to go half a semitone up
	}
	else {
		int32_t whichKernel = 1;
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

void dissectIterationDependence(int32_t probability, int32_t* getDivisor, int32_t* getWhichIterationWithinDivisor) {
	int32_t value = (probability & 127) - kNumProbabilityValues - 1;
	int32_t whichRepeat;

	int32_t tryingWhichDivisor;

	for (tryingWhichDivisor = 2; tryingWhichDivisor <= 8; tryingWhichDivisor++) {
		if (value < tryingWhichDivisor) {
			*getWhichIterationWithinDivisor = value;
			break;
		}

		value -= tryingWhichDivisor;
	}

	*getDivisor = tryingWhichDivisor;
}

int32_t encodeIterationDependence(int32_t divisor, int32_t iterationWithinDivisor) {
	int32_t value = iterationWithinDivisor;
	for (int32_t i = 2; i < divisor; i++) {
		value += i;
	}
	return value + 1 + kNumProbabilityValues;
}

int32_t getHowManyCharsAreTheSame(char const* a, char const* b) {
	int32_t count = 0;
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

bool shouldAbortLoading() {
	return (currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED
	        && (encoders::getEncoder(encoders::EncoderName::SELECT).detentPos || QwertyUI::predictionInterrupted));
}

void getNoteLengthNameFromMagnitude(StringBuf& noteLengthBuf, int32_t magnitude, char const* const notesString,
                                    bool clarifyPerColumn) {
	uint32_t division = (uint32_t)1 << (0 - magnitude);

	if (display->haveOLED()) {
		if (magnitude < 0) {
			noteLengthBuf.appendInt(division);
			// this is not fully general but since division are always a power of 2, it works out in practice (no need
			// for "rd")
			char const* suffix = ((division % 10) == 2) ? "nd" : "th";
			noteLengthBuf.append(suffix);
			noteLengthBuf.append(notesString);
		}
		else {
			uint32_t numBars = (uint32_t)1 << magnitude;
			noteLengthBuf.appendInt(numBars);
			if (clarifyPerColumn) {
				if (numBars == 1) {
					noteLengthBuf.append(" bar (per column)");
				}
				else {
					noteLengthBuf.append(" bars (per column)");
				}
			}
			else {
				noteLengthBuf.append("-bar");
			}
		}
	}
	else {
		if (magnitude < 0) {
			if (division <= 9999) {
				noteLengthBuf.appendInt(division);
				if (division == 2 || division == 32) {
					noteLengthBuf.append("ND");
				}
				else if (division <= 99) {
					noteLengthBuf.append("TH");
				}
				else if (division <= 999) {
					noteLengthBuf.append("T");
				}
			}
			else {
				noteLengthBuf.append("TINY");
			}
		}
		else {
			uint32_t numBars = (uint32_t)1 << magnitude;
			if (numBars <= 9999) {
				noteLengthBuf.appendInt(numBars);
				auto size = noteLengthBuf.size();
				if (size == 1) {
					noteLengthBuf.append("BAR");
				}
				else if (size <= 3) {
					noteLengthBuf.append("B");
				}
			}
			else {
				noteLengthBuf.append("BIG");
			}
		}
	}
}

char const* getFileNameFromEndOfPath(char const* filePathChars) {
	char const* slashPos = strrchr(filePathChars, '/');
	return slashPos ? (slashPos + 1) : filePathChars;
}

bool doesFilenameFitPrefixFormat(char const* fileName, char const* filePrefix, int32_t prefixLength) {

	if (memcasecmp(fileName, filePrefix, prefixLength)) {
		return false;
	}

	char* dotAddress = strrchr(fileName, '.');
	if (!dotAddress) {
		return false;
	}

	int32_t dotPos = (uint32_t)dotAddress - (uint32_t)fileName;
	if (dotPos < prefixLength + 3) {
		return false;
	}

	if (!memIsNumericChars(&fileName[prefixLength], 3)) {
		return false;
	}

	return true;
}

Error fresultToDelugeErrorCode(FRESULT result) {
	switch (result) {
	case FR_OK:
		return Error::NONE;

	case FR_NO_FILESYSTEM:
		return Error::SD_CARD_NO_FILESYSTEM;

	case FR_NO_FILE:
		return Error::FILE_NOT_FOUND;

	case FR_NO_PATH:
		return Error::FOLDER_DOESNT_EXIST;

	case FR_WRITE_PROTECTED:
		return Error::WRITE_PROTECTED;

	case FR_NOT_ENOUGH_CORE:
		return Error::INSUFFICIENT_RAM;

	case FR_EXIST:
		return Error::FILE_ALREADY_EXISTS;

	default:
		return Error::SD_CARD;
	}
}

char miscStringBuffer[kFilenameBufferSize] __attribute__((aligned(CACHE_LINE_SIZE)));
char shortStringBuffer[kShortStringBufferSize] __attribute__((aligned(CACHE_LINE_SIZE)));
