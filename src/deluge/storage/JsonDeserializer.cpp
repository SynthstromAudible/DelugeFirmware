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

/*******************************************************************************

    JsonDeserializer

********************************************************************************/

JsonDeserializer::JsonDeserializer() {

	reset();
}

JsonDeserializer::JsonDeserializer(uint8_t* inbuf, size_t buflen) : FileDeserializer(inbuf, buflen) {
	reset();
}

void JsonDeserializer::reset() {
	resetReader();
	if (!memoryBased) {
		// Prep to read first Cluster shortly
		fileReadBufferCurrentPos = audioFileManager.clusterSize;
		currentReadBufferEndPos = audioFileManager.clusterSize;
	}
	song_firmware_version = FirmwareVersion{FirmwareVersion::Type::OFFICIAL, {}};

	objectDepth = 0;
	arrayDepth = 0;
	readState = JsonState::NewFile;
}

// Advances read pointer until it encounters a non-whitespace character.
// It leaves the read pointer at the non-whitespace character encountered.
// Returns true if a non-whitespace character was found, otherwise false
// if we are at the end.
bool JsonDeserializer::skipWhiteSpace(bool commasToo) {
	char thisChar;
	while (peekChar(&thisChar)) {
		if (thisChar == ' ' || thisChar == '\r' || thisChar == '\n' || thisChar == '\t'
		    || ((thisChar == ',') && commasToo)) {
			readChar(&thisChar);
		}
		else {
			return true;
		}
	}
	return false;
}

char JsonDeserializer::unescape(char inchar) {
	char thisChar;
	if (inchar != '\\')
		return inchar;
	if (!readChar(&thisChar))
		return 0;
	switch (thisChar) {
	case '"':
		thisChar = '"';
		break;
	case '\\':
		thisChar = '\\';
		break;
	default:
		break;
	}
	return thisChar;
}

// Returns a Json string, max length of kFilenameBufferSize, delimited by double quotes.
char const* JsonDeserializer::readQuotedString() {
	char thisChar;
	if (!skipWhiteSpace())
		return NULL;
	if (!peekChar(&thisChar))
		return NULL;
	if (thisChar != '\"')
		return NULL;
	readChar(&thisChar);
	int32_t charPos = 0;

	while (readChar(&thisChar)) {
		if (thisChar == '\"')
			goto getOut;
		thisChar = unescape(thisChar);
		if (thisChar == 0)
			goto getOut;
		// Store this character, if space in our un-ideal buffer
		if (charPos < kFilenameBufferSize - 1) {
			stringBuffer[charPos++] = thisChar;
		}
	}

getOut:
	readDone();
	stringBuffer[charPos] = 0;
	return stringBuffer;
}

// Read a Json key name, which must be quoted and followed with a colon.
// return an empty string if a proper key is not found.
char const* JsonDeserializer::readKeyName() {
	char const* key = readQuotedString();
	if (key == NULL)
		return "";
	if (!skipWhiteSpace())
		return "";
	char aChar;
	if (!readChar(&aChar))
		return "";
	if (aChar != ':')
		return "";
	readState = JsonState::KeyRead;
	return key;
}

char const* JsonDeserializer::readNextAttributeName() {
	if (!skipWhiteSpace(true))
		return "";
	char thisChar;
	if (!peekChar(&thisChar))
		return "";
	return readKeyName();
}

// In Json, the equivalent functionality is to read the next key name.
// Which is always "key": (with double quotes & colon)
char const* JsonDeserializer::readNextTagOrAttributeName() {
	return readKeyName();
}

// the stream index is left directly addressing the leading character of the value.
bool JsonDeserializer::getIntoAttributeValue() {
	if (!skipWhiteSpace())
		return 0;
	// valid characters to start a value are a digit, minus sign,
	// or a double quote. If it is double quote, skip past it.
	char thisChar;
	if (!peekChar(&thisChar))
		return false;
	if ((thisChar >= '0' && thisChar <= '9') || thisChar == '-') {
		return true;
	}
	if (thisChar == '"')
		readChar(&thisChar);
	return (thisChar == '"');
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_COLON
char const* JsonDeserializer::readAttributeValue() {

	if (!getIntoAttributeValue()) {
		return "";
	}
	return readUntilChar('"');
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_COLON
int32_t JsonDeserializer::readAttributeValueInt() {

	if (!getIntoAttributeValue()) {
		return 0;
	}
	return readInt();
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_COLON
Error JsonDeserializer::readAttributeValueString(String* string) {

	if (!getIntoAttributeValue()) {
		string->clear();
		return Error::NONE;
	}
	Error error = readStringUntilChar(string, '"');
	return error;
}

void JsonDeserializer::skipUntilChar(char endChar) {

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

// A non-destructive (to the fileClusterBuffer contents) routine to read into a String object.
// Returns memory error. If error, caller must deal with the fact that the end-character hasn't been reached.
Error JsonDeserializer::readStringUntilChar(String* string, char endChar) {

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
	readState = JsonState::ValueRead;
	return Error::NONE;
}

// Called when the buf index is pointed at the first char of the value
// (Past the leading double quote.)
// This version, like XMLDeserialize, pokes a null character over the memory
// location occupied by endChar. (If it can get away with that).
char const* JsonDeserializer::readUntilChar(char endChar) {
	int32_t charPos = 0;

	do {
		int32_t bufferPosAtStart = fileReadBufferCurrentPos;
		while (fileReadBufferCurrentPos < currentReadBufferEndPos
		       && fileClusterBuffer[fileReadBufferCurrentPos] != endChar) {
			fileReadBufferCurrentPos++;
		}
		readState = JsonState::ValueRead;
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
char const* JsonDeserializer::readNextCharsOfTagOrAttributeValue(int32_t numChars) {

	int32_t charPos = 0;

	do {
		int32_t bufferPosAtStart = fileReadBufferCurrentPos;
		int32_t bufferPosAtEnd = bufferPosAtStart + numChars - charPos;

		int32_t currentReadBufferEndPosNow = std::min<int32_t>(currentReadBufferEndPos, bufferPosAtEnd);

		while (fileReadBufferCurrentPos < currentReadBufferEndPosNow) {
			if (fileClusterBuffer[fileReadBufferCurrentPos] == '"') {
				goto reachedEndCharEarly;
			}
			fileReadBufferCurrentPos++;
		}

		int32_t numCharsHere = fileReadBufferCurrentPos - bufferPosAtStart;

		// If we were able to just read the whole thing in one go, just return a pointer to the chars within the
		// existing buffer
		if (numCharsHere == numChars) {
			readDone();
			readState = JsonState::ValueRead;
			return &fileClusterBuffer[bufferPosAtStart];
		}

		// Otherwise, so long as we read something, add it to our buffer we're putting the output in
		if (numCharsHere > 0) {
			memcpy(&stringBuffer[charPos], &fileClusterBuffer[bufferPosAtStart], numCharsHere);

			charPos += numCharsHere;

			// And if we've now got all the chars we needed, return
			if (charPos == numChars) {
				readDone();
				readState = JsonState::ValueRead;
				return stringBuffer;
			}
		}

	} while (fileReadBufferCurrentPos == currentReadBufferEndPos && readFileClusterIfNecessary());

	// If we're here, the file ended
	return NULL;

	// And, additional bit we jump to when end-char reached
reachedEndCharEarly:
	fileReadBufferCurrentPos++; // Gets us past the endChar

	return NULL;
}

// This is almost never called now - TODO: get rid
char JsonDeserializer::readNextCharOfTagOrAttributeValue() {

	char thisChar;
	if (!readChar(&thisChar)) {
		return 0;
	}
	return thisChar;
}

// Read an integer up until the first non-numeric character.
// Leaves the buff index pointing at that non-numeric character.
int32_t JsonDeserializer::readInt() {
	uint32_t number = 0;
	char thisChar;

	if (!skipWhiteSpace(false))
		return 0;
	bool isNegative = false;
	while (peekChar(&thisChar)) {
		if (thisChar == '-') {
			isNegative = true;
			readChar(&thisChar);
			continue;
		}
		if (!(thisChar >= '0' && thisChar <= '9')) {
			goto getOut;
		}
		readChar(&thisChar);
		number *= 10;
		number += (thisChar - '0');
	}
getOut:
	readState = JsonState::ValueRead;
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

char const* JsonDeserializer::readTagOrAttributeValue() {
	return readAttributeValue();
}

int32_t JsonDeserializer::readTagOrAttributeValueInt() {
	return readInt();
}

// This isn't super optimal, like the int32_t version is, but only rarely used
int32_t JsonDeserializer::readTagOrAttributeValueHex(int32_t errorValue) {
	char const* string = readTagOrAttributeValue();
	if (string[0] != '0' || string[1] != 'x') {
		return errorValue;
	}
	return hexToInt(&string[2]);
}

int JsonDeserializer::readTagOrAttributeValueHexBytes(uint8_t* bytes, int32_t maxLen) {

	if (!getIntoAttributeValue())
		return 0;
	return readHexBytesUntil(bytes, maxLen, '\"');
}

extern bool getNibble(char ch, int* nibble);

int JsonDeserializer::readHexBytesUntil(uint8_t* bytes, int32_t maxLen, char endChar) {
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
	readState = JsonState::ValueRead;
	return read;
}

// Returns memory error
Error JsonDeserializer::readTagOrAttributeValueString(String* string) {
	if (!skipWhiteSpace())
		return Error::FILE_CORRUPTED;
	skipUntilChar('\"');
	return readStringUntilChar(string, '\"');
}

int32_t JsonDeserializer::getNumCharsRemainingInValueBeforeEndOfCluster() {

	int32_t pos = fileReadBufferCurrentPos;
	while (pos < currentReadBufferEndPos && fileClusterBuffer[pos] != '"') {
		pos++;
	}

	return pos - fileReadBufferCurrentPos;
}

// Called before unusual attribute reading. In our case, get past
// the leading double quote.
bool JsonDeserializer::prepareToReadTagOrAttributeValueOneCharAtATime() {
	return getIntoAttributeValue();
}

// Used to match syntax for Json strings.
// Iff the current index matches the given character
// we then skip over that character and return true.
// else we return false and leave the index pointing at
// the first non-whitespace character after the incoming index.
bool JsonDeserializer::match(char const ch) {
	if (!skipWhiteSpace())
		return false;
	char nowChar, nextChar;
	if (peekChar(&nowChar)) {
		if (nowChar != ch)
			return false;
		readChar(&nowChar);
		switch (ch) {
		case '{':
			objectDepth++;
			break;
		case '}':
			objectDepth--;
			break;
		case '[':
			arrayDepth++;
			break;
		case ']':
			arrayDepth--;
			break;
		}
	}
	peekChar(&nextChar);
	return true;
}

void JsonDeserializer::exitTag(char const* exitTagName, bool closeObject) {
	if (closeObject) {
		match('}');
	}
	if (readState == JsonState::ValueRead)
		return;

	// We have a key/value pair where the key is not known.
	// Based on the key type, skip over the value(s).
	// Since the value could be an object or array, we need to skip forward until we hit the
	// matching closing character. This can involve counting open and close characters until
	// we get a match.
	D_PRINTLN("Unread value detected");
	readState == JsonState::ValueRead; // declare victory prematurely.
	skipWhiteSpace();
	char leadingChar, trailingChar, currentChar, balanceCtr = 1;
	readChar(&leadingChar);
	// Strings are easy.
	if (leadingChar == '"') {
		skipUntilChar('"');
		return;
	}
	if (leadingChar == '[')
		trailingChar = ']';
	else if (leadingChar == '{')
		trailingChar = '}';
	else if ((leadingChar == '-') || (leadingChar >= '0' && leadingChar <= '9')) {
		// The other possibility is a number
		readInt(); // skip the number.
		return;
	}
	else {
		D_PRINTLN("Malformed value encountered.");
	}

	while ((balanceCtr > 0) && readChar(&currentChar)) {
		if (currentChar == leadingChar)
			balanceCtr++;
		else if (currentChar == trailingChar)
			balanceCtr--;
	}
}

Error JsonDeserializer::openJsonFile(FilePointer* filePointer, char const* firstTagName, char const* altTagName,
                                     bool ignoreIncorrectFirmware) {

	AudioEngine::logAction("openJsonFile");

	reset();

	if (!match('{'))
		return Error::FILE_CORRUPTED;
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

	closeWriter();
	return Error::FILE_CORRUPTED;
}

Error JsonDeserializer::tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware) {

	if (!strcmp(tagName, "firmwareVersion")) {
		char const* firmware_version_string = readTagOrAttributeValue();
		song_firmware_version = FirmwareVersion::parse(firmware_version_string);
	}

	// If this tag doesn't exist, it's from old firmware so is ok
	else if (!strcmp(tagName, "earliestCompatibleFirmware")) {
		char const* firmware_version_string = readTagOrAttributeValue();
		auto earliestFirmware = FirmwareVersion::parse(firmware_version_string);
		if (earliestFirmware > FirmwareVersion::current() && !ignoreIncorrectFirmware) {
			closeWriter();
			return Error::FILE_FIRMWARE_VERSION_TOO_NEW;
		}
	}

	else {
		return Error::RESULT_TAG_UNUSED;
	}

	return Error::NONE;
}
