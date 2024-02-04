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

#include "definitions_cxx.hpp"
#include <cstdint>

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
class SoundDrum;

class StorageManager {
public:
	StorageManager();

	void writeAttribute(char const* name, int32_t number, bool onNewLine = true);
	void writeAttribute(char const* name, char const* value, bool onNewLine = true);
	void writeAttributeHex(char const* name, int32_t number, int32_t numChars, bool onNewLine = true);
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

	int32_t createFile(FIL* file, char const* filePath, bool mayOverwrite);
	int32_t createXMLFile(char const* pathName, bool mayOverwrite = false, bool displayErrors = true);
	int32_t openXMLFile(FilePointer* filePointer, char const* firstTagName, char const* altTagName = "",
	                    bool ignoreIncorrectFirmware = false);
	bool prepareToReadTagOrAttributeValueOneCharAtATime();
	char readNextCharOfTagOrAttributeValue();
	char const* readNextCharsOfTagOrAttributeValue(int32_t numChars);
	void readMidiCommand(uint8_t* channel, uint8_t* note = NULL);
	int32_t initSD();
	bool closeFile();
	int32_t closeFileAfterWriting(char const* path = NULL, char const* beginningString = NULL,
	                              char const* endString = NULL);
	uint32_t readCharXML(char* thisChar);
	void write(char const* output);
	void writef(char const* format, ...);
	bool lseek(uint32_t pos);
	bool fileExists(char const* pathName);
	bool fileExists(char const* pathName, FilePointer* fp);
	int32_t openInstrumentFile(OutputType outputType, FilePointer* filePointer);
	void writeFirmwareVersion();
	bool checkSDPresent();
	bool checkSDInitialized();
	bool readXMLFileCluster();
	int32_t getNumCharsRemainingInValue();
	Instrument* createNewInstrument(OutputType newOutputType, ParamManager* getParamManager = NULL);
	int32_t loadInstrumentFromFile(Song* song, InstrumentClip* clip, OutputType outputType,
	                               bool mayReadSamplesFromFiles, Instrument** getInstrument, FilePointer* filePointer,
	                               String* name, String* dirPath);
	Instrument* createNewNonAudioInstrument(OutputType outputType, int32_t slot, int32_t subSlot);
	void writeEarliestCompatibleFirmwareVersion(char const* versionString);
	int32_t readMIDIParamFromFile(int32_t readAutomationUpToPos, MIDIParamCollection* midiParamCollection,
	                              int8_t* getCC = NULL);
	Drum* createNewDrum(DrumType drumType);
	int32_t loadSynthToDrum(Song* song, InstrumentClip* clip, bool mayReadSamplesFromFiles, SoundDrum** getInstrument,
	                        FilePointer* filePointer, String* name, String* dirPath);
	void openFilePointer(FilePointer* fp);
	int32_t tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware = false);
	int32_t readTagOrAttributeValueInt();
	int32_t readTagOrAttributeValueHex(int32_t errorValue);

	int32_t readTagOrAttributeValueString(String* string);
	int32_t checkSpaceOnCard();

	SyncType readSyncTypeFromFile(Song* song);
	void writeSyncTypeToFile(Song* song, char const* name, SyncType value, bool onNewLine = true);
	SyncLevel readAbsoluteSyncLevelFromFile(Song* song);
	void writeAbsoluteSyncLevelToFile(Song* song, char const* name, SyncLevel internalValue, bool onNewLine = true);

	bool fileAccessFailedDuring;

	int32_t firmwareVersionOfFileBeingRead;

	char* fileClusterBuffer;
	UINT currentReadBufferEndPos;
	int32_t fileBufferCurrentPos;

	int32_t fileTotalBytesWritten;

	int32_t devVarA;
	int32_t devVarB;
	int32_t devVarC;
	int32_t devVarD;
	int32_t devVarE;
	int32_t devVarF;
	int32_t devVarG;

private:
	uint8_t indentAmount;

	uint8_t xmlArea;
	bool xmlReachedEnd;
	int32_t tagDepthCaller; // How deeply indented in XML the main Deluge classes think we are, as data being read.
	int32_t tagDepthFile; // Will temporarily be different to the above as unwanted / unused XML tags parsed on the way
	                      // to finding next useful data.
	int32_t xmlReadCount;

	void skipUntilChar(char endChar);
	char const* readTagName();
	char const* readNextAttributeName();
	char const* readUntilChar(char endChar);
	char const* readAttributeValue();
	int32_t readIntUntilChar(char endChar);
	bool getIntoAttributeValue();
	int32_t readAttributeValueInt();
	bool readXMLFileClusterIfNecessary();
	int32_t readStringUntilChar(String* string, char endChar);
	int32_t readAttributeValueString(String* string);
	void restoreBackedUpCharIfNecessary();
	void xmlReadDone();

	int32_t writeBufferToFile();
};

extern StorageManager storageManager;
extern FILINFO staticFNO;
extern DIR staticDIR;
