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
#include "fatfs/fatfs.hpp"
#include "model/sync.h"
#include "util/firmware_version.h"

#include <cstdint>
#include <optional>

extern "C" {
#include "fatfs/ff.h"
}

extern void deleteOldSongBeforeLoadingNew();

struct FileSystemStuff {
	FatFS::Filesystem fileSystem; /* File system object */
	FIL currentFile;              /* File object */
};

extern struct FileSystemStuff fileSystemStuff;

class Instrument;
class PlaybackMode;
class ParamManagerForTimeline;
class ArpeggiatorSettings;
class Song;
class InstrumentClip;
class Drum;
class String;
class MIDIParamCollection;
class ParamManager;
class SoundDrum;
class StorageManager;

class SMSharedData {};

class Serializer {
public:
	virtual void writeAttribute(char const* name, int32_t number, bool onNewLine = true) = 0;
	virtual void writeAttribute(char const* name, char const* value, bool onNewLine = true) = 0;
	virtual void writeAttributeHex(char const* name, int32_t number, int32_t numChars, bool onNewLine = true) = 0;
	virtual void writeAttributeHexBytes(char const* name, uint8_t* data, int32_t numBytes, bool onNewLine = true) = 0;

	virtual void writeTag(char const* tag, int32_t number) = 0;
	virtual void writeTag(char const* tag, char const* contents) = 0;
	virtual void writeOpeningTag(char const* tag, bool startNewLineAfter = true) = 0;
	virtual void writeOpeningTagBeginning(char const* tag) = 0;
	virtual void writeOpeningTagEnd(bool startNewLineAfter = true) = 0;
	virtual void closeTag() = 0;
	virtual void writeClosingTag(char const* tag, bool shouldPrintIndents = true) = 0;
	virtual void printIndents() = 0;
	virtual void write(char const* output) = 0;
	virtual Error closeFileAfterWriting(char const* path = nullptr, char const* beginningString = nullptr,
	                                    char const* endString = nullptr) = 0;

	void writeFirmwareVersion();

	void writeEarliestCompatibleFirmwareVersion(char const* versionString) {
		writeAttribute("earliestCompatibleFirmware", versionString);
	}

	void writeSyncTypeToFile(Song* song, char const* name, SyncType value, bool onNewLine) {
		writeAttribute(name, (int32_t)value, onNewLine);
	}

	void writeAbsoluteSyncLevelToFile(Song* song, char const* name, SyncLevel internalValue, bool onNewLine);
};

class XMLSerializer : public Serializer {
public:
	XMLSerializer();
	virtual ~XMLSerializer();

	void writeAttribute(char const* name, int32_t number, bool onNewLine = true) override;
	void writeAttribute(char const* name, char const* value, bool onNewLine = true) override;
	void writeAttributeHex(char const* name, int32_t number, int32_t numChars, bool onNewLine = true) override;
	void writeAttributeHexBytes(char const* name, uint8_t* data, int32_t numBytes, bool onNewLine = true);

	void writeTag(char const* tag, int32_t number) override;
	void writeTag(char const* tag, char const* contents) override;
	void writeOpeningTag(char const* tag, bool startNewLineAfter = true) override;
	void writeOpeningTagBeginning(char const* tag) override;
	void writeOpeningTagEnd(bool startNewLineAfter = true) override;
	void closeTag() override;
	void writeClosingTag(char const* tag, bool shouldPrintIndents = true) override;
	void printIndents() override;
	void write(char const* output) override;

	StorageManager* ms;

	// Private member variables for XML display and parsing:
public:
	char* writeClusterBuffer;
	uint8_t indentAmount;
	int32_t fileWriteBufferCurrentPos;
	int32_t fileTotalBytesWritten;
	bool fileAccessFailedDuringWrite;

	Error writeXMLBufferToFile();
	Error closeFileAfterWriting(char const* path = nullptr, char const* beginningString = nullptr,
	                            char const* endString = nullptr) override;
};

class Deserializer {
public:
	virtual bool prepareToReadTagOrAttributeValueOneCharAtATime() = 0;
	virtual char readNextCharOfTagOrAttributeValue() = 0;
	virtual int32_t getNumCharsRemainingInValue() = 0;

	virtual int readHexBytesUntil(uint8_t* bytes, int32_t maxLen, char endPos) = 0;
	virtual char const* readNextTagOrAttributeName() = 0;
	virtual char const* readTagOrAttributeValue() = 0;
	virtual int32_t readTagOrAttributeValueInt() = 0;
	virtual int32_t readTagOrAttributeValueHex(int32_t errorValue) = 0;
	virtual int readTagOrAttributeValueHexBytes(uint8_t* bytes, int32_t maxLen) = 0;

	virtual char const* readNextCharsOfTagOrAttributeValue(int32_t numChars) = 0;
	virtual Error readTagOrAttributeValueString(String* string) = 0;
	virtual void exitTag(char const* exitTagName = NULL) = 0;
};

class XMLDeserializer : public Deserializer {
public:
	XMLDeserializer();
	virtual ~XMLDeserializer();

	bool prepareToReadTagOrAttributeValueOneCharAtATime() override;
	char const* readNextTagOrAttributeName() override;
	char readNextCharOfTagOrAttributeValue() override;
	int32_t getNumCharsRemainingInValue() override;

	int32_t readTagOrAttributeValueInt() override;
	int32_t readTagOrAttributeValueHex(int32_t errorValue) override;
	int readTagOrAttributeValueHexBytes(uint8_t* bytes, int32_t maxLen) override;

	int readHexBytesUntil(uint8_t* bytes, int32_t maxLen, char endPos) override;
	char const* readNextCharsOfTagOrAttributeValue(int32_t numChars) override;
	Error readTagOrAttributeValueString(String* string) override;
	char const* readTagOrAttributeValue() override;
	void exitTag(char const* exitTagName = NULL) override;

	Error openXMLFile(FilePointer* filePointer, char const* firstTagName, char const* altTagName = "",
	                  bool ignoreIncorrectFirmware = false);

	StorageManager* msd;

public:
	UINT currentReadBufferEndPos;
	int32_t fileReadBufferCurrentPos;

	char* fileClusterBuffer; // This buffer is reused in various places outside of StorageManager proper.

	uint8_t xmlArea; // state variable for tokenizer
	bool xmlReachedEnd;
	int32_t tagDepthCaller; // How deeply indented in XML the main Deluge classes think we are, as data being read.
	int32_t tagDepthFile; // Will temporarily be different to the above as unwanted / unused XML tags parsed on the way
	                      // to finding next useful data.
	int32_t xmlReadCount;

	char stringBuffer[kFilenameBufferSize];

	FirmwareVersion firmware_version = FirmwareVersion::current();
	Error tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware = false);

	void skipUntilChar(char endChar);
	uint32_t readCharXML(char* thisChar);
	char const* readTagName();
	char const* readNextAttributeName();
	char const* readUntilChar(char endChar);
	char const* readAttributeValue();

	bool fileAccessFailedDuring;

	int32_t readIntUntilChar(char endChar);
	bool getIntoAttributeValue();
	int32_t readAttributeValueInt();
	bool readXMLFileClusterIfNecessary();
	Error readStringUntilChar(String* string, char endChar);
	Error readAttributeValueString(String* string);
	bool readXMLFileCluster();
	void xmlReadDone();
};

extern XMLSerializer smSerializer;
extern XMLDeserializer smDeserializer;

class StorageManager {
public:
	StorageManager();
	virtual ~StorageManager();

	std::expected<FatFS::File, Error> createFile(char const* filePath, bool mayOverwrite);
	Error createXMLFile(char const* pathName, XMLSerializer& writer, bool mayOverwrite = false,
	                    bool displayErrors = true);
	Error openXMLFile(FilePointer* filePointer, XMLDeserializer& reader, char const* firstTagName,
	                  char const* altTagName = "", bool ignoreIncorrectFirmware = false);

	Error initSD();
	bool closeFile();

	bool fileExists(char const* pathName);
	bool fileExists(char const* pathName, FilePointer* fp);

	bool checkSDPresent();
	bool checkSDInitialized();

	Instrument* createNewInstrument(OutputType newOutputType, ParamManager* getParamManager = NULL);
	Error loadInstrumentFromFile(Song* song, InstrumentClip* clip, OutputType outputType, bool mayReadSamplesFromFiles,
	                             Instrument** getInstrument, FilePointer* filePointer, String* name, String* dirPath);
	Instrument* createNewNonAudioInstrument(OutputType outputType, int32_t slot, int32_t subSlot);

	Drum* createNewDrum(DrumType drumType);
	Error loadSynthToDrum(Song* song, InstrumentClip* clip, bool mayReadSamplesFromFiles, SoundDrum** getInstrument,
	                      FilePointer* filePointer, String* name, String* dirPath);
	void openFilePointer(FilePointer* fp);

	Error checkSpaceOnCard();

	// ** Start of public member variables. These are used outside of StorageManager.

private:
	// ** End of member variables
	Error openInstrumentFile(OutputType outputType, FilePointer* filePointer);
};

extern StorageManager storageManager;
extern FILINFO staticFNO;
extern DIR staticDIR;

inline bool isCardReady() {
	return Error::NONE == storageManager.initSD();
}
