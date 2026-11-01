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
#include "dsp-next/fixed/curves/instant_tan.h"
#include "gui/colour/colour.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "hid/display/display.h"
#include "libdeluge/clock.h"
#include "modulation/arpeggiator.h"
#include "processing/audio_output.h"
#include "processing/sound/sound.h"
#include "storage/flash_storage.h"
#include "util/lookuptables/lookuptables.h"
#include "util/name_compare.h"
#include <cmath>
#include <cstdint>
#include <string.h>

namespace params = deluge::modulation::params;
using namespace deluge;
using params::kNumParams;

const uint8_t modButtonX[8] = {1, 1, 1, 1, 2, 2, 2, 2};
const uint8_t modButtonY[8] = {0, 1, 2, 3, 0, 1, 2, 3};
const uint8_t modLedX[8] = {1, 1, 1, 1, 2, 2, 2, 2};
const uint8_t modLedY[8] = {0, 1, 2, 3, 0, 1, 2, 3};

char const* sourceToString(PatchSource source) {
	switch (source) {
	case PatchSource::LFO_GLOBAL_1:
		return "lfo1";

	case PatchSource::LFO_GLOBAL_2:
		return "lfo3";

	case PatchSource::LFO_LOCAL_1:
		return "lfo2";

	case PatchSource::LFO_LOCAL_2:
		return "lfo4";

	case PatchSource::ENVELOPE_0:
		return "envelope1";

	case PatchSource::ENVELOPE_1:
		return "envelope2";

	case PatchSource::ENVELOPE_2:
		return "envelope3";

	case PatchSource::ENVELOPE_3:
		return "envelope4";

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

char const* getSourceDisplayName(PatchSource s) {
	using enum l10n::String;
	auto lang = l10n::chosenLanguage;

	switch (s) {
	case PatchSource::LFO_GLOBAL_1:
		return l10n::get(STRING_FOR_PATCH_SOURCE_LFO_GLOBAL_1);

	case PatchSource::LFO_GLOBAL_2:
		return l10n::get(STRING_FOR_PATCH_SOURCE_LFO_GLOBAL_2);

	case PatchSource::LFO_LOCAL_1:
		return l10n::get(STRING_FOR_PATCH_SOURCE_LFO_LOCAL_1);

	case PatchSource::LFO_LOCAL_2:
		return l10n::get(STRING_FOR_PATCH_SOURCE_LFO_LOCAL_2);

	case PatchSource::ENVELOPE_0:
		return l10n::get(STRING_FOR_PATCH_SOURCE_ENVELOPE_0);

	case PatchSource::ENVELOPE_1:
		return l10n::get(STRING_FOR_PATCH_SOURCE_ENVELOPE_1);

	case PatchSource::ENVELOPE_2:
		return l10n::get(STRING_FOR_PATCH_SOURCE_ENVELOPE_2);

	case PatchSource::ENVELOPE_3:
		return l10n::get(STRING_FOR_PATCH_SOURCE_ENVELOPE_3);

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
	case PatchSource::LFO_GLOBAL_1:
		return "lfo1";

	case PatchSource::LFO_GLOBAL_2:
		return "lfo3";

	case PatchSource::LFO_LOCAL_1:
		return "lfo2";

	case PatchSource::LFO_LOCAL_2:
		return "lfo4";

	case PatchSource::ENVELOPE_0:
		return "env1";

	case PatchSource::ENVELOPE_1:
		return "env2";

	case PatchSource::ENVELOPE_2:
		return "env3";

	case PatchSource::ENVELOPE_3:
		return "env4";

	case PatchSource::VELOCITY:
		return "velo";

	case PatchSource::NOTE:
		return "note";

	case PatchSource::SIDECHAIN:
		return "side";

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

bool memIsNumericChars(char const* mem, int32_t size) {
	for (int32_t i = 0; i < size; i++) {
		if (*mem < 48 || *mem >= 58) {
			return false;
		}
		mem++;
	}
	return true;
}

bool stringIsNumericChars(std::string_view str) {
	return memIsNumericChars(str.data(), str.size());
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

char const* getOutputTypeName(OutputType outputType, int32_t channel) {
	switch (outputType) {
	case OutputType::SYNTH:
		return "Synth";
	case OutputType::KIT:
		return "Kit";
	case OutputType::MIDI_OUT:
		if (channel < 16) {
			return "MIDI";
		}
		else if (channel == MIDI_CHANNEL_MPE_LOWER_ZONE || channel == MIDI_CHANNEL_MPE_UPPER_ZONE) {
			return "MPE";
		}
		else {
			return "Internal";
		}
	case OutputType::CV:
		return "CV / gate";
	case OutputType::AUDIO: {
		auto mode = static_cast<AudioOutputMode>(channel);
		switch (mode) {

		case AudioOutputMode::player:
			return "Audio Player";
		case AudioOutputMode::sampler:
			return "Audio Sampler";
		case AudioOutputMode::looper:
			return "Audio Looper";
		}
		return "Audio";
	}
	default:
		return "None";
	}
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

// memToUIntOrError moved to util/audio_format_helpers.cpp (dep-free).

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
	return deluge::instrument::to_string(oscType).data();
}

OscType stringToOscType(char const* string) {
	return deluge::instrument::parse_osc_type(string);
}

char const* lfoTypeToString(LFOType oscType) {
	return deluge::instrument::to_string(oscType).data();
}

LFOType stringToLFOType(char const* string) {
	return deluge::instrument::parse_lfo_type(string);
}

char const* synthModeToString(SynthMode synthMode) {
	return deluge::instrument::to_string(synthMode).data();
}

SynthMode stringToSynthMode(char const* string) {
	return deluge::instrument::parse_synth_mode(string);
}

char const* polyphonyModeToString(PolyphonyMode synthMode) {
	return deluge::instrument::to_string(synthMode).data();
}

PolyphonyMode stringToPolyphonyMode(char const* string) {
	return deluge::instrument::parse_polyphony_mode(string);
}

char const* fxTypeToString(ModFXType fxType) {
	return deluge::effects::to_string(fxType).data(); // a view of a string literal
}
ModFXType stringToFXType(char const* string) {
	return deluge::effects::parse_mod_fx_type(string);
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

	case ArpNoteMode::WALK1:
		return "walk1";

	case ArpNoteMode::WALK2:
		return "walk2";

	case ArpNoteMode::WALK3:
		return "walk3";

	case ArpNoteMode::PATTERN:
		return "pattern";

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
	else if (!strcmp(string, "walk1")) {
		return ArpNoteMode::WALK1;
	}
	else if (!strcmp(string, "walk2")) {
		return ArpNoteMode::WALK2;
	}
	else if (!strcmp(string, "walk3")) {
		return ArpNoteMode::WALK3;
	}
	else if (!strcmp(string, "pattern")) {
		return ArpNoteMode::PATTERN;
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

	case ArpOctaveMode::UP_DOWN:
		return "upDown";

	case ArpOctaveMode::ALTERNATE:
		return "alt";

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
		return ArpOctaveMode::UP_DOWN;
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

	case AudioInputChannel::USB:
		return "usb";

	case AudioInputChannel::MIX:
		return "mix";

	case AudioInputChannel::OUTPUT:
		return "output";

	case AudioInputChannel::SPECIFIC_OUTPUT:
		return "specificTrack";

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
	else if (!strcmp(string, "usb")) {
		return AudioInputChannel::USB;
	}
	else if (!strcmp(string, "mix")) {
		return AudioInputChannel::MIX;
	}
	else if (!strcmp(string, "output")) {
		return AudioInputChannel::OUTPUT;
	}
	else if (!strcmp(string, "specificTrack")) {
		return AudioInputChannel::SPECIFIC_OUTPUT;
	}

	else {
		return AudioInputChannel::NONE;
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

bool isAudioFilename(char const* filename) {
	if (filename[0] == '.') {
		return false;
	}
	auto* dotPos = strrchr(filename, '.');
	return (!strcasecmp(dotPos, ".WAV") || !strcasecmp(dotPos, ".AIF") || !strcasecmp(dotPos, ".AIFF"));
}

bool isAiffFilename(char const* filename) {
	auto* dotPos = strrchr(filename, '.');
	return (dotPos != NULL && (!strcasecmp(dotPos, ".AIF") || !strcasecmp(dotPos, ".AIFF")));
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

int32_t random(int32_t upperLimit) {
	return (uint16_t)(deluge::synth_noise.next() >> 16) % (upperLimit + 1);
}

void replace_char(char* out_str, const char* in_str, char find, char replace) {
	strcpy(out_str, in_str);
	auto* current_pos = strchr(out_str, find);
	while (current_pos) {
		*current_pos = replace;
		current_pos = strchr(current_pos, find);
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

void noteCodeToString(int32_t noteCode, char* buffer, int32_t* getLengthWithoutDot, bool appendOctaveNo) {
	char* thisChar = buffer;
	int32_t octave = (noteCode) / 12 - 2;
	int32_t noteCodeWithinOctave = (uint16_t)(noteCode + 120) % (uint8_t)12;

	bool useSharps = FlashStorage::defaultUseSharps;

	*thisChar =
	    useSharps ? noteCodeToNoteLetter[noteCodeWithinOctave] : noteCodeToNoteLetterFlats[noteCodeWithinOctave];

	thisChar++;
	if (noteCodeIsSharp[noteCodeWithinOctave]) {
		char accidential = useSharps ? '#' : FLAT_CHAR;
		*thisChar = accidential;
		thisChar++;
	}
	if (appendOctaveNo) {
		intToString(octave, thisChar, 1); // terminates the string for us
	}
	else {
		*thisChar = 0;
	}

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
	deluge::synth_noise.seed(deluge_clock_now());
}

// ConvertFromIeeeExtended moved to util/audio_format_helpers.cpp (dep-free).

// Divisor must be positive. Rounds towards negative infinity
int32_t divide_round_negative(int32_t dividend, int32_t divisor) {
	if (dividend >= 0) {
		return (uint32_t)dividend / (uint32_t)divisor;
	}
	else {
		return -((uint32_t)(-dividend - 1) / (uint32_t)divisor) - 1;
	}
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

int32_t getNoteMagnitudeFfromNoteLength(uint32_t noteLength, int32_t tickMagnitude) {
	// Given a note length and a tick magnitude, figure out the _note magnitude_
	int32_t noteMagnitude = -5 - tickMagnitude;
	uint32_t level = 3;

	while (level < noteLength) {
		noteMagnitude++;
		level <<= 1;
	}
	return noteMagnitude;
}

void getNoteLengthNameFromMagnitude(etl::istring& noteLengthBuf, int32_t magnitude, char const* const notesString,
                                    bool clarifyPerColumn) {
	// Positive magnitudes are bars, negative magnitudes are divisions of bars.
	uint32_t division = (uint32_t)1 << (0 - magnitude);

	if (magnitude < 0) {
		deluge::string::appendInt(noteLengthBuf, division);
		// this is not fully general but since division are always a power of 2, it works out in practice (no need
		// for "rd")
		char const* suffix = ((division % 10) == 2) ? "nd" : "th";
		noteLengthBuf.append(suffix);
		if (notesString != nullptr) {
			noteLengthBuf.append(notesString);
		}
	}
	else {
		uint32_t numBars = (uint32_t)1 << magnitude;
		deluge::string::appendInt(noteLengthBuf, numBars);
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

char const* getFileNameFromEndOfPath(char const* filePathChars) {
	char const* slashPos = strrchr(filePathChars, '/');
	return slashPos ? (slashPos + 1) : filePathChars;
}

bool doesFilenameFitPrefixFormat(char const* fileName, char const* filePrefix, int32_t prefixLength) {

	if (memcasecmp(fileName, filePrefix, prefixLength)) {
		return false;
	}

	auto* dotAddress = strrchr(fileName, '.');
	if (!dotAddress) {
		return false;
	}

	int32_t dotPos = dotAddress - fileName;
	if (dotPos < prefixLength + 3) {
		return false;
	}

	if (!memIsNumericChars(&fileName[prefixLength], 3)) {
		return false;
	}

	return true;
}

Error delugeStatusToError(DelugeStatus status) {
	switch (status) {
	case DELUGE_OK:
		return Error::NONE;
	case DELUGE_ERR_NOT_FOUND:
		return Error::FILE_NOT_FOUND;
	case DELUGE_ERR_EXISTS:
		return Error::FILE_ALREADY_EXISTS;
	case DELUGE_ERR_NO_SPACE:
		return Error::SD_CARD_FULL;
	case DELUGE_ERR_NO_FILESYSTEM:
		return Error::SD_CARD_NO_FILESYSTEM;
	case DELUGE_ERR_WRITE_PROTECTED:
		return Error::WRITE_PROTECTED;
	case DELUGE_ERR_NODEV:
		return Error::SD_CARD_NOT_PRESENT;
	case DELUGE_ERR_NO_MEMORY:
		return Error::INSUFFICIENT_RAM;
	default:
		return Error::SD_CARD;
	}
}

char miscStringBuffer[kFilenameBufferSize] __attribute__((aligned(CACHE_LINE_SIZE)));
char shortStringBuffer[kShortStringBufferSize] __attribute__((aligned(CACHE_LINE_SIZE)));

float sigmoidLikeCurve(const float x, const float xMax, const float softening) {
	const float raw = x / (x + softening);
	const float maxVal = xMax / (xMax + softening);
	return raw / maxVal;
};
