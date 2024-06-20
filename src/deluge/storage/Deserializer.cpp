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

#include "definitions_cxx.hpp"
#include "drivers/pic/pic.h"
#include "fatfs/fatfs.hpp"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/storage_manager.h"
#include "util/firmware_version.h"
#include "util/functions.h"
#include "util/try.h"
#include "version.h"
#include <string.h>

extern "C" {
#include "RZA1/oled/oled_low_level.h"
#include "fatfs/diskio.h"
#include "fatfs/ff.h"
}

char charAtEndOfValue;

/*******************************************************************************

    XMLDeserializer

********************************************************************************/

#define BETWEEN_TAGS 0
#define IN_TAG_NAME 1
#define IN_TAG_PAST_NAME 2
#define IN_ATTRIBUTE_NAME 3
#define PAST_ATTRIBUTE_NAME 4
#define PAST_EQUALS_SIGN 5
#define IN_ATTRIBUTE_VALUE 6

XMLDeserializer::XMLDeserializer() {

	reset();
}

void XMLDeserializer::reset() {
	resetReader();
	// Prep to read first Cluster shortly
	fileReadBufferCurrentPos = audioFileManager.clusterSize;
	currentReadBufferEndPos = audioFileManager.clusterSize;

	firmware_version = FirmwareVersion{FirmwareVersion::Type::OFFICIAL, {}};

	tagDepthFile = 0;
	tagDepthCaller = 0;

	xmlArea = BETWEEN_TAGS;
}

// Only call this if IN_TAG_NAME
// TODO: this is very sub-optimal and still calls readChar()
char const* XMLDeserializer::readTagName() {

	if (false) {
skipToNextTag:
		skipUntilChar('>');
		skipUntilChar('<');
	}

	char thisChar;
	int32_t charPos = 0;

	while (readChar(&thisChar)) {
		switch (thisChar) {
		case '/':
			goto skipPastRest;

		case ' ':
		case '\r':
		case '\n':
		case '\t':
			xmlArea = IN_TAG_PAST_NAME;
			goto getOut;

		case '?':
			goto skipToNextTag;

		case '>':
			xmlArea = BETWEEN_TAGS;
			goto getOut;

		default:
			if (!charPos) {
				tagDepthFile++;
			}

			// Store this character, if space in our un-ideal buffer
			if (charPos < kFilenameBufferSize - 1) {
				stringBuffer[charPos++] = thisChar;
			}
		}
	}

getOut:
	readDone();

	if (false) {
skipPastRest:
		tagDepthFile--;
		skipUntilChar('>');
		xmlArea = BETWEEN_TAGS;
	}

	stringBuffer[charPos] = 0;
	return stringBuffer;
}

// Only call when IN_TAG_PAST_NAME
char const* XMLDeserializer::readNextAttributeName() {

	char thisChar;
	int32_t charPos = 0;

	while (readChar(&thisChar)) {
		switch (thisChar) {
		case ' ':
		case '\r':
		case '\n':
		case '\t':
			break;

		case '/':
			tagDepthFile--;
			skipUntilChar('>');
			// No break

		case '>':
			xmlArea = BETWEEN_TAGS;
			// No break

		case '<': // This is an error - there definitely shouldn't be a '<' inside a tag! TODO: make way to return error
			goto noMoreAttributes;

		default:
			goto doReadName;
		}
	}

noMoreAttributes:
	return "";

	// Here, we're in IN_ATTRIBUTE_NAME, and we're not allowed to leave this while loop until our xmlArea changes to
	// something else
	// - or there's an error or file-end, in which case we'll return error below
doReadName:
	xmlArea = IN_ATTRIBUTE_NAME;
	tagDepthFile++;
	fileReadBufferCurrentPos--; // This means we don't need to call readFileClusterIfNecessary()

	bool haveReachedNameEnd = false;

	// This is basically copied and tweaked from readUntilChar()
	do {
		int32_t bufferPosAtStart = fileReadBufferCurrentPos;
		while (fileReadBufferCurrentPos < currentReadBufferEndPos) {
			char thisChar = fileClusterBuffer[fileReadBufferCurrentPos];

			switch (thisChar) {
			case ' ':
			case '\r':
			case '\n':
			case '\t':
				xmlArea = PAST_ATTRIBUTE_NAME;
				goto reachedNameEnd;

			case '=':
				xmlArea = PAST_EQUALS_SIGN;
				goto reachedNameEnd;

			// If we get a close-tag name, it means we saw some sorta attribute name with no value, which isn't allowed,
			// so treat it as invalid
			case '>':
				xmlArea = BETWEEN_TAGS;
				goto noMoreAttributes;

				// TODO: a '/' should get us outta here too...
			}

			fileReadBufferCurrentPos++;
		}

		if (false) {
reachedNameEnd:
			readDone();
			haveReachedNameEnd = true;
			// If possible, just return a pointer to the chars within the existing buffer
			if (!charPos && fileReadBufferCurrentPos < currentReadBufferEndPos) {
				fileClusterBuffer[fileReadBufferCurrentPos] = 0; // NULL end of the string we're returning
				fileReadBufferCurrentPos++;                      // Gets us past the endChar
				return &fileClusterBuffer[bufferPosAtStart];
			}
		}

		int32_t numCharsHere = fileReadBufferCurrentPos - bufferPosAtStart;
		int32_t numCharsToCopy = std::min<int32_t>(numCharsHere, kFilenameBufferSize - 1 - charPos);

		if (numCharsToCopy > 0) {
			memcpy(&stringBuffer[charPos], &fileClusterBuffer[bufferPosAtStart], numCharsToCopy);

			charPos += numCharsToCopy;
		}

		if (haveReachedNameEnd) {
			stringBuffer[charPos] = 0;
			fileReadBufferCurrentPos++; // Gets us past the endChar
			return stringBuffer;
		}

	} while (fileReadBufferCurrentPos == currentReadBufferEndPos && readFileClusterIfNecessary());

	// If here, file ended
	return "";
}

// char charAtEndOfValue; // *** JFF get rid of this global!

char const* XMLDeserializer::readNextTagOrAttributeName() {

	char const* toReturn;
	int32_t tagDepthStart = tagDepthFile;

	switch (xmlArea) {

	default:
#if ALPHA_OR_BETA_VERSION
		// Can happen with invalid files, though I'm implementing error checks whenever a user alerts me to a scenario.
		// Fraser got this, Nov 2021.
		FREEZE_WITH_ERROR("E365");
#else
		__builtin_unreachable();
#endif
		break;

	case IN_ATTRIBUTE_VALUE: // Could have been left here during a char-at-a-time read
		skipUntilChar(charAtEndOfValue);
		xmlArea = IN_TAG_PAST_NAME;
		// No break

	case IN_TAG_PAST_NAME:
		toReturn = readNextAttributeName();
		// If depth has changed, this means we met a /> and must get out
		if (*toReturn || tagDepthFile != tagDepthStart) {
			break;
		}
		// No break

	case BETWEEN_TAGS:
		skipUntilChar('<');
		xmlArea = IN_TAG_NAME;
		// No break

	case IN_TAG_NAME:
		toReturn = readTagName();
	}

	if (*toReturn) {
		/*
		for (int32_t t = 0; t < tagDepthCaller; t++) {
D_PRINTLN("\t");
		}
		D_PRINTLN(toReturn);
		*/
		tagDepthCaller++;
		AudioEngine::logAction(toReturn);
	}

	return toReturn;
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_EQUALS_SIGN
// Returns the quote character that opens the value - or 0 if fail
bool XMLDeserializer::getIntoAttributeValue() {

	char thisChar;

	switch (xmlArea) {
	case PAST_ATTRIBUTE_NAME:
		while (readChar(&thisChar)) {
			switch (thisChar) {
			case ' ':
			case '\r':
			case '\n':
			case '\t':
				break;

			case '=':
				xmlArea = PAST_EQUALS_SIGN;
				goto pastEqualsSign;

			default:
				return false; // There shouldn't be any other characters. If there are, that's an error
			}
		}

		break;

	case PAST_EQUALS_SIGN:
pastEqualsSign:
		while (readChar(&thisChar)) {
			switch (thisChar) {
			case ' ':
			case '\r':
			case '\n':
			case '\t':
				break;

			case '"':
			case '\'':
				goto inAttributeValue;

			default:
				return false; // There shouldn't be any other characters. If there are, that's an error
			}
		}
		break;
	}

	if (false) {
inAttributeValue:
		xmlArea = IN_ATTRIBUTE_VALUE;
		tagDepthFile--;
		charAtEndOfValue = thisChar;
		return true;
	}

	return false; // Fail
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_EQUALS_SIGN
char const* XMLDeserializer::readAttributeValue() {

	if (!getIntoAttributeValue()) {
		return "";
	}
	xmlArea = IN_TAG_PAST_NAME; // How it'll be after this next call
	return readUntilChar(charAtEndOfValue);
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_EQUALS_SIGN
int32_t XMLDeserializer::readAttributeValueInt() {

	if (!getIntoAttributeValue()) {
		return 0;
	}
	xmlArea = IN_TAG_PAST_NAME; // How it'll be after this next call
	return readIntUntilChar(charAtEndOfValue);
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_EQUALS_SIGN
Error XMLDeserializer::readAttributeValueString(String* string) {

	if (!getIntoAttributeValue()) {
		string->clear();
		return Error::NONE;
	}
	Error error = readStringUntilChar(string, charAtEndOfValue);
	if (error == Error::NONE) {
		xmlArea = IN_TAG_PAST_NAME;
	}
	return error;
}

void XMLDeserializer::skipUntilChar(char endChar) {

	readFileClusterIfNecessary(); // Does this need to be here? Originally I didn't have it...
	do {
		while (fileReadBufferCurrentPos < currentReadBufferEndPos
		       && fileClusterBuffer[fileReadBufferCurrentPos] != endChar) {
			fileReadBufferCurrentPos++;
		}

	} while (fileReadBufferCurrentPos == currentReadBufferEndPos && readFileClusterIfNecessary());

	fileReadBufferCurrentPos++; // Gets us past the endChar

	readDone();
}

// Returns memory error. If error, caller must deal with the fact that the end-character hasn't been reached
Error XMLDeserializer::readStringUntilChar(String* string, char endChar) {

	int32_t newStringPos = 0;

	do {
		int32_t bufferPosNow = fileReadBufferCurrentPos;
		while (bufferPosNow < currentReadBufferEndPos && fileClusterBuffer[bufferPosNow] != endChar) {
			bufferPosNow++;
		}

		int32_t numCharsHere = bufferPosNow - fileReadBufferCurrentPos;

		if (numCharsHere) {
			Error error =
			    string->concatenateAtPos(&fileClusterBuffer[fileReadBufferCurrentPos], newStringPos, numCharsHere);

			fileReadBufferCurrentPos = bufferPosNow;

			if (error != Error::NONE) {
				return error;
			}

			newStringPos += numCharsHere;
		}

	} while (fileReadBufferCurrentPos == currentReadBufferEndPos && readFileClusterIfNecessary());

	fileReadBufferCurrentPos++; // Gets us past the endChar

	readDone();
	return Error::NONE;
}

char const* XMLDeserializer::readUntilChar(char endChar) {
	int32_t charPos = 0;

	do {
		int32_t bufferPosAtStart = fileReadBufferCurrentPos;
		while (fileReadBufferCurrentPos < currentReadBufferEndPos
		       && fileClusterBuffer[fileReadBufferCurrentPos] != endChar) {
			fileReadBufferCurrentPos++;
		}

		// If possible, just return a pointer to the chars within the existing buffer
		if (!charPos && fileReadBufferCurrentPos < currentReadBufferEndPos) {
			fileClusterBuffer[fileReadBufferCurrentPos] = 0;

			fileReadBufferCurrentPos++; // Gets us past the endChar
			return &fileClusterBuffer[bufferPosAtStart];
		}

		int32_t numCharsHere = fileReadBufferCurrentPos - bufferPosAtStart;
		int32_t numCharsToCopy = std::min<int32_t>(numCharsHere, kFilenameBufferSize - 1 - charPos);

		if (numCharsToCopy > 0) {
			memcpy(&stringBuffer[charPos], &fileClusterBuffer[bufferPosAtStart], numCharsToCopy);

			charPos += numCharsToCopy;
		}

	} while (fileReadBufferCurrentPos == currentReadBufferEndPos && readFileClusterIfNecessary());

	fileReadBufferCurrentPos++; // Gets us past the endChar

	readDone();

	stringBuffer[charPos] = 0;
	return stringBuffer;
}

// Unlike readUntilChar(), above, does not put a null character at the end of the returned "string". And, has a preset
// number of chars. And, returns NULL when nothing more to return. numChars must be <= FILENAME_BUFFER_SIZE
char const* XMLDeserializer::readNextCharsOfTagOrAttributeValue(int32_t numChars) {

	int32_t charPos = 0;

	do {
		int32_t bufferPosAtStart = fileReadBufferCurrentPos;
		int32_t bufferPosAtEnd = bufferPosAtStart + numChars - charPos;

		int32_t currentReadBufferEndPosNow = std::min<int32_t>(currentReadBufferEndPos, bufferPosAtEnd);

		while (fileReadBufferCurrentPos < currentReadBufferEndPosNow) {
			if (fileClusterBuffer[fileReadBufferCurrentPos] == charAtEndOfValue) {
				goto reachedEndCharEarly;
			}
			fileReadBufferCurrentPos++;
		}

		int32_t numCharsHere = fileReadBufferCurrentPos - bufferPosAtStart;

		// If we were able to just read the whole thing in one go, just return a pointer to the chars within the
		// existing buffer
		if (numCharsHere == numChars) {
			readDone();
			return &fileClusterBuffer[bufferPosAtStart];
		}

		// Otherwise, so long as we read something, add it to our buffer we're putting the output in
		if (numCharsHere > 0) {
			memcpy(&stringBuffer[charPos], &fileClusterBuffer[bufferPosAtStart], numCharsHere);

			charPos += numCharsHere;

			// And if we've now got all the chars we needed, return
			if (charPos == numChars) {
				readDone();
				return stringBuffer;
			}
		}

	} while (fileReadBufferCurrentPos == currentReadBufferEndPos && readFileClusterIfNecessary());

	// If we're here, the file ended
	return NULL;

	// And, additional bit we jump to when end-char reached
reachedEndCharEarly:
	fileReadBufferCurrentPos++; // Gets us past the endChar
	if (charAtEndOfValue == '<') {
		xmlArea = IN_TAG_NAME;
	}
	else {
		xmlArea = IN_TAG_PAST_NAME; // Could be ' or "
	}
	return NULL;
}

// This is almost never called now - TODO: get rid
char XMLDeserializer::readNextCharOfTagOrAttributeValue() {

	char thisChar;
	if (!readChar(&thisChar)) {
		return 0;
	}
	if (thisChar == charAtEndOfValue) {
		if (charAtEndOfValue == '<') {
			xmlArea = IN_TAG_NAME;
		}
		else {
			xmlArea = IN_TAG_PAST_NAME; // Could be ' or "
		}
		readDone();
		return 0;
	}
	return thisChar;
}

// Will always skip up until the end-char, even if it doesn't like the contents it sees
int32_t XMLDeserializer::readIntUntilChar(char endChar) {
	uint32_t number = 0;
	char thisChar;

	if (!readChar(&thisChar)) {
		return 0;
	}

	bool isNegative = (thisChar == '-');
	if (!isNegative) {
		goto readDigit;
	}

	while (readChar(&thisChar)) {
readDigit:
		if (!(thisChar >= '0' && thisChar <= '9')) {
			goto getOut;
		}
		number *= 10;
		number += (thisChar - '0');
	}

	if (false) {
getOut:
		if (thisChar != endChar) {
			skipUntilChar(endChar);
		}
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

char const* XMLDeserializer::readTagOrAttributeValue() {

	switch (xmlArea) {

	case BETWEEN_TAGS:
		xmlArea = IN_TAG_NAME; // How it'll be after this call
		return readUntilChar('<');

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		return readAttributeValue();

	case IN_TAG_PAST_NAME: // Could happen if trying to read a value but instead of a value there are multiple more
	                       // contents, like attributes etc. Obviously not "meant" to happen, but we need to cope.
		return "";

	default:
		FREEZE_WITH_ERROR("BBBB");
		__builtin_unreachable();
	}
}

int32_t XMLDeserializer::readTagOrAttributeValueInt() {

	switch (xmlArea) {

	case BETWEEN_TAGS:
		xmlArea = IN_TAG_NAME; // How it'll be after this call
		return readIntUntilChar('<');

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		return readAttributeValueInt();

	case IN_TAG_PAST_NAME: // Could happen if trying to read a value but instead of a value there are multiple more
	                       // contents, like attributes etc. Obviously not "meant" to happen, but we need to cope.
		return 0;

	default:
		FREEZE_WITH_ERROR("BBBB");
		__builtin_unreachable();
	}
}

// This isn't super optimal, like the int32_t version is, but only rarely used
int32_t XMLDeserializer::readTagOrAttributeValueHex(int32_t errorValue) {
	char const* string = readTagOrAttributeValue();
	if (string[0] != '0' || string[1] != 'x') {
		return errorValue;
	}
	return hexToInt(&string[2]);
}

int XMLDeserializer::readTagOrAttributeValueHexBytes(uint8_t* bytes, int32_t maxLen) {
	switch (xmlArea) {

	case BETWEEN_TAGS:
		xmlArea = IN_TAG_NAME; // How it'll be after this call
		return readHexBytesUntil(bytes, maxLen, '<');

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		if (!getIntoAttributeValue())
			return 0;
		xmlArea = IN_TAG_PAST_NAME; // How it'll be after this next call
		return readHexBytesUntil(bytes, maxLen, charAtEndOfValue);

	case IN_TAG_PAST_NAME: // Could happen if trying to read a value but instead of a value there are multiple more
	                       // contents, like attributes etc. Obviously not "meant" to happen, but we need to cope.
		return 0;

	default:
		FREEZE_WITH_ERROR("BBBB");
		__builtin_unreachable();
	}
}

bool getNibble(char ch, int* nibble) {
	if ('0' <= ch and ch <= '9') {
		*nibble = ch - '0';
	}
	else if ('a' <= ch and ch <= 'f') {
		*nibble = ch - 'a' + 10;
	}
	else if ('A' <= ch and ch <= 'F') {
		*nibble = ch - 'A' + 10;
	}
	else {
		return false;
	}
	return true;
}

int XMLDeserializer::readHexBytesUntil(uint8_t* bytes, int32_t maxLen, char endChar) {
	int read;
	char thisChar;

	for (read = 0; read < maxLen; read++) {
		int highNibble, lowNibble;

		if (!readChar(&thisChar))
			return 0;
		if (!getNibble(thisChar, &highNibble)) {
			goto getOut;
		}

		if (!readChar(&thisChar))
			return 0;
		if (!getNibble(thisChar, &lowNibble)) {
			goto getOut;
		}

		bytes[read] = (highNibble << 4) + lowNibble;
	}
getOut:
	if (thisChar != endChar) {
		skipUntilChar(endChar);
	}
	return read;
}

// Returns memory error
Error XMLDeserializer::readTagOrAttributeValueString(String* string) {

	Error error;

	switch (xmlArea) {
	case BETWEEN_TAGS:
		error = readStringUntilChar(string, '<');
		if (error == Error::NONE) {
			xmlArea = IN_TAG_NAME;
		}
		return error;

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		return readAttributeValueString(string);

	case IN_TAG_PAST_NAME: // Could happen if trying to read a value but instead of a value there are multiple more
	                       // contents, like attributes etc. Obviously not "meant" to happen, but we need to cope.
		return Error::FILE_CORRUPTED;

	default:
		if (ALPHA_OR_BETA_VERSION) {
			FREEZE_WITH_ERROR("BBBB");
		}
		__builtin_unreachable();
	}
}

int32_t XMLDeserializer::getNumCharsRemainingInValue() {

	int32_t pos = fileReadBufferCurrentPos;
	while (pos < currentReadBufferEndPos && fileClusterBuffer[pos] != charAtEndOfValue) {
		pos++;
	}

	return pos - fileReadBufferCurrentPos;
}

// Returns whether we're all good to go
bool XMLDeserializer::prepareToReadTagOrAttributeValueOneCharAtATime() {
	switch (xmlArea) {

	case BETWEEN_TAGS:
		// xmlArea = IN_TAG_NAME; // How it'll be after reading all chars
		charAtEndOfValue = '<';
		return true;

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		return getIntoAttributeValue();

	default:
		if (ALPHA_OR_BETA_VERSION) {
			FREEZE_WITH_ERROR("CCCC");
		}
		__builtin_unreachable();
	}
}

void XMLDeserializer::exitTag(char const* exitTagName) {
	// back out the file depth to one less than the caller depth
	while (tagDepthFile >= tagDepthCaller) {

		if (reachedBufferEnd) {
			return;
		}

		switch (xmlArea) {

		case IN_ATTRIBUTE_VALUE: // Could get left in here after a char-at-a-time read
			skipUntilChar(charAtEndOfValue);
			xmlArea = IN_TAG_PAST_NAME;
			// No break

		case IN_TAG_PAST_NAME:
			readNextAttributeName();
			break;

		case PAST_ATTRIBUTE_NAME:
		case PAST_EQUALS_SIGN:
			readAttributeValue();
			break;

		case BETWEEN_TAGS:
			skipUntilChar('<');
			xmlArea = IN_TAG_NAME;
			// Got to next tag start
			// No break

		case IN_TAG_NAME:
			readTagName();
			break;

		default:
			if (ALPHA_OR_BETA_VERSION) {
				FREEZE_WITH_ERROR("AAAA"); // Really shouldn't be possible anymore, I feel fairly certain...
			}
			__builtin_unreachable();
		}
	}
	// It is possible for caller and file tag depths to get out of sync due to faulty error handling
	// On exit reset the caller depth to match tag depth. File depth represents the parsers view of
	// where we are in the xml parsing, caller depth represents the callers view. The caller can be shallower
	// as the file will open past empty or unused tags, but should never be deeper.
	tagDepthCaller = tagDepthFile;
}

Error XMLDeserializer::openXMLFile(FilePointer* filePointer, char const* firstTagName, char const* altTagName,
                                   bool ignoreIncorrectFirmware) {

	AudioEngine::logAction("openXMLFile");

	reset();

	char const* tagName;

	while (*(tagName = readNextTagOrAttributeName())) {

		if (!strcmp(tagName, firstTagName) || !strcmp(tagName, altTagName)) {
			return Error::NONE;
		}

		Error result = tryReadingFirmwareTagFromFile(tagName, ignoreIncorrectFirmware);
		if (result != Error::NONE && result != Error::RESULT_TAG_UNUSED) {
			return result;
		}
		exitTag(tagName);
	}

	f_close(&readFIL);
	return Error::FILE_CORRUPTED;
}

Error XMLDeserializer::tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware) {

	if (!strcmp(tagName, "firmwareVersion")) {
		char const* firmware_version_string = readTagOrAttributeValue();
		firmware_version = FirmwareVersion::parse(firmware_version_string);
	}

	// If this tag doesn't exist, it's from old firmware so is ok
	else if (!strcmp(tagName, "earliestCompatibleFirmware")) {
		char const* firmware_version_string = readTagOrAttributeValue();
		auto earliestFirmware = FirmwareVersion::parse(firmware_version_string);
		if (earliestFirmware > FirmwareVersion::current() && !ignoreIncorrectFirmware) {
			f_close(&readFIL);
			return Error::FILE_FIRMWARE_VERSION_TOO_NEW;
		}
	}

	else {
		return Error::RESULT_TAG_UNUSED;
	}

	return Error::NONE;
}
