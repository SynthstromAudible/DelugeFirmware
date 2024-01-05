#include "deluge/storage/storage_manager.h"

#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/print.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/playback_mode.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "util/functions.h"

#include "CppUTest/TestHarness.h"

StorageManager storageManager{};

StorageManager::StorageManager() {
	fileClusterBuffer = NULL;

	devVarA = 150;
	devVarB = 8;
	devVarC = 100;
	devVarD = 60;
	devVarE = 60;
	devVarF = 40;
	devVarG = 0;
}

void StorageManager::writeTag(char const* tag, int32_t number) {
	char* buffer = shortStringBuffer;
	intToString(number, buffer);
	writeTag(tag, buffer);
}

void StorageManager::writeTag(char const* tag, char const* contents) {

	printIndents();
	write("<");
	write(tag);
	write(">");
	write(contents);
	write("</");
	write(tag);
	write(">\n");
}

void StorageManager::writeAttribute(char const* name, int32_t number, bool onNewLine) {

	char buffer[12];
	intToString(number, buffer);

	writeAttribute(name, buffer, onNewLine);
}

// numChars may be up to 8
void StorageManager::writeAttributeHex(char const* name, int32_t number, int32_t numChars, bool onNewLine) {

	char buffer[11];
	buffer[0] = '0';
	buffer[1] = 'x';
	intToHex(number, &buffer[2], numChars);

	writeAttribute(name, buffer, onNewLine);
}

void StorageManager::writeAttribute(char const* name, char const* value, bool onNewLine) {

	if (onNewLine) {
		write("\n");
		printIndents();
	}
	else {
		write(" ");
	}
	write(name);
	write("=\"");
	write(value);
	write("\"");
}

void StorageManager::writeOpeningTag(char const* tag, bool startNewLineAfter) {
	writeOpeningTagBeginning(tag);
	writeOpeningTagEnd(startNewLineAfter);
}

void StorageManager::writeOpeningTagBeginning(char const* tag) {
	printIndents();
	write("<");
	write(tag);
	indentAmount++;
}

void StorageManager::closeTag() {
	write(" /");
	writeOpeningTagEnd();
	indentAmount--;
}

void StorageManager::writeOpeningTagEnd(bool startNewLineAfter) {
	if (startNewLineAfter) {
		write(">\n");
	}
	else {
		write(">");
	}
}

void StorageManager::writeClosingTag(char const* tag, bool shouldPrintIndents) {
	indentAmount--;
	if (shouldPrintIndents) {
		printIndents();
	}
	write("</");
	write(tag);
	write(">\n");
}

void StorageManager::printIndents() {
	for (int32_t i = 0; i < indentAmount; i++) {
		write("\t");
	}
}

char stringBuffer[kFilenameBufferSize] __attribute__((aligned(CACHE_LINE_SIZE)));

#define BETWEEN_TAGS 0
#define IN_TAG_NAME 1
#define IN_TAG_PAST_NAME 2
#define IN_ATTRIBUTE_NAME 3
#define PAST_ATTRIBUTE_NAME 4
#define PAST_EQUALS_SIGN 5
#define IN_ATTRIBUTE_VALUE 6

// Only call this if IN_TAG_NAME
// TODO: this is very sub-optimal and still calls readCharXML()
char const* StorageManager::readTagName() {

	if (false) {
skipToNextTag:
		skipUntilChar('>');
		skipUntilChar('<');
	}

	char thisChar;
	int32_t charPos = 0;

	while (readCharXML(&thisChar)) {
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
	xmlReadDone();

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
char const* StorageManager::readNextAttributeName() {

	char thisChar;
	int32_t charPos = 0;

	while (readCharXML(&thisChar)) {
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

	// Here, we're in IN_ATTRIBUTE_NAME, and we're not allowed to leave this while loop until our xmlArea changes to something else
	// - or there's an error or file-end, in which case we'll return error below
doReadName:
	xmlArea = IN_ATTRIBUTE_NAME;
	tagDepthFile++;
	fileBufferCurrentPos--; // This means we don't need to call readXMLFileClusterIfNecessary()

	bool haveReachedNameEnd = false;

	// This is basically copied and tweaked from readUntilChar()
	do {
		int32_t bufferPosAtStart = fileBufferCurrentPos;
		while (fileBufferCurrentPos < currentReadBufferEndPos) {
			char thisChar = fileClusterBuffer[fileBufferCurrentPos];

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

			// If we get a close-tag name, it means we saw some sorta attribute name with no value, which isn't allowed, so treat it as invalid
			case '>':
				xmlArea = BETWEEN_TAGS;
				goto noMoreAttributes;

				// TODO: a '/' should get us outta here too...
			}

			fileBufferCurrentPos++;
		}

		if (false) {
reachedNameEnd:
			xmlReadDone();
			haveReachedNameEnd = true;
			// If possible, just return a pointer to the chars within the existing buffer
			if (!charPos && fileBufferCurrentPos < currentReadBufferEndPos) {
				fileClusterBuffer[fileBufferCurrentPos] = 0; // NULL end of the string we're returning
				fileBufferCurrentPos++;                      // Gets us past the endChar
				return &fileClusterBuffer[bufferPosAtStart];
			}
		}

		int32_t numCharsHere = fileBufferCurrentPos - bufferPosAtStart;
		int32_t numCharsToCopy = std::min<int32_t>(numCharsHere, kFilenameBufferSize - 1 - charPos);

		if (numCharsToCopy > 0) {
			memcpy(&stringBuffer[charPos], &fileClusterBuffer[bufferPosAtStart], numCharsToCopy);

			charPos += numCharsToCopy;
		}

		if (haveReachedNameEnd) {
			stringBuffer[charPos] = 0;
			fileBufferCurrentPos++; // Gets us past the endChar
			return stringBuffer;
		}

	} while (fileBufferCurrentPos == currentReadBufferEndPos && readXMLFileClusterIfNecessary());

	// If here, file ended
	return "";
}

char charAtEndOfValue;

char const* StorageManager::readNextTagOrAttributeName() {

	char const* toReturn;
	int32_t tagDepthStart = tagDepthFile;

	switch (xmlArea) {

	default:
#if ALPHA_OR_BETA_VERSION
		// Can happen with invalid files, though I'm implementing error checks whenever a user alerts me to a scenario. Fraser got this, Nov 2021.
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
    		Debug::print("\t");
    	}
    	Debug::println(toReturn);
		*/
		tagDepthCaller++;
		AudioEngine::logAction(toReturn);
	}

	return toReturn;
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_EQUALS_SIGN
// Returns the quote character that opens the value - or 0 if fail
bool StorageManager::getIntoAttributeValue() {

	char thisChar;

	switch (xmlArea) {
	case PAST_ATTRIBUTE_NAME:
		while (readCharXML(&thisChar)) {
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
		while (readCharXML(&thisChar)) {
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
char const* StorageManager::readAttributeValue() {

	if (!getIntoAttributeValue()) {
		return "";
	}
	xmlArea = IN_TAG_PAST_NAME; // How it'll be after this next call
	return readUntilChar(charAtEndOfValue);
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_EQUALS_SIGN
int32_t StorageManager::readAttributeValueInt() {

	if (!getIntoAttributeValue()) {
		return 0;
	}
	xmlArea = IN_TAG_PAST_NAME; // How it'll be after this next call
	return readIntUntilChar(charAtEndOfValue);
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_EQUALS_SIGN
int32_t StorageManager::readAttributeValueString(String* string) {

	if (!getIntoAttributeValue()) {
		string->clear();
		return NO_ERROR;
	}
	else {
		int32_t error = readStringUntilChar(string, charAtEndOfValue);
		if (!error) {
			xmlArea = IN_TAG_PAST_NAME;
		}
		return error;
	}
}

void StorageManager::xmlReadDone() {
	// Nothing to do in the mock version -- the real version tries to keep the UI and audio engine ticking along but we
	// don't care in tests.
}

void StorageManager::skipUntilChar(char endChar) {

	readXMLFileClusterIfNecessary(); // Does this need to be here? Originally I didn't have it...

	do {
		while (fileBufferCurrentPos < currentReadBufferEndPos && fileClusterBuffer[fileBufferCurrentPos] != endChar) {
			fileBufferCurrentPos++;
		}

	} while (fileBufferCurrentPos == currentReadBufferEndPos && readXMLFileClusterIfNecessary());

	fileBufferCurrentPos++; // Gets us past the endChar

	xmlReadDone();
}

// Returns memory error. If error, caller must deal with the fact that the end-character hasn't been reached
int32_t StorageManager::readStringUntilChar(String* string, char endChar) {

	int32_t newStringPos = 0;

	do {
		int32_t bufferPosNow = fileBufferCurrentPos;
		while (bufferPosNow < currentReadBufferEndPos && fileClusterBuffer[bufferPosNow] != endChar) {
			bufferPosNow++;
		}

		int32_t numCharsHere = bufferPosNow - fileBufferCurrentPos;

		if (numCharsHere) {
			int32_t error =
			    string->concatenateAtPos(&fileClusterBuffer[fileBufferCurrentPos], newStringPos, numCharsHere);

			fileBufferCurrentPos = bufferPosNow;

			if (error) {
				return error;
			}

			newStringPos += numCharsHere;
		}

	} while (fileBufferCurrentPos == currentReadBufferEndPos && readXMLFileClusterIfNecessary());

	fileBufferCurrentPos++; // Gets us past the endChar

	xmlReadDone();
	return NO_ERROR;
}

char const* StorageManager::readUntilChar(char endChar) {
	int32_t charPos = 0;

	do {
		int32_t bufferPosAtStart = fileBufferCurrentPos;
		while (fileBufferCurrentPos < currentReadBufferEndPos && fileClusterBuffer[fileBufferCurrentPos] != endChar) {
			fileBufferCurrentPos++;
		}

		// If possible, just return a pointer to the chars within the existing buffer
		if (!charPos && fileBufferCurrentPos < currentReadBufferEndPos) {
			fileClusterBuffer[fileBufferCurrentPos] = 0;

			fileBufferCurrentPos++; // Gets us past the endChar
			return &fileClusterBuffer[bufferPosAtStart];
		}

		int32_t numCharsHere = fileBufferCurrentPos - bufferPosAtStart;
		int32_t numCharsToCopy = std::min<int32_t>(numCharsHere, kFilenameBufferSize - 1 - charPos);

		if (numCharsToCopy > 0) {
			memcpy(&stringBuffer[charPos], &fileClusterBuffer[bufferPosAtStart], numCharsToCopy);

			charPos += numCharsToCopy;
		}

	} while (fileBufferCurrentPos == currentReadBufferEndPos && readXMLFileClusterIfNecessary());

	fileBufferCurrentPos++; // Gets us past the endChar

	xmlReadDone();

	stringBuffer[charPos] = 0;
	return stringBuffer;
}

// Unlike readUntilChar(), above, does not put a null character at the end of the returned "string". And, has a preset number of chars.
// And, returns NULL when nothing more to return.
// numChars must be <= FILENAME_BUFFER_SIZE
char const* StorageManager::readNextCharsOfTagOrAttributeValue(int32_t numChars) {

	int32_t charPos = 0;

	do {
		int32_t bufferPosAtStart = fileBufferCurrentPos;
		int32_t bufferPosAtEnd = bufferPosAtStart + numChars - charPos;

		int32_t currentReadBufferEndPosNow = std::min<int32_t>(currentReadBufferEndPos, bufferPosAtEnd);

		while (fileBufferCurrentPos < currentReadBufferEndPosNow) {
			if (fileClusterBuffer[fileBufferCurrentPos] == charAtEndOfValue) {
				goto reachedEndCharEarly;
			}
			fileBufferCurrentPos++;
		}

		int32_t numCharsHere = fileBufferCurrentPos - bufferPosAtStart;

		// If we were able to just read the whole thing in one go, just return a pointer to the chars within the existing buffer
		if (numCharsHere == numChars) {
			xmlReadDone();
			return &fileClusterBuffer[bufferPosAtStart];
		}

		// Otherwise, so long as we read something, add it to our buffer we're putting the output in
		if (numCharsHere > 0) {
			memcpy(&stringBuffer[charPos], &fileClusterBuffer[bufferPosAtStart], numCharsHere);

			charPos += numCharsHere;

			// And if we've now got all the chars we needed, return
			if (charPos == numChars) {
				xmlReadDone();
				return stringBuffer;
			}
		}

	} while (fileBufferCurrentPos == currentReadBufferEndPos && readXMLFileClusterIfNecessary());

	// If we're here, the file ended
	return NULL;

	// And, additional bit we jump to when end-char reached
reachedEndCharEarly:
	fileBufferCurrentPos++; // Gets us past the endChar
	if (charAtEndOfValue == '<') {
		xmlArea = IN_TAG_NAME;
	}
	else {
		xmlArea = IN_TAG_PAST_NAME; // Could be ' or "
	}
	return NULL;
}

// This is almost never called now - TODO: get rid
char StorageManager::readNextCharOfTagOrAttributeValue() {

	char thisChar;
	if (!readCharXML(&thisChar)) {
		return 0;
	}
	if (thisChar == charAtEndOfValue) {
		if (charAtEndOfValue == '<') {
			xmlArea = IN_TAG_NAME;
		}
		else {
			xmlArea = IN_TAG_PAST_NAME; // Could be ' or "
		}
		xmlReadDone();
		return 0;
	}
	return thisChar;
}

// Will always skip up until the end-char, even if it doesn't like the contents it sees
int32_t StorageManager::readIntUntilChar(char endChar) {
	uint32_t number = 0;
	char thisChar;

	if (!readCharXML(&thisChar)) {
		return 0;
	}

	bool isNegative = (thisChar == '-');
	if (!isNegative) {
		goto readDigit;
	}

	while (readCharXML(&thisChar)) {
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

char const* StorageManager::readTagOrAttributeValue() {

	switch (xmlArea) {

	case BETWEEN_TAGS:
		xmlArea = IN_TAG_NAME; // How it'll be after this call
		return readUntilChar('<');

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		return readAttributeValue();

	case IN_TAG_PAST_NAME: // Could happen if trying to read a value but instead of a value there are multiple more contents, like attributes etc. Obviously not "meant" to happen, but we need to cope.
		return "";

	default:
		FREEZE_WITH_ERROR("BBBB");
		__builtin_unreachable();
	}
}

int32_t StorageManager::readTagOrAttributeValueInt() {

	switch (xmlArea) {

	case BETWEEN_TAGS:
		xmlArea = IN_TAG_NAME; // How it'll be after this call
		return readIntUntilChar('<');

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		return readAttributeValueInt();

	case IN_TAG_PAST_NAME: // Could happen if trying to read a value but instead of a value there are multiple more contents, like attributes etc. Obviously not "meant" to happen, but we need to cope.
		return 0;

	default:
		FREEZE_WITH_ERROR("BBBB");
		__builtin_unreachable();
	}
}

// This isn't super optimal, like the int32_t version is, but only rarely used
int32_t StorageManager::readTagOrAttributeValueHex(int32_t errorValue) {
	char const* string = readTagOrAttributeValue();
	if (string[0] != '0' || string[1] != 'x') {
		return errorValue;
	}
	return hexToInt(&string[2]);
}

// Returns memory error
int32_t StorageManager::readTagOrAttributeValueString(String* string) {

	int32_t error;

	switch (xmlArea) {
	case BETWEEN_TAGS:
		error = readStringUntilChar(string, '<');
		if (!error) {
			xmlArea = IN_TAG_NAME;
		}
		return error;

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		return readAttributeValueString(string);

	case IN_TAG_PAST_NAME: // Could happen if trying to read a value but instead of a value there are multiple more contents, like attributes etc. Obviously not "meant" to happen, but we need to cope.
		return ERROR_FILE_CORRUPTED;

	default:
		if (ALPHA_OR_BETA_VERSION) {
			FREEZE_WITH_ERROR("BBBB");
		}
		__builtin_unreachable();
	}
}

int32_t StorageManager::getNumCharsRemainingInValue() {

	int32_t pos = fileBufferCurrentPos;
	while (pos < currentReadBufferEndPos && fileClusterBuffer[pos] != charAtEndOfValue) {
		pos++;
	}

	return pos - fileBufferCurrentPos;
}

// Returns whether we're all good to go
bool StorageManager::prepareToReadTagOrAttributeValueOneCharAtATime() {
	switch (xmlArea) {

	case BETWEEN_TAGS:
		//xmlArea = IN_TAG_NAME; // How it'll be after reading all chars
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

// Returns whether successful loading took place
bool StorageManager::readXMLFileClusterIfNecessary() {
	// XXX: for tests/mocks this should be a noop, we should just load the whole file at once

	// Load next Cluster if necessary
	if (fileBufferCurrentPos >= audioFileManager.clusterSize) {
		xmlReadCount = 0;
		bool result = readXMLFileCluster();
		if (!result) {
			xmlReachedEnd = true;
		}
		return result;
	}

	// Watch out for end of file
	if (fileBufferCurrentPos >= currentReadBufferEndPos) {
		xmlReachedEnd = true;
	}

	return false;
}

uint32_t StorageManager::readCharXML(char* thisChar) {

	bool stillGoing = readXMLFileClusterIfNecessary();
	if (xmlReachedEnd) {
		return 0;
	}

	*thisChar = fileClusterBuffer[fileBufferCurrentPos];

	fileBufferCurrentPos++;

	return 1;
}

void StorageManager::exitTag(char const* exitTagName) {

	while (tagDepthFile >= tagDepthCaller) {

		if (xmlReachedEnd) {
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

	tagDepthCaller--;
}

void StorageManager::readMidiCommand(uint8_t* channel, uint8_t* note) {
	char const* tagName;
	while (*(tagName = readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "channel")) {
			*channel = readTagOrAttributeValueInt();
			*channel = std::min(*channel, (uint8_t)15);
			exitTag("channel");
		}
		else if (!strcmp(tagName, "note")) {
			if (note != NULL) {
				*note = readTagOrAttributeValueInt();
				*note = std::min(*note, (uint8_t)127);
			}
			exitTag("note");
		}
	}
}

int32_t StorageManager::checkSpaceOnCard() {
	// TODO: allow mocking this
	return NO_ERROR;
}

// Creates folders and subfolders as needed!
int32_t StorageManager::createFile(FIL* file, char const* filePath, bool mayOverwrite) {
	FAIL("not yet implemented");

	return NO_ERROR;
}

int32_t StorageManager::createXMLFile(char const* filePath, bool mayOverwrite) {
	FAIL("not yet implemented");

	return NO_ERROR;
}

bool StorageManager::fileExists(char const* pathName) {
	FAIL("not yet implemented");
	return true;
}

// Lets you get the FilePointer for the file.
bool StorageManager::fileExists(char const* pathName, FilePointer* fp) {
	FAIL("not yet implemented");
	return true;
}

// TODO: replace with writes to a sink we can query in tests
void StorageManager::write(char const* output) {
	FAIL("not yet implemented");
}

int32_t StorageManager::writeBufferToFile() {
	// TODO: implement some suitable mock
	FAIL("Not implemented");

	return NO_ERROR;
}

// Returns false if some error, including error while writing
int32_t StorageManager::closeFileAfterWriting(char const* path, char const* beginningString, char const* endString) {
	FAIL("not yet implemented");

	return NO_ERROR;
}

bool StorageManager::lseek(uint32_t pos) {
	FAIL("not yet implemented");
	return true;
}

int32_t StorageManager::openXMLFile(FilePointer* filePointer, char const* firstTagName, char const* altTagName,
                                    bool ignoreIncorrectFirmware) {
	FAIL("not yet implemented");

	return true;
}

int32_t StorageManager::tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware) {

	if (!strcmp(tagName, "firmwareVersion")) {
		char const* firmwareVersionString = readTagOrAttributeValue();
		firmwareVersionOfFileBeingRead = stringToFirmwareVersion(firmwareVersionString);
	}

	else if (!strcmp(tagName,
	                 "earliestCompatibleFirmware")) { // If this tag doesn't exist, it's from old firmware so is ok
		char const* firmwareVersionString = readTagOrAttributeValue();
		int32_t earliestFirmware = stringToFirmwareVersion(firmwareVersionString);
		if (earliestFirmware > kCurrentFirmwareVersion && !ignoreIncorrectFirmware) {
			FAIL("Firmware version too new, and not ignoring incorrecet firmware");
			return ERROR_FILE_FIRMWARE_VERSION_TOO_NEW;
		}
	}

	else {
		return RESULT_TAG_UNUSED;
	}

	return NO_ERROR;
}

bool StorageManager::readXMLFileCluster() {
	// TODO: this should do work but doesn't for now, in the mock
	FAIL("Not yet implemented");
	return true;
}

// Returns false if some error, including error while writing
bool StorageManager::closeFile() {
	FAIL("Not yet implemented");
	return true;
}

void StorageManager::writeFirmwareVersion() {
	writeAttribute("firmwareVersion", "4.1.4-alpha");
}

void StorageManager::writeEarliestCompatibleFirmwareVersion(char const* versionString) {
	writeAttribute("earliestCompatibleFirmware", versionString);
}

int32_t StorageManager::initSD() {
	// Nothing to do in the mock (other than maybe assert it's called, or add tests for error handling)
	return NO_ERROR;
}

bool StorageManager::checkSDPresent() {
	// TODO: provide an interface for this so we can test it
	return true;
}

bool StorageManager::checkSDInitialized() {
	// TOOO: provide an interface for this so we can test it
	return true;
}

// Function can't fail.
void StorageManager::openFilePointer(FilePointer* fp) {
	FAIL("not yet implemented");
}

int32_t StorageManager::openInstrumentFile(InstrumentType instrumentType, FilePointer* filePointer) {

	AudioEngine::logAction("openInstrumentFile");
	if (!filePointer->sclust) {
		return ERROR_FILE_NOT_FOUND;
	}
	char const* firstTagName;
	char const* altTagName = "";

	if (instrumentType == InstrumentType::SYNTH) {
		firstTagName = "sound";
		altTagName = "synth"; // Compatibility with old xml files
	}
	else {
		firstTagName = "kit";
	}

	int32_t error = openXMLFile(filePointer, firstTagName, altTagName);
	return error;
}

// Returns error status
// clip may be NULL
int32_t StorageManager::loadInstrumentFromFile(Song* song, InstrumentClip* clip, InstrumentType instrumentType,
                                               bool mayReadSamplesFromFiles, Instrument** getInstrument,
                                               FilePointer* filePointer, String* name, String* dirPath) {

	AudioEngine::logAction("loadInstrumentFromFile");
	Debug::print("opening instrument file - ");
	Debug::print(dirPath->get());
	Debug::print(name->get());
	Debug::print(" from FP ");
	Debug::println((int32_t)filePointer->sclust);

	int32_t error = openInstrumentFile(instrumentType, filePointer);
	if (error) {
		Debug::print("opening instrument file failed - ");
		Debug::println(name->get());
		return error;
	}

	AudioEngine::logAction("loadInstrumentFromFile");
	Instrument* newInstrument = createNewInstrument(instrumentType);

	if (!newInstrument) {
		closeFile();
		Debug::print("Allocating instrument file failed - ");
		Debug::println(name->get());
		return ERROR_INSUFFICIENT_RAM;
	}

	error = newInstrument->readFromFile(song, clip, 0);

	bool fileSuccess = closeFile();

	// If that somehow didn't work...
	if (error || !fileSuccess) {
		Debug::print("reading instrument file failed - ");
		Debug::println(name->get());
		if (!fileSuccess) {
			error = ERROR_SD_CARD;
		}

deleteInstrumentAndGetOut:
		Debug::print("abandoning load - ");
		Debug::println(name->get());
		newInstrument->deleteBackedUpParamManagers(song);
		void* toDealloc = static_cast<void*>(newInstrument);
		newInstrument->~Instrument();
		delugeDealloc(toDealloc);

		return error;
	}

	// Check that a ParamManager was actually loaded for the Instrument, cos if not, that'd spell havoc
	if (!song->getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)newInstrument->toModControllable(),
	                                                     NULL)) {

		// Prior to V2.0 (or was it only in V1.0 on the 40-pad?) Kits didn't have anything that would have caused the paramManager to be created when we read the Kit just now.
		// So, just make one.
		if (firmwareVersionOfFileBeingRead < FIRMWARE_2P0P0_BETA && instrumentType == InstrumentType::KIT) {
			ParamManagerForTimeline paramManager;
			error = paramManager.setupUnpatched();
			if (error) {
				goto deleteInstrumentAndGetOut;
			}

			GlobalEffectableForClip::initParams(&paramManager);
			((Kit*)newInstrument)->compensateInstrumentVolumeForResonance(&paramManager, song); // Necessary?
			song->backUpParamManager(((Kit*)newInstrument), clip, &paramManager, true);
		}
		else {
paramManagersMissing:
			Debug::print("creating param manager failed - ");
			Debug::println(name->get());
			error = ERROR_FILE_CORRUPTED;
			goto deleteInstrumentAndGetOut;
		}
	}

	// For Kits, ensure that every audio Drum has a ParamManager somewhere
	if (newInstrument->type == InstrumentType::KIT) {
		Kit* kit = (Kit*)newInstrument;
		for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
			if (thisDrum->type == DrumType::SOUND) {
				SoundDrum* soundDrum = (SoundDrum*)thisDrum;
				if (!currentSong->getBackedUpParamManagerPreferablyWithClip(soundDrum,
				                                                            NULL)) { // If no backedUpParamManager...
					goto paramManagersMissing;
				}
			}
		}
	}

	newInstrument->name.set(name);
	newInstrument->dirPath.set(dirPath);

	newInstrument->loadAllAudioFiles(mayReadSamplesFromFiles); // Needs name, directory and slots set first, above.

	*getInstrument = newInstrument;
	return NO_ERROR;
}

/**
 * Special function to read a synth preset into a sound drum
*/
int32_t StorageManager::loadSynthToDrum(Song* song, InstrumentClip* clip, bool mayReadSamplesFromFiles,
                                        SoundDrum** getInstrument, FilePointer* filePointer, String* name,
                                        String* dirPath) {
	InstrumentType instrumentType = InstrumentType::SYNTH;
	SoundDrum* newDrum = (SoundDrum*)createNewDrum(DrumType::SOUND);
	if (!newDrum) {
		return ERROR_INSUFFICIENT_RAM;
	}

	AudioEngine::logAction("loadSynthDrumFromFile");

	int32_t error = openInstrumentFile(instrumentType, filePointer);
	if (error) {
		return error;
	}

	AudioEngine::logAction("loadInstrumentFromFile");

	error = newDrum->readFromFile(song, clip, 0);

	bool fileSuccess = closeFile();

	// If that somehow didn't work...
	if (error || !fileSuccess) {

		void* toDealloc = static_cast<void*>(newDrum);
		newDrum->~Drum();
		GeneralMemoryAllocator::get().dealloc(toDealloc);
		return error;

		if (!fileSuccess) {
			error = ERROR_SD_CARD;
			return error;
		}
	}
	//these have to get cleared, otherwise we keep creating drums that aren't attached to note rows
	if (*getInstrument) {
		song->deleteBackedUpParamManagersForModControllable(*getInstrument);
		(*getInstrument)->wontBeRenderedForAWhile();
		void* toDealloc = static_cast<void*>(*getInstrument);
		(*getInstrument)->~Drum();
		GeneralMemoryAllocator::get().dealloc(toDealloc);
	}

	*getInstrument = newDrum;
	return error;
}

// After calling this, you must make sure you set dirPath of Instrument.
Instrument* StorageManager::createNewInstrument(InstrumentType newInstrumentType, ParamManager* paramManager) {

	uint32_t instrumentSize;

	if (newInstrumentType == InstrumentType::SYNTH) {
		instrumentSize = sizeof(SoundInstrument);
	}

	// Kit
	else {
		instrumentSize = sizeof(Kit);
	}

	void* instrumentMemory = GeneralMemoryAllocator::get().allocMaxSpeed(instrumentSize);
	if (!instrumentMemory) {
		return NULL;
	}

	Instrument* newInstrument;

	int32_t error;

	// Synth
	if (newInstrumentType == InstrumentType::SYNTH) {
		if (paramManager) {
			error = paramManager->setupWithPatching();
			if (error) {
paramManagerSetupError:
				delugeDealloc(instrumentMemory);
				return NULL;
			}
			Sound::initParams(paramManager);
		}
		newInstrument = new (instrumentMemory) SoundInstrument();
	}

	// Kit
	else {
		if (paramManager) {
			error = paramManager->setupUnpatched();
			if (error) {
				goto paramManagerSetupError;
			}

			GlobalEffectableForClip::initParams(paramManager);
		}
		newInstrument = new (instrumentMemory) Kit();
	}

	return newInstrument;
}

Instrument* StorageManager::createNewNonAudioInstrument(InstrumentType instrumentType, int32_t slot, int32_t subSlot) {
	int32_t size = (instrumentType == InstrumentType::MIDI_OUT) ? sizeof(MIDIInstrument) : sizeof(CVInstrument);
	// Paul: Might make sense to put these into Internal?
	void* instrumentMemory = GeneralMemoryAllocator::get().allocLowSpeed(size);
	if (!instrumentMemory) { // RAM fail
		return NULL;
	}

	NonAudioInstrument* newInstrument;

	if (instrumentType == InstrumentType::MIDI_OUT) {
		newInstrument = new (instrumentMemory) MIDIInstrument();
		((MIDIInstrument*)newInstrument)->channelSuffix = subSlot;
	}
	else {
		newInstrument = new (instrumentMemory) CVInstrument();
	}
	newInstrument->channel = slot;

	return newInstrument;
}

Drum* StorageManager::createNewDrum(DrumType drumType) {
	int32_t memorySize;
	if (drumType == DrumType::SOUND) {
		memorySize = sizeof(SoundDrum);
	}
	else if (drumType == DrumType::MIDI) {
		memorySize = sizeof(MIDIDrum);
	}
	else if (drumType == DrumType::GATE) {
		memorySize = sizeof(GateDrum);
	}

	void* drumMemory = GeneralMemoryAllocator::get().allocMaxSpeed(memorySize);
	if (!drumMemory) {
		return NULL;
	}

	Drum* newDrum;
	if (drumType == DrumType::SOUND)
		newDrum = new (drumMemory) SoundDrum();
	else if (drumType == DrumType::MIDI)
		newDrum = new (drumMemory) MIDIDrum();
	else if (drumType == DrumType::GATE)
		newDrum = new (drumMemory) GateDrum();

	return newDrum;
}

// This has now mostly been replaced by an equivalent-ish function in InstrumentClip.
// Now, this will only ever be called in two scenarios:
// -- Pre-V2.0 files, so we know there's no mention of bend or aftertouch in this case where we have a ParamManager.
// -- When reading a MIDIInstrument, so we know there's no ParamManager (I checked), so no need to actually read the param.
int32_t StorageManager::readMIDIParamFromFile(int32_t readAutomationUpToPos, MIDIParamCollection* midiParamCollection,
                                              int8_t* getCC) {

	char const* tagName;
	int32_t cc = CC_NUMBER_NONE;

	while (*(tagName = readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "cc")) {
			char const* contents = readTagOrAttributeValue();
			if (!strcasecmp(contents, "bend")) {
				cc = CC_NUMBER_PITCH_BEND;
			}
			else if (!strcasecmp(contents, "aftertouch")) {
				cc = CC_NUMBER_AFTERTOUCH;
			}
			else if (!strcasecmp(contents, "none")
			         || !strcmp(contents, "120")) { // We used to write 120 for "none", pre V2.0
				cc = CC_NUMBER_NONE;
			}
			else {
				cc = stringToInt(contents);
			}
			// TODO: Pre-V2.0 files could still have CC74, so ideally I'd move that to "expression" params here...
			exitTag("cc");
		}
		else if (!strcmp(tagName, "value")) {
			if (cc != CC_NUMBER_NONE && midiParamCollection) {

				MIDIParam* midiParam = midiParamCollection->params.getOrCreateParamFromCC(cc, 0);
				if (!midiParam) {
					return ERROR_INSUFFICIENT_RAM;
				}

				int32_t error = midiParam->param.readFromFile(readAutomationUpToPos);
				if (error) {
					return error;
				}
			}
			exitTag("value");
		}
		else {
			exitTag(tagName);
		}
	}

	if (getCC) {
		*getCC = cc;
	}

	return NO_ERROR;
}

// For a bunch of params like this, e.g. for syncing delay, LFOs, arps, the value stored in the file is relative to the song insideWorldTickMagnitude -
// so that if someone loads a preset into a song with a different insideWorldTickMagnitude, the results are what you'd expect.
SyncType StorageManager::readSyncTypeFromFile(Song* song) {
	return (SyncType)readTagOrAttributeValueInt();
}

void StorageManager::writeSyncTypeToFile(Song* song, char const* name, SyncType value, bool onNewLine) {
	writeAttribute(name, (int32_t)value, onNewLine);
}

SyncLevel StorageManager::readAbsoluteSyncLevelFromFile(Song* song) {
	return (SyncLevel)song->convertSyncLevelFromFileValueToInternalValue(readTagOrAttributeValueInt());
}

void StorageManager::writeAbsoluteSyncLevelToFile(Song* song, char const* name, SyncLevel internalValue,
                                                  bool onNewLine) {
	writeAttribute(name, song->convertSyncLevelFromInternalValueToFileValue(internalValue), onNewLine);
}
