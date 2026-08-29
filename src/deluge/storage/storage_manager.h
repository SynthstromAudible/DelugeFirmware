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
#include "extern.h"
#include "io/file.hpp"
#include "libdeluge/scheduler.h" // isSDRoutineActive()
#include "model/sync.h"
#include "util/firmware_version.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>

extern void deleteOldSongBeforeLoadingNew();

class Instrument;
class PlaybackMode;
class ParamManagerForTimeline;
class ArpeggiatorSettings;
class Song;
class InstrumentClip;
class Drum;
#include <string>
class MIDIParamCollection;
class ParamManager;
class SoundDrum;
class FileItem;
class MIDIInstrument;

class SMSharedData {};

class FileReader {
public:
	FileReader();
	FileReader(char* memBuffer, uint32_t bufLen);
	virtual ~FileReader();

	std::optional<deluge::io::File> file;
	char* fileClusterBuffer;
	uint32_t currentReadBufferEndPos{};
	int32_t fileReadBufferCurrentPos{};

	/// @return true on success, false on failure.
	bool closeWriter();

	bool peekChar(char* thisChar);
	bool readChar(char* thisChar);

	/// @brief Whether the reader has run out of bytes to hand out.
	///
	/// For a file-backed reader that means end of file. For a memory-backed one it means the end of
	/// the supplied buffer, which is NOT the same claim: the buffer may be a prefix of a longer file,
	/// so a walk that stops here has been cut short rather than having seen everything there is.
	[[nodiscard]] bool reachedEnd() const { return reachedBufferEnd; }

	uint32_t bytesRemainingInBuffer() { return currentReadBufferEndPos - fileReadBufferCurrentPos; }
	char* GetCurrentAddressInBuffer() { return fileClusterBuffer + fileReadBufferCurrentPos; }

protected:
	bool callRoutines = true;
	bool readFileCluster();
	bool readFileClusterIfNecessary();

	void readDone();

	bool memoryBased = false;
	int32_t readCount{}; // Used for multitask interleaving.
	bool reachedBufferEnd{};
	void resetReader();
};

class FileWriter {
public:
	std::optional<deluge::io::File> file;
	FileWriter();
	FileWriter(bool inMem);

	virtual ~FileWriter();

	Error closeAfterWriting(char const* path, char const* beginningString, char const* endString);

	void writeByte(int8_t b);
	void writeBlock(uint8_t* block, uint32_t size);
	void writeChars(char const* output);
	/// @return true on success, false on failure.
	bool closeWriter();

	char* getBufferPtr() { return writeClusterBuffer; }
	int32_t bytesWritten();
	void setMemoryBased() { memoryBased = true; }

protected:
	void resetWriter();
	Error writeBufferToFile();
	bool memoryBased = false;
	uint8_t indentAmount{};
	char* writeClusterBuffer;
	uint32_t bufferSize;
	int32_t fileWriteBufferCurrentPos{};
	int32_t fileTotalBytesWritten{};
	bool fileAccessFailedDuringWrite{};
};

class Serializer {
public:
	virtual void writeAttribute(char const* name, int32_t number, bool onNewLine = true) = 0;
	virtual void writeAttribute(char const* name, char const* value, bool onNewLine = true) = 0;
	virtual void writeAttributeHex(char const* name, int32_t number, int32_t numChars, bool onNewLine = true) = 0;
	virtual void writeAttributeHexBytes(char const* name, uint8_t* data, int32_t numBytes, bool onNewLine = true) = 0;
	virtual void writeTagNameAndSeperator(char const* tag) = 0;
	virtual void writeTag(char const* tag, int32_t number, bool box = false) = 0;
	virtual void writeTag(char const* tag, char const* contents, bool box = false, bool quote = true) = 0;
	virtual void writeOpeningTag(char const* tag, bool startNewLineAfter = true, bool box = false) = 0;
	virtual void writeOpeningTagBeginning(char const* tag, bool box = false, bool newLineBefore = true) = 0;
	virtual void writeOpeningTagEnd(bool startNewLineAfter = true) = 0;
	virtual void closeTag(bool box = false) = 0;
	virtual void writeClosingTag(char const* tag, bool shouldPrintIndents = true, bool box = false) = 0;
	virtual void writeArrayStart(char const* tag, bool startNewLineAfter = true, bool box = false) = 0;
	virtual void writeArrayEnding(char const* tag, bool shouldPrintIndents = true, bool box = false) = 0;
	virtual void printIndents() = 0;
	virtual void insertCommaIfNeeded() = 0;
	virtual void write(char const* output) = 0;
	virtual Error closeFileAfterWriting(char const* path = nullptr, char const* beginningString = nullptr,
	                                    char const* endString = nullptr) = 0;

	virtual void reset() = 0;
	void writeFirmwareVersion();

	void writeEarliestCompatibleFirmwareVersion(char const* versionString) {
		writeAttribute("earliestCompatibleFirmware", versionString);
	}

	void writeSyncTypeToFile(Song* song, char const* name, SyncType value, bool onNewLine) {
		writeAttribute(name, (int32_t)value, onNewLine);
	}

	void writeAbsoluteSyncLevelToFile(Song* song, char const* name, SyncLevel internalValue, bool onNewLine);
};

class XMLSerializer : public Serializer, public FileWriter {
public:
	XMLSerializer();
	~XMLSerializer() override = default;

	void writeAttribute(char const* name, int32_t number, bool onNewLine = true) override;
	void writeAttribute(char const* name, char const* value, bool onNewLine = true) override;
	void writeAttributeHex(char const* name, int32_t number, int32_t numChars, bool onNewLine = true) override;
	void writeAttributeHexBytes(char const* name, uint8_t* data, int32_t numBytes, bool onNewLine = true) override;
	void writeTagNameAndSeperator(char const* tag) override;
	void writeTag(char const* tag, int32_t number, bool box = false) override;
	void writeTag(char const* tag, char const* contents, bool box = false, bool quote = true) override;
	void writeOpeningTag(char const* tag, bool startNewLineAfter = true, bool box = false) override;
	void writeOpeningTagBeginning(char const* tag, bool box = false, bool newLineBefore = true) override;
	void writeOpeningTagEnd(bool startNewLineAfter = true) override;
	void closeTag(bool box = false) override;
	void writeClosingTag(char const* tag, bool shouldPrintIndents = true, bool box = false) override;
	void writeArrayStart(char const* tag, bool shouldPrintIndents = true, bool box = true) override;
	void writeArrayEnding(char const* tag, bool shouldPrintIndents = true, bool box = true) override;
	void insertCommaIfNeeded() override {}
	void printIndents() override;
	void write(char const* output) override;
	Error closeFileAfterWriting(char const* path = nullptr, char const* beginningString = nullptr,
	                            char const* endString = nullptr) override;
	void reset() override;

private:
	uint8_t indentAmount{};
};

class Deserializer {
public:
	virtual bool prepareToReadTagOrAttributeValueOneCharAtATime() = 0;
	virtual char readNextCharOfTagOrAttributeValue() = 0;
	virtual int32_t getNumCharsRemainingInValueBeforeEndOfCluster() = 0;

	virtual char const* readNextTagOrAttributeName() = 0;
	virtual char const* readTagOrAttributeValue() = 0;
	virtual int32_t readTagOrAttributeValueInt() = 0;
	virtual int32_t readTagOrAttributeValueHex(int32_t errorValue) = 0;
	virtual int readTagOrAttributeValueHexBytes(uint8_t* bytes, int32_t maxLen) = 0;
	virtual Error tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware = false) = 0;

	virtual char const* readNextCharsOfTagOrAttributeValue(int32_t numChars) = 0;
	virtual Error readTagOrAttributeValueString(std::string& string) = 0;
	virtual bool match(char const ch) = 0;
	virtual void exitTag(char const* exitTagName = NULL, bool closeObject = false) = 0;

	virtual void reset() = 0;
};

class FileDeserializer : public Deserializer, public FileReader {
public:
	FileDeserializer() : FileReader() {}
	FileDeserializer(uint8_t* inbuf, size_t buflen) : FileReader((char*)inbuf, buflen) {}
};

class XMLDeserializer : public FileDeserializer {
public:
	XMLDeserializer();
	/// @brief Read XML out of a caller-owned buffer instead of a file.
	/// @param inbuf  First byte of the buffer; the caller owns it and must outlive this reader.
	/// @param buflen Bytes readable at @p inbuf.
	XMLDeserializer(uint8_t* inbuf, size_t buflen);
	~XMLDeserializer() override = default;

	bool prepareToReadTagOrAttributeValueOneCharAtATime() override;
	char const* readNextTagOrAttributeName() override;
	char readNextCharOfTagOrAttributeValue() override;
	int32_t getNumCharsRemainingInValueBeforeEndOfCluster() override;

	int32_t readTagOrAttributeValueInt() override;
	int32_t readTagOrAttributeValueHex(int32_t errorValue) override;
	int readTagOrAttributeValueHexBytes(uint8_t* bytes, int32_t maxLen) override;

	int readHexBytesUntil(uint8_t* bytes, int32_t maxLen, char endPos);
	char const* readNextCharsOfTagOrAttributeValue(int32_t numChars) override;
	Error readTagOrAttributeValueString(std::string& string) override;
	char const* readTagOrAttributeValue() override;
	bool match(char const ch) override;

	void exitTag(char const* exitTagName = NULL, bool closeObject = false) override;
	Error openXMLFile(char const* firstTagName, char const* altTagName = "", bool ignoreIncorrectFirmware = false);
	void reset() override;

	Error tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware) override;

private:
	char charAtEndOfValue{};
	uint8_t xmlArea{}; // state variable for tokenizer

	int32_t tagDepthCaller{}; // How deeply indented in XML the main Deluge classes think we are, as data being read.
	int32_t tagDepthFile{};   // Will temporarily be different to the above as unwanted / unused XML tags parsed on the
	                          // way to finding next useful data.

	char stringBuffer[kFilenameBufferSize]{};

	void skipUntilChar(char endChar);

	char const* readTagName();
	char const* readNextAttributeName();
	char const* readUntilChar(char endChar);
	char const* readAttributeValue();

	int32_t readIntUntilChar(char endChar);
	bool getIntoAttributeValue();
	int32_t readAttributeValueInt();

	Error readStringUntilChar(std::string& string, char endChar);
	Error readAttributeValueString(std::string& string);
};

class JsonSerializer : public Serializer, public FileWriter {
public:
	JsonSerializer();
	~JsonSerializer() override = default;

	void writeAttribute(char const* name, int32_t number, bool onNewLine = true) override;
	void writeAttribute(char const* name, char const* value, bool onNewLine = true) override;
	void writeAttributeHex(char const* name, int32_t number, int32_t numChars, bool onNewLine = true) override;
	void writeAttributeHexBytes(char const* name, uint8_t* data, int32_t numBytes, bool onNewLine = true) override;
	void writeTagNameAndSeperator(char const* tag) override;
	void writeTag(char const* tag, int32_t number, bool box = false) override;
	void writeTag(char const* tag, char const* contents, bool box = false, bool quote = true) override;
	void writeOpeningTag(char const* tag, bool startNewLineAfter = true, bool box = false) override;
	void writeOpeningTagBeginning(char const* tag, bool box = false, bool newLineBefore = true) override;
	void writeOpeningTagEnd(bool startNewLineAfter = true) override;
	void closeTag(bool box = false) override;
	void writeClosingTag(char const* tag, bool shouldPrintIndents = true, bool box = false) override;
	void writeArrayStart(char const* tag, bool shouldPrintIndents = true, bool box = false) override;
	void writeArrayEnding(char const* tag, bool shouldPrintIndents = true, bool box = false) override;
	void printIndents() override;
	void insertCommaIfNeeded() override;
	void write(char const* output) override;
	Error closeFileAfterWriting(char const* path = nullptr, char const* beginningString = nullptr,
	                            char const* endString = nullptr) override;
	void reset() override;
	// Begin Json-only API

private:
	uint8_t indentAmount{};
	bool firstItemHasBeenWritten = false;
};

class JsonDeserializer : public FileDeserializer {
public:
	JsonDeserializer();
	JsonDeserializer(uint8_t* inbuf, size_t buflen);
	~JsonDeserializer() override = default;

	bool prepareToReadTagOrAttributeValueOneCharAtATime() override;
	char const* readNextTagOrAttributeName() override;
	char readNextCharOfTagOrAttributeValue() override;
	int32_t getNumCharsRemainingInValueBeforeEndOfCluster() override;

	int32_t readTagOrAttributeValueInt() override;
	int32_t readTagOrAttributeValueHex(int32_t errorValue) override;
	int readTagOrAttributeValueHexBytes(uint8_t* bytes, int32_t maxLen) override;

	int readHexBytesUntil(uint8_t* bytes, int32_t maxLen, char endPos);
	char const* readNextCharsOfTagOrAttributeValue(int32_t numChars) override;
	Error readTagOrAttributeValueString(std::string& string) override;
	char const* readTagOrAttributeValue() override;
	bool match(char const ch) override;
	void exitTag(char const* exitTagName = NULL, bool closeObject = false) override;

	Error openJsonFile(char const* firstTagName, char const* altTagName = "", bool ignoreIncorrectFirmware = false);
	void reset() override;
	Error tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware) override;
	void setReplySeqNum(uint8_t msgNum) { replySeqNum = msgNum; }
	uint8_t getReplySeqNum() { return replySeqNum; }

private:
	uint8_t replySeqNum = 0;
	int32_t objectDepth{};
	int32_t arrayDepth{};

	enum JsonState { NewFile, KeyRead, ValueRead, ReadError };
	JsonState readState = NewFile;

	char stringBuffer[kFilenameBufferSize]{};

	void skipUntilChar(char endChar);
	char unescape(char inchar);
	bool skipWhiteSpace(bool commasToo = true);
	char const* readQuotedString();
	char const* readKeyName();
	char const* readNextAttributeName();
	char const* readUntilChar(char endChar);
	char const* readAttributeValue();

	int32_t readInt();
	bool getIntoAttributeValue();
	int32_t readAttributeValueInt();
	Error readAttributeValueString(std::string& string);

	Error readStringUntilChar(std::string& string, char endChar);
};

extern XMLSerializer smSerializer;
extern XMLDeserializer smDeserializer;
extern JsonSerializer smJsonSerializer;
extern JsonDeserializer smJsonDeserializer;
extern Serializer& GetSerializer();
extern FileDeserializer* activeDeserializer;

namespace StorageManager {

std::expected<deluge::io::File, Error> createFile(char const* filePath, bool mayOverwrite);
Error createXMLFile(char const* pathName, XMLSerializer& writer, bool mayOverwrite = false, bool displayErrors = true);
Error createJsonFile(char const* pathName, JsonSerializer& writer, bool mayOverwrite = false,
                     bool displayErrors = true);
Error openXMLFile(char const* path, XMLDeserializer& reader, char const* firstTagName, char const* altTagName = "",
                  bool ignoreIncorrectFirmware = false);
Error openJsonFile(char const* path, JsonDeserializer& reader, char const* firstTagName, char const* altTagName = "",
                   bool ignoreIncorrectFirmware = false);
Error openDelugeFile(char const* path, char const* firstTagName, char const* altTagName = "",
                     bool ignoreIncorrectFirmware = false);

/// @brief Open a Deluge document that is already in memory, with no I/O of any kind.
///
/// The in-memory twin of openDelugeFile(): it picks the deserializer the caller names rather than
/// inferring it from a filename extension, points that deserializer at @p buffer, and runs the SAME
/// openXMLFile()/openJsonFile() validation the file path runs.
///
/// @param buffer                 The document bytes. Borrowed, NOT copied: it must outlive every read
///                               through `activeDeserializer` that follows. May be a PREFIX of a longer
///                               document — the reader simply runs out where the buffer does, which
///                               FileReader::reachedEnd() then reports.
/// @param isJson                 `true` to parse as JSON, `false` as XML. The caller decides, because
///                               a buffer has no filename to infer from.
/// @param firstTagName           The root tag the document must open with.
/// @param altTagName             An accepted alternative root tag.
/// @param ignoreIncorrectFirmware Pass the file's `earliestCompatibleFirmware` check.
/// @return Error::NONE, or Error::FILE_CORRUPTED if the root tag was not found.
Error openDelugeBuffer(std::span<const uint8_t> buffer, bool isJson, char const* firstTagName,
                       char const* altTagName = "", bool ignoreIncorrectFirmware = false);
Error initSD();

/// @brief Check whether a file exists at @p pathName.
///
/// @param pathName Absolute path on the SD volume.
/// @return `true` if the file is present; `false` if it is **known absent**
///         (Status::NOT_FOUND). Otherwise an error if existence could not be determined, e.g.
///         Status::BUSY (caller is not on the storage worker fiber) or Status::NO_FILESYSTEM
///         (card unusable).
/// @warning Callers must never treat an error as absence — see
///          docs/superpowers/specs/2026-08-08-existence-check-conflation-design.md.
std::expected<bool, deluge::io::Status> fileExists(char const* pathName);
/// takes a full path/to/file.text and makes sure the directories exist
bool buildPathToFile(const char* fileName);

bool checkSDPresent();
bool checkSDInitialized();

Instrument* createNewInstrument(OutputType newOutputType, ParamManager* getParamManager = nullptr);
Error loadInstrumentFromFile(Song* song, InstrumentClip* clip, OutputType outputType, bool mayReadSamplesFromFiles,
                             Instrument** getInstrument, char const* path, std::string* name, std::string* dirPath);
Instrument* createNewNonAudioInstrument(OutputType outputType, int32_t slot, int32_t subSlot);

Error openMidiDeviceDefinitionFile(char const* path);
Error loadMidiDeviceDefinitionFile(MIDIInstrument* midiInstrument, char const* path, std::string* fileName,
                                   bool updateFileName = true);

Error openPatternFile(char const* path);
Error loadPatternFile(char const* path, std::string* fileName, bool overwriteExisting, bool noScaling, bool previewOnly,
                      bool selectedDrumOnly);

Error openFavouriteFile(char const* path);
Error loadFavouriteFile(char const* path, std::string* fileName);

Drum* createNewDrum(DrumType drumType);
Error loadSynthToDrum(Song* song, InstrumentClip* clip, bool mayReadSamplesFromFiles, SoundDrum** getInstrument,
                      char const* path, std::string* name, std::string* dirPath);

Error checkSpaceOnCard();

Error openInstrumentFile(OutputType outputType, char const* path);
} // namespace StorageManager

extern FirmwareVersion song_firmware_version;
extern const bool writeJsonFlag;

inline bool isCardReady() {
	return !isSDRoutineActive() && Error::NONE == StorageManager::initSD();
}
