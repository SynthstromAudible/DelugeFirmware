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
#include "libdeluge/file_io.h"
#include "util/audio_format_helpers.h" // byte/format helpers (charsToIntegerConstant, swapEndianness*, …) re-exported
#include "util/c_string.h"
#include "util/cfunctions.h" // IWYU pragma: export - minimal set of functions which need c linkage
#include "util/db_shift.h"
#include "util/etl_string.h"
#include "util/fixedpoint.h"
#include "util/lookuptables/lookuptables.h"
#include "util/name_compare.h" // IWYU pragma: export - strcmpspecial and friends used to live here
#include "util/synth_noise.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

class UI;

extern UI* getCurrentUI();

extern const uint8_t modButtonX[];
extern const uint8_t modButtonY[];

extern uint8_t subModeToReturnTo;

char const* getThingName(OutputType outputType);
char const* getOutputTypeName(OutputType outputType, int32_t channel);

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

char const* getInstrumentFolder(OutputType outputType);

bool isAudioFilename(char const* filename);
bool isAiffFilename(char const* filename);

char const* getFileNameFromEndOfPath(char const* filePathChars);

int32_t getParamFromUserValue(uint8_t p, int8_t userValue);
int32_t getLookupIndexFromValue(int32_t value, const int32_t* table, int32_t maxIndex);

class Sound;
namespace deluge::dsp {
struct StereoSample;
} // namespace deluge::dsp
using StereoSample = deluge::dsp::StereoSample;

char const* getSourceDisplayName(PatchSource s);

char const* sourceToString(PatchSource source);
PatchSource stringToSource(char const* string);
char const* sourceToStringShort(PatchSource source);

[[gnu::always_inline]] inline uint8_t getRandom255() {
	return deluge::synth_noise.next() >> 24;
}

[[gnu::always_inline]] inline int32_t getNoise() {
	return static_cast<int32_t>(deluge::synth_noise.next());
}

/// generates a triangle distribution from -1q31 to 1q31 centered on 0 (Irwin-Hall)
inline q31_t sampleTriangleDistribution() {
	auto u1 = getNoise();
	auto u2 = getNoise();
	auto s = add_saturate(u1, u2);
	return s;
}

void seedRandom();

int32_t random(int32_t upperLimit);

[[gnu::always_inline]] inline int32_t increaseMagnitude(int32_t number, int32_t magnitude) {
	if (magnitude >= 0) {
		return number << magnitude;
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

int32_t memcasecmp(char const* first, char const* second, int32_t size);
int32_t getHowManyCharsAreTheSame(char const* a, char const* b);
void dimColour(uint8_t colour[3]);
bool charCaseEqual(char firstChar, char secondChar);
int32_t getNoteMagnitudeFfromNoteLength(uint32_t noteLength, int32_t tickMagnitude);
/// buffer must have at least 5 characters on 7seg, or 30 for OLED
void getNoteLengthNameFromMagnitude(etl::istring& buf, int32_t magnitude, char const* durrationSuffix = "-notes",
                                    bool clarifyPerColumn = false);
bool doesFilenameFitPrefixFormat(char const* fileName, char const* filePrefix, int32_t prefixLength);
Error delugeStatusToError(DelugeStatus status);

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

float sigmoidLikeCurve(const float x, const float xMax, const float softening);
