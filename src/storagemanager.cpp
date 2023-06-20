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

#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <Cluster.h>
#include <drivers/RZA1/gpio/gpio.h>
#include <InstrumentClip.h>
#include <ParamManager.h>
#include <sounddrum.h>
#include <soundinstrument.h>
#include "storagemanager.h"
#include "functions.h"
#include "song.h"
#include "numericdriver.h"
#include "uart.h"
#include "CVEngine.h"
#include "instrument.h"
#include "song.h"
#include "midiengine.h"
#include <string.h>
#include "PlaybackMode.h"
#include "kit.h"
#include "GeneralMemoryAllocator.h"
#include "MIDIInstrument.h"
#include "CVInstrument.h"
#include "MIDIParam.h"
#include "MIDIDrum.h"
#include "GateDrum.h"
#include "loadsongui.h"
#include <new>
#include "matrixdriver.h"
#include "soundeditor.h"
#include "MenuItemColour.h"
#include "PadLEDs.h"
#include "Buttons.h"
#include "MIDIParamCollection.h"
#include "uitimermanager.h"

extern "C" {
#include "ff.h"
#include "diskio.h"
#include "sio_char.h"
#include "oled_low_level.h"

FRESULT f_readdir_get_filepointer(DIR* dp,      /* Pointer to the open directory object */
                                  FILINFO* fno, /* Pointer to file information to return */
                                  FilePointer* filePointer);

void routineForSD(void);
}

StorageManager storageManager;
FILINFO staticFNO;
DIR staticDIR;

extern void initialiseConditions();
extern void songLoaded(Song* song);

// Because FATFS and FIL objects store buffers for SD read data to be read to via DMA, we have to space them apart from any other data
// so that invalidation and stuff works
struct FileSystemStuff fileSystemStuff;

StorageManager::StorageManager() {
	fileClusterBuffer = NULL;

	devVarA = 100;
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
void StorageManager::writeAttributeHex(char const* name, int32_t number, int numChars, bool onNewLine) {

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
	if (startNewLineAfter) write(">\n");
	else write(">");
}

void StorageManager::writeClosingTag(char const* tag, bool shouldPrintIndents) {
	indentAmount--;
	if (shouldPrintIndents) printIndents();
	write("</");
	write(tag);
	write(">\n");
}

void StorageManager::printIndents() {
	for (int i = 0; i < indentAmount; i++) {
		write("\t");
	}
}

char stringBuffer[FILENAME_BUFFER_SIZE] __attribute__((aligned(CACHE_LINE_SIZE)));

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
	int charPos = 0;

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
			if (charPos < FILENAME_BUFFER_SIZE - 1) {
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
	int charPos = 0;

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
		int bufferPosAtStart = fileBufferCurrentPos;
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

		int numCharsHere = fileBufferCurrentPos - bufferPosAtStart;
		int numCharsToCopy = getMin(numCharsHere, FILENAME_BUFFER_SIZE - 1 - charPos);

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
	int tagDepthStart = tagDepthFile;

	switch (xmlArea) {

	default:
#if ALPHA_OR_BETA_VERSION
		numericDriver.freezeWithError(
		    "E365"); // Can happen with invalid files, though I'm implementing error checks whenever a user alerts me to a scenario. Fraser got this, Nov 2021.
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
		if (*toReturn || tagDepthFile != tagDepthStart) break;
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
    	for (int t = 0; t < tagDepthCaller; t++) {
    		Uart::print("\t");
    	}
    	Uart::println(toReturn);
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

	if (!getIntoAttributeValue()) return "";
	xmlArea = IN_TAG_PAST_NAME; // How it'll be after this next call
	return readUntilChar(charAtEndOfValue);
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_EQUALS_SIGN
int32_t StorageManager::readAttributeValueInt() {

	if (!getIntoAttributeValue()) return 0;
	xmlArea = IN_TAG_PAST_NAME; // How it'll be after this next call
	return readIntUntilChar(charAtEndOfValue);
}

// Only call if PAST_ATTRIBUTE_NAME or PAST_EQUALS_SIGN
int StorageManager::readAttributeValueString(String* string) {

	if (!getIntoAttributeValue()) {
		string->clear();
		return NO_ERROR;
	}
	else {
		int error = readStringUntilChar(string, charAtEndOfValue);
		if (!error) xmlArea = IN_TAG_PAST_NAME;
		return error;
	}
}

void StorageManager::xmlReadDone() {
	xmlReadCount++; // Increment first, cos we don't want to call SD routine immediately when it's 0

	if (!(xmlReadCount & 63)) { // 511 bad. 255 almost fine. 127 almost always fine
		AudioEngine::routineWithClusterLoading();

		uiTimerManager.routine();

#if HAVE_OLED
		oledRoutine();
#endif
		uartFlushIfNotSending(UART_ITEM_PIC);
	}
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
int StorageManager::readStringUntilChar(String* string, char endChar) {

	int newStringPos = 0;

	do {
		int bufferPosNow = fileBufferCurrentPos;
		while (bufferPosNow < currentReadBufferEndPos && fileClusterBuffer[bufferPosNow] != endChar) {
			bufferPosNow++;
		}

		int numCharsHere = bufferPosNow - fileBufferCurrentPos;

		if (numCharsHere) {
			int error = string->concatenateAtPos(&fileClusterBuffer[fileBufferCurrentPos], newStringPos, numCharsHere);

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
	int charPos = 0;

	do {
		int bufferPosAtStart = fileBufferCurrentPos;
		while (fileBufferCurrentPos < currentReadBufferEndPos && fileClusterBuffer[fileBufferCurrentPos] != endChar) {
			fileBufferCurrentPos++;
		}

		// If possible, just return a pointer to the chars within the existing buffer
		if (!charPos && fileBufferCurrentPos < currentReadBufferEndPos) {
			fileClusterBuffer[fileBufferCurrentPos] = 0;

			fileBufferCurrentPos++; // Gets us past the endChar
			return &fileClusterBuffer[bufferPosAtStart];
		}

		int numCharsHere = fileBufferCurrentPos - bufferPosAtStart;
		int numCharsToCopy = getMin(numCharsHere, FILENAME_BUFFER_SIZE - 1 - charPos);

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
char const* StorageManager::readNextCharsOfTagOrAttributeValue(int numChars) {

	int charPos = 0;

	do {
		int bufferPosAtStart = fileBufferCurrentPos;
		int bufferPosAtEnd = bufferPosAtStart + numChars - charPos;

		int currentReadBufferEndPosNow = getMin(currentReadBufferEndPos, bufferPosAtEnd);

		while (fileBufferCurrentPos < currentReadBufferEndPosNow) {
			if (fileClusterBuffer[fileBufferCurrentPos] == charAtEndOfValue) {
				goto reachedEndCharEarly;
			}
			fileBufferCurrentPos++;
		}

		int numCharsHere = fileBufferCurrentPos - bufferPosAtStart;

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
	if (charAtEndOfValue == '<') xmlArea = IN_TAG_NAME;
	else xmlArea = IN_TAG_PAST_NAME; // Could be ' or "
	return NULL;
}

// This is almost never called now - TODO: get rid
char StorageManager::readNextCharOfTagOrAttributeValue() {

	char thisChar;
	if (!readCharXML(&thisChar)) return 0;
	if (thisChar == charAtEndOfValue) {
		if (charAtEndOfValue == '<') xmlArea = IN_TAG_NAME;
		else xmlArea = IN_TAG_PAST_NAME; // Could be ' or "
		xmlReadDone();
		return 0;
	}
	return thisChar;
}

// Will always skip up until the end-char, even if it doesn't like the contents it sees
int32_t StorageManager::readIntUntilChar(char endChar) {
	uint32_t number = 0;
	char thisChar;

	if (!readCharXML(&thisChar)) return 0;

	bool isNegative = (thisChar == '-');
	if (!isNegative) goto readDigit;

	while (readCharXML(&thisChar)) {
readDigit:
		if (!(thisChar >= '0' && thisChar <= '9')) goto getOut;
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
		if (number >= 2147483648) return -2147483648;
		else return -(int32_t)number;
	}
	else return number;
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
		numericDriver.freezeWithError("BBBB");
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
		numericDriver.freezeWithError("BBBB");
		__builtin_unreachable();
	}
}

// This isn't super optimal, like the int version is, but only rarely used
int32_t StorageManager::readTagOrAttributeValueHex(int errorValue) {
	char const* string = readTagOrAttributeValue();
	if (string[0] != '0' || string[1] != 'x') return errorValue;
	return hexToInt(&string[2]);
}

// Returns memory error
int StorageManager::readTagOrAttributeValueString(String* string) {

	int error;

	switch (xmlArea) {
	case BETWEEN_TAGS:
		error = readStringUntilChar(string, '<');
		if (!error) xmlArea = IN_TAG_NAME;
		return error;

	case PAST_ATTRIBUTE_NAME:
	case PAST_EQUALS_SIGN:
		return readAttributeValueString(string);

	case IN_TAG_PAST_NAME: // Could happen if trying to read a value but instead of a value there are multiple more contents, like attributes etc. Obviously not "meant" to happen, but we need to cope.
		return ERROR_FILE_CORRUPTED;

	default:
		if (ALPHA_OR_BETA_VERSION) numericDriver.freezeWithError("BBBB");
		__builtin_unreachable();
	}
}

int StorageManager::getNumCharsRemainingInValue() {

	int pos = fileBufferCurrentPos;
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
		if (ALPHA_OR_BETA_VERSION) numericDriver.freezeWithError("CCCC");
		__builtin_unreachable();
	}
}

// Returns whether successful loading took place
bool StorageManager::readXMLFileClusterIfNecessary() {

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
	if (xmlReachedEnd) return 0;

	*thisChar = fileClusterBuffer[fileBufferCurrentPos];

	fileBufferCurrentPos++;

	return 1;
}

void StorageManager::exitTag(char const* exitTagName) {

	while (tagDepthFile >= tagDepthCaller) {

		if (xmlReachedEnd) return;

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
			if (ALPHA_OR_BETA_VERSION)
				numericDriver.freezeWithError("AAAA"); // Really shouldn't be possible anymore, I feel fairly certain...
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
			*channel = getMin(*channel, (uint8_t)15);
			exitTag("channel");
		}
		else if (!strcmp(tagName, "note")) {
			if (note != NULL) {
				*note = readTagOrAttributeValueInt();
				*note = getMin(*note, (uint8_t)127);
			}
			exitTag("note");
		}
	}
}

int StorageManager::checkSpaceOnCard() {
	Uart::print("free clusters: ");
	Uart::println(fileSystemStuff.fileSystem.free_clst);
	return fileSystemStuff.fileSystem.free_clst ? NO_ERROR
	                                            : ERROR_SD_CARD_FULL; // This doesn't seem to always be 100% accurate...
}

// Creates folders and subfolders as needed!
int StorageManager::createFile(FIL* file, char const* filePath, bool mayOverwrite) {

	int error = initSD();
	if (error) return error;

	error = checkSpaceOnCard();
	if (error) return error;

	bool triedCreatingFolder = false;

	BYTE mode = FA_WRITE;
	if (mayOverwrite) mode |= FA_CREATE_ALWAYS;
	else mode |= FA_CREATE_NEW;

tryAgain:
	FRESULT result = f_open(file, filePath, mode);

	if (result != FR_OK) {

processError:
		// If folder doesn't exist, try creating it - once only
		if (result == FR_NO_PATH) {
			if (triedCreatingFolder) return ERROR_FOLDER_DOESNT_EXIST;
			triedCreatingFolder = true;

			String folderPath;
			error = folderPath.set(filePath);
			if (error) return error;

			// Get just the folder path
cutFolderPathAndTryCreating:
			char const* folderPathChars = folderPath.get();
			char const* slashAddr = strrchr(folderPathChars, '/');
			if (!slashAddr) return ERROR_UNSPECIFIED; // Shouldn't happen
			int slashPos = (uint32_t)slashAddr - (uint32_t)folderPathChars;

			error = folderPath.shorten(slashPos);
			if (error) return error;

			// Try making the folder
			result = f_mkdir(folderPath.get());
			if (result == FR_OK) goto tryAgain;
			else if (result
			         == FR_NO_PATH) { // If that folder couldn't be created because its parent folder didn't exist...
				triedCreatingFolder = false;      // Let it do multiple tries again
				goto cutFolderPathAndTryCreating; // Go and try creating the parent folder
			}
			else goto processError;
		}

		// Otherwise, just return the appropriate error.
		else {
			error = fresultToDelugeErrorCode(result);
			if (error == ERROR_SD_CARD)
				error = ERROR_WRITE_FAIL; // Get a bit more specific if we only got the most general error.
			return error;
		}
	}

	return NO_ERROR;
}

int StorageManager::createXMLFile(char const* filePath, bool mayOverwrite) {

	int error = createFile(&fileSystemStuff.currentFile, filePath, mayOverwrite);
	if (error) return error;

	fileBufferCurrentPos = 0;
	fileTotalBytesWritten = 0;
	fileAccessFailedDuring = false;

	write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");

	indentAmount = 0;

	return NO_ERROR;
}

bool StorageManager::fileExists(char const* pathName) {
	int error = initSD();
	if (error) return false;

	FRESULT result = f_stat(pathName, &staticFNO);
	return (result == FR_OK);
}

// Lets you get the FilePointer for the file.
bool StorageManager::fileExists(char const* pathName, FilePointer* fp) {
	int error = initSD();
	if (error) return false;

	FRESULT result = f_open(&fileSystemStuff.currentFile, pathName, FA_READ);
	if (result != FR_OK) return false;

	fp->sclust = fileSystemStuff.currentFile.obj.sclust;
	fp->objsize = fileSystemStuff.currentFile.obj.objsize;

	f_close(&fileSystemStuff.currentFile);
	return true;
}

// TODO: this is really inefficient
void StorageManager::write(char const* output) {

	while (*output) {

		if (fileBufferCurrentPos == audioFileManager.clusterSize) {

			if (!fileAccessFailedDuring) {
				int error = writeBufferToFile();
				if (error) {
					fileAccessFailedDuring = true;
					return;
				}
			}

			fileBufferCurrentPos = 0;
		}

		fileClusterBuffer[fileBufferCurrentPos] = *output;

		output++;
		fileBufferCurrentPos++;

		// Ensure we do some of the audio routine once in a while
		if (!(fileBufferCurrentPos & 0b11111111)) {
			AudioEngine::logAction("writeCharXML");

			AudioEngine::routineWithClusterLoading();

			uiTimerManager.routine();

#if HAVE_OLED
			oledRoutine();
#endif
			uartFlushIfNotSending(UART_ITEM_PIC);
		}
	}
}

int StorageManager::writeBufferToFile() {
	UINT bytesWritten;
	FRESULT result = f_write(&fileSystemStuff.currentFile, fileClusterBuffer, fileBufferCurrentPos, &bytesWritten);
	if (result != FR_OK || bytesWritten != fileBufferCurrentPos) {
		return ERROR_SD_CARD;
	}

	fileTotalBytesWritten += fileBufferCurrentPos;

	return NO_ERROR;
}

// Returns false if some error, including error while writing
int StorageManager::closeFileAfterWriting(char const* path, char const* beginningString, char const* endString) {
	if (fileAccessFailedDuring)
		return ERROR_WRITE_FAIL; // Calling f_close if this is false might be dangerous - if access has failed, we don't want it to flush any data to the card or anything
	int error = writeBufferToFile();
	if (error) return ERROR_WRITE_FAIL;

	FRESULT result = f_close(&fileSystemStuff.currentFile);
	if (result) return ERROR_WRITE_FAIL;

	if (path) {
		// Check file exists
		result = f_open(&fileSystemStuff.currentFile, path, FA_READ);
		if (result) return ERROR_WRITE_FAIL;
	}

	// Check size
	if (f_size(&fileSystemStuff.currentFile) != fileTotalBytesWritten) return ERROR_WRITE_FAIL;

	// Check beginning
	if (beginningString) {
		UINT dontCare;
		int length = strlen(beginningString);
		result = f_read(&fileSystemStuff.currentFile, miscStringBuffer, length, &dontCare);
		if (result) return ERROR_WRITE_FAIL;
		if (memcmp(miscStringBuffer, beginningString, length)) return ERROR_WRITE_FAIL;
	}

	// Check end
	if (endString) {
		UINT dontCare;
		int length = strlen(endString);

		result = f_lseek(&fileSystemStuff.currentFile, fileTotalBytesWritten - length);
		if (result) return ERROR_WRITE_FAIL;

		result = f_read(&fileSystemStuff.currentFile, miscStringBuffer, length, &dontCare);
		if (result) return ERROR_WRITE_FAIL;
		if (memcmp(miscStringBuffer, endString, length)) return ERROR_WRITE_FAIL;
	}

	result = f_close(&fileSystemStuff.currentFile);
	if (result) return ERROR_WRITE_FAIL;

	return NO_ERROR;
}

bool StorageManager::lseek(uint32_t pos) {
	FRESULT result = f_lseek(&fileSystemStuff.currentFile, pos);
	if (result != FR_OK) {
		fileAccessFailedDuring = true;
	}

	return (result == FR_OK);
}

int StorageManager::openXMLFile(FilePointer* filePointer, char const* firstTagName, char const* altTagName,
                                bool ignoreIncorrectFirmware) {

	AudioEngine::logAction("openXMLFile");

	openFilePointer(filePointer);

	// Prep to read first Cluster shortly
	fileBufferCurrentPos = audioFileManager.clusterSize;
	currentReadBufferEndPos = audioFileManager.clusterSize;

	firmwareVersionOfFileBeingRead = FIRMWARE_OLD;

	tagDepthFile = 0;
	tagDepthCaller = 0;
	xmlReachedEnd = false;
	xmlArea = BETWEEN_TAGS;

	char const* tagName;

	while (*(tagName = readNextTagOrAttributeName())) {

		if (!strcmp(tagName, firstTagName) || !strcmp(tagName, altTagName)) return NO_ERROR;

		int result = tryReadingFirmwareTagFromFile(tagName, ignoreIncorrectFirmware);
		if (result && result != RESULT_TAG_UNUSED) return result;
		exitTag(tagName);
	}

	f_close(&fileSystemStuff.currentFile);
	return ERROR_FILE_CORRUPTED;
}

int StorageManager::tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware) {

	if (!strcmp(tagName, "firmwareVersion")) {
		char const* firmwareVersionString = readTagOrAttributeValue();
		firmwareVersionOfFileBeingRead = stringToFirmwareVersion(firmwareVersionString);
	}

	else if (!strcmp(tagName,
	                 "earliestCompatibleFirmware")) { // If this tag doesn't exist, it's from old firmware so is ok
		char const* firmwareVersionString = readTagOrAttributeValue();
		int earliestFirmware = stringToFirmwareVersion(firmwareVersionString);
		if (earliestFirmware > CURRENT_FIRMWARE_VERSION && !ignoreIncorrectFirmware) {
			f_close(&fileSystemStuff.currentFile);
			return ERROR_FILE_FIRMWARE_VERSION_TOO_NEW;
		}
	}

	else return RESULT_TAG_UNUSED;

	return NO_ERROR;
}

bool StorageManager::readXMLFileCluster() {

	AudioEngine::logAction("readXMLFileCluster");

	FRESULT result = f_read(&fileSystemStuff.currentFile, (UINT*)fileClusterBuffer, audioFileManager.clusterSize,
	                        &currentReadBufferEndPos);
	if (result) {
		fileAccessFailedDuring = true;
		return false;
	}

	// If error or we reached end of file
	if (!currentReadBufferEndPos) return false;

	fileBufferCurrentPos = 0;

	return true;
}

// Returns false if some error, including error while writing
bool StorageManager::closeFile() {
	if (fileAccessFailedDuring)
		return false; // Calling f_close if this is false might be dangerous - if access has failed, we don't want it to flush any data to the card or anything
	FRESULT result = f_close(&fileSystemStuff.currentFile);
	return (result == FR_OK);
}

void StorageManager::writeFirmwareVersion() {
	writeAttribute("firmwareVersion", "4.1.4-alpha");
}

void StorageManager::writeEarliestCompatibleFirmwareVersion(char const* versionString) {
	writeAttribute("earliestCompatibleFirmware", versionString);
}

// Gets ready to access SD card.
// You should call this before you're gonna do any accessing - otherwise any errors won't reflect if there's in fact just no card inserted.
int StorageManager::initSD() {

	FRESULT result;

	// If we know the SD card is still initialised, no need to actually initialise
	DSTATUS status = disk_status(SD_PORT);
	if ((status & STA_NOINIT) == 0) {
		return NO_ERROR;
	}

	// But if there's no card present, we're in trouble
	if (status & STA_NODISK) {
		return ERROR_SD_CARD_NOT_PRESENT;
	}

	// Otherwise, we can mount the filesystem...
	result = f_mount(&fileSystemStuff.fileSystem, "", 1);

	return fresultToDelugeErrorCode(result);
}

bool StorageManager::checkSDPresent() {
	DSTATUS status = disk_status(SD_PORT);
	bool present = !(status & STA_NODISK);
	return present;
}

bool StorageManager::checkSDInitialized() {
	DSTATUS status = disk_status(SD_PORT);
	return !(status & STA_NOINIT);
}

// Function can't fail.
void StorageManager::openFilePointer(FilePointer* fp) {

	AudioEngine::logAction("openFilePointer");

	Uart::println("openFilePointer");

	fileSystemStuff.currentFile.obj.sclust = fp->sclust;
	fileSystemStuff.currentFile.obj.objsize = fp->objsize;
	fileSystemStuff.currentFile.obj.fs = &fileSystemStuff.fileSystem; /* Validate the file object */
	fileSystemStuff.currentFile.obj.id = fileSystemStuff.fileSystem.id;

	fileSystemStuff.currentFile.flag = FA_READ; /* Set file access mode */
	fileSystemStuff.currentFile.err = 0;        /* Clear error flag */
	fileSystemStuff.currentFile.sect = 0;       /* Invalidate current data sector */
	fileSystemStuff.currentFile.fptr = 0;       /* Set file pointer top of the file */

	fileAccessFailedDuring = false;
}

int StorageManager::openInstrumentFile(int instrumentType, FilePointer* filePointer) {

	AudioEngine::logAction("openInstrumentFile");

	char const* firstTagName;
	char const* altTagName = "";

	if (instrumentType == INSTRUMENT_TYPE_SYNTH) {
		firstTagName = "sound";
		altTagName = "synth"; // Compatibility with old xml files
	}
	else {
		firstTagName = "kit";
	}

	int error = openXMLFile(filePointer, firstTagName, altTagName);
	return error;
}

// Returns error status
// clip may be NULL
int StorageManager::loadInstrumentFromFile(Song* song, InstrumentClip* clip, int instrumentType,
                                           bool mayReadSamplesFromFiles, Instrument** getInstrument,
                                           FilePointer* filePointer, String* name, String* dirPath) {

	AudioEngine::logAction("loadInstrumentFromFile");

	int error = openInstrumentFile(instrumentType, filePointer);
	if (error) return error;

	AudioEngine::logAction("loadInstrumentFromFile");
	Instrument* newInstrument = createNewInstrument(instrumentType);

	if (!newInstrument) {
		closeFile();
		return ERROR_INSUFFICIENT_RAM;
	}

	error = newInstrument->readFromFile(song, clip, 0);

	bool fileSuccess = closeFile();

	// If that somehow didn't work...
	if (error || !fileSuccess) {

		if (!fileSuccess) error = ERROR_SD_CARD;

deleteInstrumentAndGetOut:
		newInstrument->deleteBackedUpParamManagers(song);
		void* toDealloc = dynamic_cast<void*>(newInstrument);
		newInstrument->~Instrument();
		generalMemoryAllocator.dealloc(toDealloc);

		return error;
	}

	// Check that a ParamManager was actually loaded for the Instrument, cos if not, that'd spell havoc
	if (!song->getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)newInstrument->toModControllable(),
	                                                     NULL)) {

		// Prior to V2.0 (or was it only in V1.0 on the 40-pad?) Kits didn't have anything that would have caused the paramManager to be created when we read the Kit just now.
		// So, just make one.
		if (firmwareVersionOfFileBeingRead < FIRMWARE_2P0P0_BETA && instrumentType == INSTRUMENT_TYPE_KIT) {
			ParamManagerForTimeline paramManager;
			error = paramManager.setupUnpatched();
			if (error) goto deleteInstrumentAndGetOut;

			GlobalEffectableForClip::initParams(&paramManager);
			((Kit*)newInstrument)->compensateInstrumentVolumeForResonance(&paramManager, song); // Necessary?
			song->backUpParamManager(((Kit*)newInstrument), clip, &paramManager, true);
		}
		else {
paramManagersMissing:
			error = ERROR_FILE_CORRUPTED;
			goto deleteInstrumentAndGetOut;
		}
	}

	// For Kits, ensure that every audio Drum has a ParamManager somewhere
	if (newInstrument->type == INSTRUMENT_TYPE_KIT) {
		Kit* kit = (Kit*)newInstrument;
		for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {
			if (thisDrum->type == DRUM_TYPE_SOUND) {
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

// After calling this, you must make sure you set dirPath of Instrument.
Instrument* StorageManager::createNewInstrument(uint8_t newInstrumentType, ParamManager* paramManager) {

	uint32_t instrumentSize;

	if (newInstrumentType == INSTRUMENT_TYPE_SYNTH) {
		instrumentSize = sizeof(SoundInstrument);
	}

	// Kit
	else {
		instrumentSize = sizeof(Kit);
	}

	void* instrumentMemory = generalMemoryAllocator.alloc(instrumentSize, NULL, false, true);
	if (!instrumentMemory) {
		return NULL;
	}

	Instrument* newInstrument;

	int error;

	// Synth
	if (newInstrumentType == INSTRUMENT_TYPE_SYNTH) {
		if (paramManager) {
			error = paramManager->setupWithPatching();
			if (error) {
paramManagerSetupError:
				generalMemoryAllocator.dealloc(instrumentMemory);
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
			if (error) goto paramManagerSetupError;

			GlobalEffectableForClip::initParams(paramManager);
		}
		newInstrument = new (instrumentMemory) Kit();
	}

	return newInstrument;
}

Instrument* StorageManager::createNewNonAudioInstrument(int instrumentType, int slot, int subSlot) {
	int size = (instrumentType == INSTRUMENT_TYPE_MIDI_OUT) ? sizeof(MIDIInstrument) : sizeof(CVInstrument);
	void* instrumentMemory = generalMemoryAllocator.alloc(size);
	if (!instrumentMemory) { // RAM fail
		return NULL;
	}

	NonAudioInstrument* newInstrument;

	if (instrumentType == INSTRUMENT_TYPE_MIDI_OUT) {
		newInstrument = new (instrumentMemory) MIDIInstrument();
		((MIDIInstrument*)newInstrument)->channelSuffix = subSlot;
	}
	else {
		newInstrument = new (instrumentMemory) CVInstrument();
	}
	newInstrument->channel = slot;

	return newInstrument;
}

Drum* StorageManager::createNewDrum(int drumType) {
	int memorySize;
	if (drumType == DRUM_TYPE_SOUND) memorySize = sizeof(SoundDrum);
	else if (drumType == DRUM_TYPE_MIDI) memorySize = sizeof(MIDIDrum);
	else if (drumType == DRUM_TYPE_GATE) memorySize = sizeof(GateDrum);

	void* drumMemory = generalMemoryAllocator.alloc(memorySize, NULL, false, true);
	if (!drumMemory) {
		return NULL;
	}

	Drum* newDrum;
	if (drumType == DRUM_TYPE_SOUND) newDrum = new (drumMemory) SoundDrum();
	else if (drumType == DRUM_TYPE_MIDI) newDrum = new (drumMemory) MIDIDrum();
	else if (drumType == DRUM_TYPE_GATE) newDrum = new (drumMemory) GateDrum();

	return newDrum;
}

// This has now mostly been replaced by an equivalent-ish function in InstrumentClip.
// Now, this will only ever be called in two scenarios:
// -- Pre-V2.0 files, so we know there's no mention of bend or aftertouch in this case where we have a ParamManager.
// -- When reading a MIDIInstrument, so we know there's no ParamManager (I checked), so no need to actually read the param.
int StorageManager::readMIDIParamFromFile(int32_t readAutomationUpToPos, MIDIParamCollection* midiParamCollection,
                                          int8_t* getCC) {

	char const* tagName;
	int cc = CC_NUMBER_NONE;

	while (*(tagName = readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "cc")) {
			char const* contents = readTagOrAttributeValue();
			if (!strcasecmp(contents, "bend")) cc = CC_NUMBER_PITCH_BEND;
			else if (!strcasecmp(contents, "aftertouch")) cc = CC_NUMBER_AFTERTOUCH;
			else if (!strcasecmp(contents, "none")
			         || !strcmp(contents, "120")) // We used to write 120 for "none", pre V2.0
				cc = CC_NUMBER_NONE;
			else cc = stringToInt(contents);
			// TODO: Pre-V2.0 files could still have CC74, so ideally I'd move that to "expression" params here...
			exitTag("cc");
		}
		else if (!strcmp(tagName, "value")) {
			if (cc != CC_NUMBER_NONE && midiParamCollection) {

				MIDIParam* midiParam = midiParamCollection->params.getOrCreateParamFromCC(cc, 0);
				if (!midiParam) return ERROR_INSUFFICIENT_RAM;

				int error = midiParam->param.readFromFile(readAutomationUpToPos);
				if (error) return error;
			}
			exitTag("value");
		}
		else exitTag(tagName);
	}

	if (getCC) *getCC = cc;

	return NO_ERROR;
}

// For a bunch of params like this, e.g. for syncing delay, LFOs, arps, the value stored in the file is relative to the song insideWorldTickMagnitude -
// so that if someone loads a preset into a song with a different insideWorldTickMagnitude, the results are what you'd expect.
SyncType StorageManager::readSyncTypeFromFile(Song* song) {
	return (SyncType)readTagOrAttributeValueInt();
}

void StorageManager::writeSyncTypeToFile(Song* song, char const* name, SyncType value, bool onNewLine) {
	writeAttribute(name, (int)value, onNewLine);
}

SyncLevel StorageManager::readAbsoluteSyncLevelFromFile(Song* song) {
	return (SyncLevel)song->convertSyncLevelFromFileValueToInternalValue(readTagOrAttributeValueInt());
}

void StorageManager::writeAbsoluteSyncLevelToFile(Song* song, char const* name, SyncLevel internalValue,
                                                  bool onNewLine) {
	writeAttribute(name, song->convertSyncLevelFromInternalValueToFileValue(internalValue), onNewLine);
}
