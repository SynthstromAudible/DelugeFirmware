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
/// @brief Open a Deluge document that is already in memory, with no I/O of any kind.
///
/// The only way a document is parsed. It picks the deserializer the caller names rather than
/// inferring it from a filename extension, points that deserializer at @p buffer, and runs the same
/// XMLDeserializer::openXMLFile()/JsonDeserializer::openJsonFile() validation as ever -- those are
/// the deserializers' own methods, and they stay; what has gone is the pair of StorageManager
/// functions that used to open a document by streaming it off the card while parsing it.
///
/// @param buffer                 The document bytes. Borrowed, NOT copied: it must outlive every read
///                               through `activeDeserializer` that follows. May be a PREFIX of a longer
///                               document — the reader simply runs out where the buffer does, which
///                               FileReader::reachedEnd() then reports.
///                               MUTABLE, and written to: the walk NUL-terminates each name and value
///                               it returns IN PLACE, overwriting the delimiter byte in these very
///                               bytes (`XMLDeserializer::readNextAttributeName`, and both
///                               deserializers' `readUntilChar`). The caller's buffer therefore comes
///                               back perforated with NULs, and read-only storage — a `.rodata`
///                               literal, a memory-mapped page — must never be passed here.
/// @param isJson                 `true` to parse as JSON, `false` as XML. The caller decides, because
///                               a buffer has no filename to infer from.
/// @param firstTagName           The root tag the document must open with.
/// @param altTagName             An accepted alternative root tag.
/// @param ignoreIncorrectFirmware Pass the file's `earliestCompatibleFirmware` check.
/// @return Error::NONE, or Error::FILE_CORRUPTED if the root tag was not found.
/// @note This leaves `activeDeserializer` pointing at the memory reader on EVERY path, the failed
///       open included — so a caller must save the previous value before calling and restore it at a
///       single exit, as `deluge::storage::decodeSongPreview()` does. That is not hygiene: code
///       downstream WRITES through the global without opening a document of its own (SaveSongUI's
///       sample collection reads `Cluster::size` — 32 KiB — into `activeDeserializer->fileClusterBuffer`,
///       which left pointing here IS @p buffer). Copy that function's save/restore, not just this call.
Error openDelugeBuffer(std::span<uint8_t> buffer, bool isJson, char const* firstTagName, char const* altTagName = "",
                       bool ignoreIncorrectFirmware = false);

/// @brief Outcome of one attempt to stage a document and open it for parsing.
enum class StagedOpen : uint8_t {
	/// Nothing is ready yet. The caller must return to its executor and ask again on a later tick.
	/// Only the channel arm can produce this; the synchronous arm never does.
	Deferred,
	/// The document is in the caller's `StagingState` and `activeDeserializer` now points at it.
	Opened,
	/// The document could not be staged or did not parse. Any buffer has been released.
	Failed,
	/// This context cannot stage: it is off the storage owner and the channel is not running, so the
	/// only fill left is the blocking one -- and blocking HERE stops the very tier the card needs
	/// running to answer, which wedges rather than waits. The caller must take the work to the owner
	/// and stage there.
	///
	/// NEVER means "the file was unreadable". This is the BUILD-configuration fallback: it disappears
	/// when the channel becomes unconditional, and nothing chooses it because staging failed.
	NotHere,
};

/// @brief Cross-tick state for one staged open. One per concurrent staging caller.
///
/// This must OUTLIVE the parse, not merely the open. openDelugeBuffer() borrows these bytes and the
/// walk NUL-terminates names and values inside them, so freeing when the open returns pulls the
/// document out from under the deserializer. Free it when the DESERIALIZER is finished --
/// releaseStaging() after closeWriter(), and on every path that abandons the load in between.
struct StagingState {
	/// The staged bytes, owned by this state and freed by releaseStaging().
	uint8_t* buffer = nullptr;
	/// Bytes in `buffer`. Set to the file's true size once the fill completes.
	uint32_t size = 0;
	/// Whether a channel read has been issued and is awaiting its collect.
	bool issued = false;
};

/// @brief Read the whole of @p path into a freshly allocated buffer, blocking until it is there.
///
/// The OWNER arm of openDocumentStaged(). It runs only where blocking was always legal, and it is a
/// BUILD-configuration fallback rather than a runtime one: it retires when the channel becomes
/// unconditional, and nothing chooses it because staging failed.
///
/// @param path NUL-terminated path.
/// @param out  Receives the buffer and its size on success; untouched on failure.
/// @return Error::NONE, or which KIND of failure it was. FILE_CORRUPTED for a file that opened and
///         held nothing -- an empty document has no root tag, which is what the streaming reader
///         reported for one, and collapsing that into FILE_NOT_FOUND would tell someone their file
///         is missing when it is sitting on the card.
Error readWholeFileSynchronously(char const* path, StagingState* out);

/// @brief Stage @p path in memory and open it for parsing, from whichever arm this context allows.
///
/// The one entry point for reading a Deluge document. Off the storage owner it drives the byte
/// channel and the whole-file read, returning StagedOpen::Deferred until the bytes arrive; on the
/// owner it fills synchronously and returns a terminal answer at once.
///
/// @param path         NUL-terminated path. Its extension picks the parser, exactly as the file-backed
///                     opener did: a name containing ".Json" parses as JSON, anything else as XML.
/// @param firstTagName The root tag the document must open with.
/// @param altTagName   An accepted alternative root tag.
/// @param state        Caller-owned, and must outlive the parse. See StagingState.
/// @return Deferred (ask again next tick), Opened, or Failed.
/// @note On Opened this leaves `activeDeserializer` pointing at a memory reader, with all of
///       openDelugeBuffer()'s caveats about saving and restoring it.
StagedOpen openDocumentStaged(char const* path, char const* firstTagName, char const* altTagName, StagingState* state);

/// @brief Free a staged buffer and clear its state. Safe on a state that holds nothing.
void releaseStaging(StagingState* state);

/// @brief Stage a document synchronously in a shared slot and open it for parsing.
///
/// For the single-shot openers below, whose callers run a whole load start-to-finish inside one
/// call and so cannot act on a StagedOpen::Deferred. `LoadInstrumentPresetUI::performLoad()` states
/// that invariant outright and enforces it with FREEZE_WITH_ERROR("TIR1"); the pattern, favourite,
/// MIDI-definition and synth-to-drum loads have the same one-call shape.
///
/// This is behaviour-preserving rather than a relocation: those callers blocked through the
/// streaming reader before and block the same amount here. What changes is only that the parse now
/// runs over memory, which is what lets the streaming readers go.
///
/// The shared slot is safe because these openers are never in flight at once -- each is driven by a
/// single load* wrapper on one call stack. The wrapper MUST call releaseDocumentStaging() once its
/// deserializer is finished.
///
/// @return Error::NONE, Error::FILE_NOT_FOUND if it could not be staged, or Error::FILE_CORRUPTED.
Error openDocumentSynchronously(char const* path, char const* firstTagName, char const* altTagName = "");

/// @brief Whether @p path names a JSON document rather than an XML one.
///
/// The extension rule the file-backed opener used, in one place now that three call sites need it: a
/// buffer has no filename to infer from, so whoever stages one has to decide before parsing. Getting
/// it wrong routes an XML song to the JSON parser and the failure reads as a corrupt file.
bool documentIsJson(char const* path);

/// @brief Release the shared slot filled by openDocumentSynchronously(). Safe to call twice.
void releaseDocumentStaging();

/// @brief Whole-file staging reads the CHANNEL has completed.
///
/// The evidence that the async arm executed at all. It shipped with no harness building
/// `async_file_write_active`, so until this counter had a gate reading it, nothing had ever run it.
[[nodiscard]] int32_t stagingChannelServedOps();
Error initSD();

/// @brief Run AudioFileManager::firstCardRead() if the filesystem has been mounted since we last
///        looked, and nothing otherwise.
///
/// The card can now be mounted by something other than @ref initSD — the directory channel's driver
/// mounts on the storage tier, because mounting is card I/O and its caller is on the interaction
/// tier. So the unmounted→mounted edge is tracked by a counter both paths share rather than by
/// `initSD` watching its own call.
///
/// @param mayRunHere Whether the caller is somewhere `firstCardRead()` can run. It reaches
///        `cardReinserted()`, which OPENS every resident sample to check it survived the swap — real
///        card I/O, so on the interaction tier this must be false and the work is handed to the
///        storage owner instead. Handing it over does not block.
void noticeCardMount(bool mayRunHere);

/// @brief How many mount edges @ref noticeCardMount has acted on. For gates only.
///
/// Exists because the two hazards here are both invisible from the outside: firing the notification
/// twice for one mount, and never firing it at all. Neither shows up in a listing's result.
[[nodiscard]] uint32_t cardMountNotices();

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
