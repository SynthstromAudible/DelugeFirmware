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

#ifndef STORAGEMANAGER_H
#define STORAGEMANAGER_H

#include "definitions.h"
#include "r_typedefs.h"

extern "C" {
#include "fatfs/ff.h"
}

extern void deleteOldSongBeforeLoadingNew();

struct FileSystemStuff {
	FATFS fileSystem; /* File system object */
	FIL currentFile;  /* File object */
};

extern struct FileSystemStuff fileSystemStuff;

class Instrument;
class PlaybackMode;
class ParamManagerForTimeline;
class ArpeggiatorSettings;
class Song;
class InstrumentClip;
class ParamManagerMIDI;
class Drum;
class String;
class MIDIParamCollection;
class ParamManager;

class StorageManager {
public:
	StorageManager();

	void writeAttribute(char const* name, int32_t number, bool onNewLine = true);
	void writeAttribute(char const* name, char const* value, bool onNewLine = true);
	void writeAttributeHex(char const* name, int32_t number, int numChars, bool onNewLine = true);
	void writeTag(char const* tag, int32_t number);
	void writeTag(char const* tag, char const* contents);
	void writeOpeningTag(char const* tag, bool startNewLineAfter = true);
	void writeOpeningTagBeginning(char const* tag);
	void writeOpeningTagEnd(bool startNewLineAfter = true);
	void closeTag();
	void writeClosingTag(char const* tag, bool shouldPrintIndents = true);
	void printIndents();
	char const* readNextTagOrAttributeName();
	void exitTag(char const* exitTagName = NULL);
	char const* readTagOrAttributeValue();

	int createFile(FIL* file, char const* filePath, bool mayOverwrite);
	int createXMLFile(char const* pathName, bool mayOverwrite = false);
	int openXMLFile(FilePointer* filePointer, char const* firstTagName, char const* altTagName = "",
	                bool ignoreIncorrectFirmware = false);
	bool prepareToReadTagOrAttributeValueOneCharAtATime();
	char readNextCharOfTagOrAttributeValue();
	char const* readNextCharsOfTagOrAttributeValue(int numChars);
	void readMidiCommand(uint8_t* channel, uint8_t* note = NULL);
	int initSD();
	bool closeFile();
	int closeFileAfterWriting(char const* path = NULL, char const* beginningString = NULL,
	                          char const* endString = NULL);
	uint32_t readCharXML(char* thisChar);
	void write(char const* output);
	bool lseek(uint32_t pos);
	bool fileExists(char const* pathName);
	bool fileExists(char const* pathName, FilePointer* fp);
	int openInstrumentFile(int instrumentType, FilePointer* filePointer);
	void writeFirmwareVersion();
	bool checkSDPresent();
	bool checkSDInitialized();
	bool readXMLFileCluster();
	int getNumCharsRemainingInValue();
	Instrument* createNewInstrument(uint8_t newInstrumentType, ParamManager* getParamManager = NULL);
	int loadInstrumentFromFile(Song* song, InstrumentClip* clip, int instrumentType, bool mayReadSamplesFromFiles,
	                           Instrument** getInstrument, FilePointer* filePointer, String* name, String* dirPath);
	Instrument* createNewNonAudioInstrument(int instrumentType, int slot, int subSlot);
	void writeEarliestCompatibleFirmwareVersion(char const* versionString);
	int readMIDIParamFromFile(int32_t readAutomationUpToPos, MIDIParamCollection* midiParamCollection,
	                          int8_t* getCC = NULL);
	Drum* createNewDrum(int drumType);
	void openFilePointer(FilePointer* fp);
	int tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware = false);
	int32_t readTagOrAttributeValueInt();
	int32_t readTagOrAttributeValueHex(int errorValue);

	int readTagOrAttributeValueString(String* string);
	int checkSpaceOnCard();

	SyncType readSyncTypeFromFile(Song* song);
	void writeSyncTypeToFile(Song* song, char const* name, SyncType value, bool onNewLine = true);
	SyncLevel readAbsoluteSyncLevelFromFile(Song* song);
	void writeAbsoluteSyncLevelToFile(Song* song, char const* name, SyncLevel internalValue, bool onNewLine = true);

	bool fileAccessFailedDuring;

	int firmwareVersionOfFileBeingRead;

	char* fileClusterBuffer;
	UINT currentReadBufferEndPos;
	int fileBufferCurrentPos;

	int fileTotalBytesWritten;

	int devVarA;
	int devVarB;
	int devVarC;
	int devVarD;
	int devVarE;
	int devVarF;
	int devVarG;

private:
	uint8_t indentAmount;

	uint8_t xmlArea;
	bool xmlReachedEnd;
	int tagDepthCaller; // How deeply indented in XML the main Deluge classes think we are, as data being read.
	int tagDepthFile; // Will temporarily be different to the above as unwanted / unused XML tags parsed on the way to finding next useful data.
	int xmlReadCount;

	void skipUntilChar(char endChar);
	char const* readTagName();
	char const* readNextAttributeName();
	char const* readUntilChar(char endChar);
	char const* readAttributeValue();
	int32_t readIntUntilChar(char endChar);
	bool getIntoAttributeValue();
	int32_t readAttributeValueInt();
	bool readXMLFileClusterIfNecessary();
	int readStringUntilChar(String* string, char endChar);
	int readAttributeValueString(String* string);
	void restoreBackedUpCharIfNecessary();
	void xmlReadDone();

	int writeBufferToFile();
};

extern StorageManager storageManager;
extern FILINFO staticFNO;
extern DIR staticDIR;

#endif // STORAGEMANAGER_H
