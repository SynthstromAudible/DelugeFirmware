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

#pragma once

#include "const_functions.h"
#include "definitions_cxx.hpp"
#include "fatfs/ff.h"
#include "gui/colour/colour.h"         // IWYU pragma: export todo: this probably shouldn't be exported from here
#include "util/audio_format_helpers.h" // byte/format helpers (charsToIntegerConstant, swapEndianness*, …) re-exported
#include "util/c_string.h"
#include "util/cfunctions.h" // IWYU pragma: export - minimal set of functions which need c linkage
#include "util/etl_string.h"
#include "util/fixedpoint.h"
#include "util/lookuptables/lookuptables.h"
#include "util/param_functions.h" // IWYU pragma: export — param scaling/patching math (split out for the synth core)
#include "util/waves.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

class UI;

extern UI* getCurrentUI();

extern const uint8_t modButtonX[];
extern const uint8_t modButtonY[];

extern uint8_t subModeToReturnTo;

void functionsInit();

char const* getThingName(OutputType outputType);
char const* getOutputTypeName(OutputType outputType, int32_t channel);

// signed_saturate_operand_unknown / lshiftAndSaturate / lshiftAndSaturateUnknown now live in
// util/param_functions.h (included above), split out so the synth core needn't pull this heavy header.

// charsToIntegerConstant / swapEndianness* / memToUIntOrError / ConvertFromIeeeExtended now live in
// util/audio_format_helpers.h (included above), so the parser can depend on them without this heavy header.

/**
 * replace single asterix with a single digit
 * single asterix works for single digits only
 * replace double asterix with a double digit
 * double asterix works with single and double digits only
 */
[[gnu::always_inline]] constexpr void asterixToInt(char* str, int32_t i) {
	while (*str != 0) {
		// if double asterix (two digits)
		if (*str == '*' && *(str + 1) == '*') {
			intToString(i, str, 2);
			break;
		}
		// if single asterix (single digit)
		else if (*str == '*') {
			*str = (char)('0' + i);
			break;
		}
		str++;
	}
}

void replace_char(char* str, const char* in_str, char find, char replace);

/// pads a string up to a num of characters if the current string size is shorter than num
[[gnu::always_inline]] constexpr void padStringTo(std::string& str, const size_t num) {
	if (num > str.size()) {
		str.insert(0, num - str.size(), ' ');
	}
}

int32_t stringToInt(char const* string);
int32_t stringToUIntOrError(char const* mem);
void getInstrumentPresetFilename(char const* filePrefix, int16_t presetNumber, int8_t presetSubslotNumber,
                                 char* fileName);
char const* oscTypeToString(OscType osctype);
OscType stringToOscType(char const* string);

char const* lfoTypeToString(LFOType oscType);
LFOType stringToLFOType(char const* string);

char const* synthModeToString(SynthMode synthMode);
SynthMode stringToSynthMode(char const* string);

char const* polyphonyModeToString(PolyphonyMode synthMode);
PolyphonyMode stringToPolyphonyMode(char const* string);

char const* fxTypeToString(ModFXType fxType);
ModFXType stringToFXType(char const* string);

char const* modFXParamToString(ModFXParam fxType);
ModFXParam stringToModFXParam(char const* string);

char const* filterTypeToString(FilterType fxType);
FilterType stringToFilterType(char const* string);

ArpMode oldModeToArpMode(OldArpMode oldMode);
ArpNoteMode oldModeToArpNoteMode(OldArpMode oldMode);
ArpOctaveMode oldModeToArpOctaveMode(OldArpMode oldMode);

char const* oldArpModeToString(OldArpMode mode);
OldArpMode stringToOldArpMode(char const* string);

char const* arpModeToString(ArpMode mode);
ArpMode stringToArpMode(char const* string);

char const* arpNoteModeToString(ArpNoteMode mode);
ArpNoteMode stringToArpNoteMode(char const* string);

char const* arpOctaveModeToString(ArpOctaveMode mode);
ArpOctaveMode stringToArpOctaveMode(char const* string);

char const* arpMpeModSourceToString(ArpMpeModSource modSource);
ArpMpeModSource stringToArpMpeModSource(char const* string);

char const* inputChannelToString(AudioInputChannel inputChannel);
AudioInputChannel stringToInputChannel(char const* string);

char const* sequenceDirectionModeToString(SequenceDirection sequenceDirectionMode);
SequenceDirection stringToSequenceDirectionMode(char const* string);
char const* launchStyleToString(LaunchStyle launchStyle);
LaunchStyle stringToLaunchStyle(char const* string);

char const* getInstrumentFolder(OutputType outputType);

bool isAudioFilename(char const* filename);
bool isAiffFilename(char const* filename);

char const* getFileNameFromEndOfPath(char const* filePathChars);
char const* getPathFromFullPath(char const* filePathChars);

int32_t getParamFromUserValue(uint8_t p, int8_t userValue);
int32_t getLookupIndexFromValue(int32_t value, const int32_t* table, int32_t maxIndex);
int32_t instantTan(int32_t input);

int32_t combineHitStrengths(int32_t strength1, int32_t strength2);

int32_t cableToLinearParamShortcut(int32_t sourceValue);
int32_t cableToExpParamShortcut(int32_t sourceValue);

class Sound;
struct StereoSample;

char const* getSourceDisplayNameForOLED(PatchSource s);

char const* sourceToString(PatchSource source);
PatchSource stringToSource(char const* string);
char const* sourceToStringShort(PatchSource source);

int32_t shiftVolumeByDB(int32_t oldValue, float offset);
int32_t quickLog(uint32_t input);

// input must not have any extra bits set than numBitsInInput specifies
// Output of this function (unlike the regular 1d one) is only +- 1073741824
[[gnu::always_inline]] inline int32_t interpolateTableSigned2d(uint32_t inputX, uint32_t inputY,
                                                               int32_t numBitsInInputX, int32_t numBitsInInputY,
                                                               const int16_t* table, int32_t numBitsInTableSizeX,
                                                               int32_t numBitsInTableSizeY) {

	int32_t whichValue = inputY >> (numBitsInInputY - numBitsInTableSizeY);

	int32_t tableSizeOneRow = (1 << numBitsInTableSizeX) + 1;

	int32_t value1 =
	    interpolateTableSigned(inputX, numBitsInInputX, &table[whichValue * tableSizeOneRow], numBitsInTableSizeX);
	int32_t value2 = interpolateTableSigned(inputX, numBitsInInputX, &table[(whichValue + 1) * tableSizeOneRow],
	                                        numBitsInTableSizeX);

	int32_t lshiftAmount = 31 + numBitsInTableSizeY - numBitsInInputY;

	uint32_t strength2;

	if (lshiftAmount >= 0)
		strength2 = (inputY << lshiftAmount) & 2147483647;
	else
		strength2 = (inputY >> (0 - lshiftAmount)) & 2147483647;

	uint32_t strength1 = 2147483647 - strength2;
	return multiply_32x32_rshift32(value1, strength1) + multiply_32x32_rshift32(value2, strength2);
}

template <unsigned saturationAmount>
[[gnu::always_inline]] inline int32_t getTanH(int32_t input) {
	uint32_t workingValue;

	if (saturationAmount)
		workingValue = (uint32_t)lshiftAndSaturate<saturationAmount>(input) + 2147483648u;
	else
		workingValue = (uint32_t)input + 2147483648u;

	return interpolateTableSigned(workingValue, 32, tanHSmall, 8) >> (saturationAmount + 2);
}

[[gnu::always_inline]] inline int32_t getTanHUnknown(int32_t input, uint32_t saturationAmount) {
	uint32_t workingValue;

	if (saturationAmount)
		workingValue = (uint32_t)lshiftAndSaturateUnknown(input, saturationAmount) + 2147483648u;
	else
		workingValue = (uint32_t)input + 2147483648u;

	return interpolateTableSigned(workingValue, 32, tanHSmall, 8) >> (saturationAmount + 2);
}

[[gnu::always_inline]] inline int32_t getTanHAntialiased(int32_t input, uint32_t* lastWorkingValue,
                                                         uint32_t saturationAmount) {

	uint32_t workingValue = (uint32_t)lshiftAndSaturateUnknown(input, saturationAmount) + 2147483648u;

	int32_t toReturn = interpolateTableSigned2d(workingValue, *lastWorkingValue, 32, 32, &tanH2d[0][0], 7, 6)
	                   >> (saturationAmount + 1);
	*lastWorkingValue = workingValue;
	return toReturn;
}

#define znew (z = 36969 * (z & 65535) + (z >> 16))
#define wnew (w = 18000 * (w & 65535) + (w >> 16))
#define MWC (int32_t)((znew << 16) + wnew)

[[gnu::always_inline]] inline uint8_t getRandom255() {
	return CONG >> 24;
}

[[gnu::always_inline]] inline int32_t getNoise() {
	return CONG;
}

/// generates a triangle distribution from -1q31 to 1q31 centered on 0 (Irwin-Hall)
inline q31_t sampleTriangleDistribution() {
	auto u1 = getNoise();
	auto u2 = getNoise();
	auto s = add_saturate(u1, u2);
	return s;
}

void seedRandom();

extern bool shouldInterpretNoteNames;
extern bool octaveStartsFromA;

int32_t random(int32_t upperLimit);
bool shouldDoPanning(int32_t panAmount, int32_t* amplitudeL, int32_t* amplitudeR);

uint32_t getOscInitialPhaseForZero(OscType waveType);
int32_t fastPythag(int32_t x, int32_t y);
int32_t strcmpspecial(char const* first, char const* second);
int32_t doLanczos(int32_t* data, int32_t pos, uint32_t posWithinPos, int32_t memoryNumElements);
int32_t doLanczosCircular(int32_t* data, int32_t pos, uint32_t posWithinPos, int32_t memoryNumElements);

// intensity is out of 65536 now
// occupancyMask is out of 64 now
inline RGB drawSquare(const RGB& squareColour, int32_t intensity, const RGB& square, uint8_t* occupancyMask,
                      int32_t occupancyFromWhichColourCame) {

	int32_t modifiedIntensity = intensity; //(intensity * occupancyFromWhichColourCame) >> 6; // Out of 65536

	// Make new colour being drawn into this square marginalise the colour already in the square
	int32_t colourRemainingAmount = 65536;

	// We know how much colour we want to add to this square, so constrain any existing colour to the remaining "space"
	// that it may still occupy
	int32_t maxOldOccupancy = (65536 - modifiedIntensity) >> 10;

	// If the square has more colour in it than it's allowed to retain, then plan to reduce it
	if (*occupancyMask > maxOldOccupancy) {
		colourRemainingAmount = (maxOldOccupancy << 16) / *occupancyMask; // out of 65536
	}

	// Add the new colour, reducing the old if that's what we're doing
	int32_t newOccupancyMaskValue =
	    rshift_round(*occupancyMask * colourRemainingAmount, 16) + rshift_round(modifiedIntensity, 10);
	*occupancyMask = std::min<int32_t>(64, newOccupancyMaskValue);

	return RGB::blend2(square, squareColour, colourRemainingAmount, modifiedIntensity);
}

[[gnu::always_inline]] inline int32_t increaseMagnitude(int32_t number, int32_t magnitude) {
	if (magnitude >= 0) {
		return number << magnitude;
	}
	return number >> (-magnitude);
}

[[gnu::always_inline]] inline int32_t increaseMagnitudeAndSaturate(int32_t number, int32_t magnitude) {
	if (magnitude > 0) {
		return lshiftAndSaturateUnknown(number, magnitude);
	}
	return number >> (-magnitude);
}

int32_t howMuchMoreMagnitude(uint32_t to, uint32_t from);
void noteCodeToString(int32_t noteCode, char* buffer, int32_t* getLengthWithoutDot = nullptr,
                      bool appendOctaveNo = true);
void concatenateLines(const char* lines[], size_t numLines, char* resultString);
int32_t divide_round_negative(int32_t dividend, int32_t divisor);

[[gnu::always_inline]] inline int32_t getMagnitudeOld(uint32_t input) {
	return 32 - clz(input);
}

[[gnu::always_inline]] inline int32_t getMagnitude(uint32_t input) {
	return 31 - clz(input);
}

[[gnu::always_inline]] inline bool isPowerOfTwo(uint32_t input) {
	return (input == 1 << getMagnitude(input));
}

int32_t getWhichKernel(int32_t phaseIncrement);

int32_t memcasecmp(char const* first, char const* second, int32_t size);
int32_t getHowManyCharsAreTheSame(char const* a, char const* b);
void dimColour(uint8_t colour[3]);
bool charCaseEqual(char firstChar, char secondChar);
bool shouldAbortLoading();
int32_t getNoteMagnitudeFfromNoteLength(uint32_t noteLength, int32_t tickMagnitude);
/// buffer must have at least 5 characters on 7seg, or 30 for OLED
void getNoteLengthNameFromMagnitude(etl::istring& buf, int32_t magnitude, char const* durrationSuffix = "-notes",
                                    bool clarifyPerColumn = false);
bool doesFilenameFitPrefixFormat(char const* fileName, char const* filePrefix, int32_t prefixLength);
Error fresultToDelugeErrorCode(FRESULT result);
namespace FatFS {
enum class Error;
}
Error fatfsErrorToDelugeError(FatFS::Error result);

[[gnu::always_inline]] inline void writeInt16(char** address, uint16_t number) {
	*(uint16_t*)*address = number;
	*address += 2;
}

[[gnu::always_inline]] inline void writeInt32(char** address, uint32_t number) {
	*(uint32_t*)*address = number;
	*address += 4;
}

extern char miscStringBuffer[];

constexpr size_t kShortStringBufferSize = 64;
extern char shortStringBuffer[];

struct StereoFloatSample {
	float l;
	float r;
};

float sigmoidLikeCurve(const float x, const float xMax, const float softening);
