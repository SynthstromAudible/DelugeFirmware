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
#include "io/stall_bound.h"
#include "modulation/sync.h"
#include "storage/document_format.h"
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
	bool readFileCluster();
	bool readFileClusterIfNecessary();

	bool memoryBased = false;
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
	/// @brief Put the writer back to writing through a file.
	///
	/// Needed because the shared serialiser is borrowed for memory-backed work and then handed back:
	/// left in memory mode it would silently stop reaching the card for everyone after it.
	void clearMemoryBased() { memoryBased = false; }

	/// @brief Bytes the buffer can currently hold. GROWS as a memory-backed document outgrows it.
	[[nodiscard]] uint32_t bufferCapacity() const { return bufferSize; }

	/// Redirect writes into a caller-owned buffer (for in-memory serialization, e.g. a host
	/// round-trip harness). Writes truncate at `len`, so size it to the largest expected output.
	void setMemoryBuffer(char* buf, uint32_t len) {
		writeClusterBuffer = buf;
		bufferSize = len;
		setMemoryBased();
	}

protected:
	void resetWriter();
	Error writeBufferToFile();

	/// @brief Double the buffer, preserving what has been written. Memory-backed writers only.
	/// @return false if a larger block could not be had, which is a FAILED save and never a short one.
	bool growWriteBuffer();

	bool memoryBased = false;
	uint8_t indentAmount{};
	char* writeClusterBuffer;
	/// @brief The address alloc_sdram actually returned.
	///
	/// `writeClusterBuffer` is this plus CACHE_LINE_SIZE, so freeing THAT hands the allocator an
	/// interior pointer -- which the host's own test seam documents libc rejecting outright. Keeping
	/// the base is what lets the destructor and the growth path both free the thing that was
	/// allocated.
	char* writeClusterBufferBase{};
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

	/// A text-content leaf element whose value is written incrementally between the two calls (e.g. an
	/// auto-param "0x…" hex blob). XML writes it as an element (<tag>…</tag>); JSON overrides to a string
	/// ("tag": "…"), since JSON has no text-content-element concept and the reader consumes it
	/// char-at-a-time as a scalar. The default (element) keeps XMLSerializer byte-identical.
	virtual void writeOpeningTextTag(char const* tag) { writeOpeningTag(tag, false); }
	virtual void writeClosingTextTag(char const* tag) { writeClosingTag(tag, false); }
	virtual void printIndents() = 0;
	virtual void insertCommaIfNeeded() = 0;
	virtual void write(char const* output) = 0;
	virtual Error closeFileAfterWriting(char const* path = nullptr, char const* beginningString = nullptr,
	                                    char const* endString = nullptr) = 0;

	virtual void reset() = 0;
	/// @brief The format this serializer writes.
	[[nodiscard]] virtual deluge::storage::DocumentFormat format() const = 0;
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
	[[nodiscard]] deluge::storage::DocumentFormat format() const override {
		return deluge::storage::DocumentFormat::XML;
	}

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

#include "storage/deserializer.h"

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
	[[nodiscard]] deluge::storage::DocumentFormat format() const override {
		return deluge::storage::DocumentFormat::JSON;
	}

	// A text-content leaf as a JSON string ("tag": "…") rather than an object box, so the auto-param
	// <value> hex round-trips (the reader reads it char-at-a-time as a scalar). See Serializer base.
	void writeOpeningTextTag(char const* tag) override {
		insertCommaIfNeeded();
		write("\n");
		printIndents();
		write("\"");
		write(tag);
		write("\": \"");
	}
	void writeClosingTextTag(char const* /*tag*/) override {
		write("\"");
		firstItemHasBeenWritten = true;
	}

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
	/// Write @p text as the inside of a JSON string, escaping `"`, `\\` and control characters.
	void writeEscaped(char const* text);
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
/// @brief The serializer the document being written goes through: the writer of the innermost ActiveWriterScope, else
/// the XML serializer. Writers nested inside a document (an output's, a drum's) take theirs from here.
extern Serializer& GetSerializer();
/// @brief The shared serializer for @p format.
Serializer& serializerFor(deluge::storage::DocumentFormat format);
/// @brief The shared serializer for @p format, as the FileWriter that holds its memory-based surface.
FileWriter& fileWriterFor(deluge::storage::DocumentFormat format);

/// @brief Makes @p writer the one GetSerializer() returns while a document is written through it; scopes nest.
class ActiveWriterScope {
public:
	explicit ActiveWriterScope(Serializer& writer);
	~ActiveWriterScope();
	ActiveWriterScope(const ActiveWriterScope&) = delete;
	ActiveWriterScope& operator=(const ActiveWriterScope&) = delete;

private:
	Serializer* previous_;
};
extern FileDeserializer* activeDeserializer;
// The reader readFromFile is currently consuming: a streaming reader, or a TreeDocument's tree reader for the
// span of its parse (deluge::storage::ActiveReaderScope). The audio-yield gate consults this to decide whether
// to pump the engine.
extern Deserializer* activeReader;

namespace deluge::storage {
class TreeDocument;
}

namespace StorageManager {

/// @brief The XML declaration every XML document the firmware writes begins with.
///
/// `createXMLFile` writes it for a document streamed to the card; a document serialised into memory for the byte
/// channel writes it itself, so both arms produce the same bytes.
inline constexpr const char* kXmlDocumentHead = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";

std::expected<deluge::io::File, Error> createFile(char const* filePath, bool mayOverwrite);
Error createXMLFile(char const* pathName, XMLSerializer& writer, bool mayOverwrite = false, bool displayErrors = true);
Error createJsonFile(char const* pathName, JsonSerializer& writer, bool mayOverwrite = false,
                     bool displayErrors = true);
/// @brief Open a Deluge document that is already in memory, with no I/O of any kind.
///
/// The only way a document is parsed. It picks the deserializer the caller names rather than
/// inferring it from a filename extension, points that deserializer at @p buffer, and runs
/// XMLDeserializer::openXMLFile() or JsonDeserializer::openJsonFile() to validate it -- those remain
/// the deserializers' own methods.
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
	/// How long this staging has been deferred without progress. Bounded, because a channel that is
	/// never going to answer must END the wait rather than defer forever -- an unbounded retry is not
	/// patience, it is a wedge that presents as the device having hung. The caller sets its limit;
	/// releaseStaging() keeps it.
	deluge::io::StallBound stallBound{};
	/// What the latest Deferred waits for. See stagingNextRunUs().
	enum class Wait : uint8_t {
		Answer,  ///< The channel's answer to a request it holds, which raises DELUGE_WAKE_STORAGE.
		Refused, ///< The channel refused (or may have dropped) a request: asked again on the next run.
		Now,     ///< Nothing to wait for: the next run issues the read.
	};
	Wait wait = Wait::Answer;
};

/// @brief Read the whole of @p path into a freshly allocated buffer, blocking until it is there.
///
/// The inline arm of openDocumentStaged(), which takes it only before boot finishes; openDocumentSynchronously()
/// fills through it too. Nothing chooses it because staging failed.
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
/// The one entry point for reading a Deluge document. After boot it drives the byte channel and the
/// whole-file read, returning StagedOpen::Deferred until the bytes arrive; before boot finishes it
/// fills synchronously and returns a terminal answer at once.
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

/// @brief Stage @p path in memory without parsing it: openDocumentStaged() minus the open.
///
/// Same arms, same Deferred/Failed answers and the same StagingState contract; Opened here means only that all of
/// the file's bytes are in `state->buffer`, untouched -- for a caller that parses them some other way
/// (deluge::storage::TreeDocument::open()).
StagedOpen stageDocument(char const* path, StagingState* state);

/// @brief Stage @p path and open it as a Value tree in @p document: stageDocument() plus TreeDocument::open().
///
/// The opener for every document whose readers take tags from the tree (presets, kit rows, MIDI device
/// definitions). Same Deferred/Failed answers as stageDocument(); on Opened the staged bytes are already freed,
/// since the tree holds its own copies, and @p document is positioned inside @p firstTagName (or @p altTagName).
/// A file that does not parse fails here, before the caller has changed anything.
/// @param state    Caller-owned staging, as for stageDocument().
/// @param document Caller-owned; release it once the parse is done.
StagedOpen openDocumentStagedTree(char const* path, char const* firstTagName, char const* altTagName,
                                  StagingState* state, deluge::storage::TreeDocument& document);

/// @brief When a caller that runs only when woken asks again after openDocumentStaged() answered Deferred, on the app
///        loop's clock (µs).
///
/// 0 (at once) when the next run has a request to issue; for a refused request, the stall bound's deadline or
/// kRefusedRecheckUs from now, whichever is sooner (an answer aging out of the channel raises no wake); for a
/// request the channel holds, the stall bound's deadline -- a full bound from now if its span has not started -- since
/// the answer itself raises DELUGE_WAKE_STORAGE.
[[nodiscard]] uint64_t stagingNextRunUs(const StagingState& state);

/// @brief Free a staged buffer and clear its state. Safe on a state that holds nothing.
///
/// Safe mid-read, too: a buffer the byte channel is still reading into is parked instead of freed, and
/// reapOrphanedStaging() frees it once the channel lets go. So a consumer that abandons a staged read on any path
/// needs no special handling.
void releaseStaging(StagingState* state);

/// @brief Free the parked buffers the byte channel no longer holds. Called from the channels' service task, and from
///        each staging call, so a parked buffer is freed within a tick of its read finishing.
void reapOrphanedStaging();

/// @brief How many released buffers are parked waiting for the byte channel to let go. For gates.
[[nodiscard]] int32_t orphanedStagingCount();

/// @brief Stage a document synchronously in a shared slot and open it for parsing.
///
/// Blocks on the card for the whole read, so it belongs to the inline arms that run before boot finishes: the
/// settings, MIDI-device, MIDI-follow and performance-view readers, the favourites bank, and the song browser's
/// preview. After boot each of those reads through a channel instead, and a document read that has to span ticks
/// uses openDocumentStaged().
///
/// The shared slot is safe because these callers are never in flight at once -- each runs start to finish on one
/// call stack. The caller MUST call releaseDocumentStaging() once its deserializer is finished.
///
/// @return Error::NONE, Error::FILE_NOT_FOUND if it could not be staged, or Error::FILE_CORRUPTED.
Error openDocumentSynchronously(char const* path, char const* firstTagName, char const* altTagName = "");

/// @brief openDocumentStagedTree()'s blocking arm: read @p path whole and open it as a Value tree in @p document.
///
/// For the inline readers that need a tree (the harness's preset fixtures). Uses no shared slot: the bytes are
/// freed before this returns.
/// @return Error::NONE, the read's error, or Error::FILE_CORRUPTED.
Error openDocumentTreeSynchronously(char const* path, char const* firstTagName, char const* altTagName,
                                    deluge::storage::TreeDocument& document);

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
/// The evidence that the async arm executed at all, for a gate to check against -- without it, a test
/// could pass while quietly always taking the synchronous path.
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
/// The work it starts does not care where its caller stands: `cardReinserted()` ARMS a walk that
/// checks one resident per tick through the byte channel, rather than opening every resident sample
/// in one blocking pass. That is why this takes no "may I run the work here?" parameter.
void noticeCardMount();

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
///         Status::BUSY (the filesystem refused to answer) or Status::NO_FILESYSTEM
///         (card unusable).
/// @warning Callers must never treat an error as absence — see
///          docs/superpowers/specs/2026-08-08-existence-check-conflation-design.md.
std::expected<bool, deluge::io::Status> fileExists(char const* pathName);
/// takes a full path/to/file.text and makes sure the directories exist
bool buildPathToFile(const char* fileName);

bool checkSDPresent();
bool checkSDInitialized();

Instrument* createNewInstrument(OutputType newOutputType, ParamManager* getParamManager = nullptr);
/// @brief Read an Instrument from @p document.
///
/// The parse half of a staged read: it opens nothing and releases nothing, so a caller that opened the document itself
/// (openDocumentStagedTree(), or the harness's own fixture loader) parses it on its own tier and releases it after. On
/// any failure the Instrument it made is deleted again.
/// @param document               The preset, opened as a Value tree inside its root tag.
/// @param song                   The song the Instrument will belong to.
/// @param clip                   The clip it is being loaded for, if any.
/// @param outputType             Which kind of Instrument the document must hold.
/// @param mayReadSamplesFromFiles Whether sample data may be read from the card while parsing.
/// @param getInstrument Receives the new Instrument on success; untouched otherwise.
/// @param name          The Instrument's name.
/// @param dirPath       The folder the preset is in.
/// @return Error::NONE, or why the document could not be read into an Instrument.
Error parseInstrumentDocument(deluge::storage::TreeDocument& document, Song* song, InstrumentClip* clip,
                              OutputType outputType, bool mayReadSamplesFromFiles, Instrument** getInstrument,
                              std::string* name, std::string* dirPath);
/// @brief The root tag, and its accepted alternative, of an @p outputType preset document.
/// @param outputType   Which kind of Instrument the document must hold.
/// @param firstTagName Set to the expected root tag.
/// @param altTagName   Set to the accepted alternative root tag.
void instrumentDocumentTags(OutputType outputType, char const** firstTagName, char const** altTagName);
Instrument* createNewNonAudioInstrument(OutputType outputType, int32_t slot, int32_t subSlot);

/// @brief Read a device definition into @p midiInstrument from @p document. The parse half of a staged definition
///        read; it opens nothing and releases nothing.
/// @param document       The definition, opened as a Value tree inside its root tag.
/// @param midiInstrument The instrument to read the definition into.
/// @param fileName       Set to the definition's file name.
/// @param updateFileName Whether @p fileName should be updated.
/// @return Error::NONE, or why the definition could not be read.
Error parseMidiDeviceDefinitionDocument(deluge::storage::TreeDocument& document, MIDIInstrument* midiInstrument,
                                        std::string* fileName, bool updateFileName);

/// @brief Paste the pattern the document `activeDeserializer` has open into the current clip, and close it. The parse
///        half of a staged pattern read; it opens nothing and frees no staging.
/// @param overwriteExisting Whether the pasted notes replace the ones already under them.
/// @param noScaling         Whether the notes keep the pattern's own scale instead of the clip's.
/// @param previewOnly       Whether this is a PLAY preview rather than a load.
/// @param selectedDrumOnly  Whether only the selected drum's row is pasted.
/// @return Error::NONE, or why the pattern could not be read.
Error parsePatternDocument(bool overwriteExisting, bool noScaling, bool previewOnly, bool selectedDrumOnly);

Error openFavouriteFile(char const* path);

Drum* createNewDrum(DrumType drumType);
/// @brief Read a synth preset into a new SoundDrum from @p document.
///
/// The parse half of a staged synth-to-row read; it opens nothing and releases nothing. On success a drum already in
/// @p getInstrument is deleted and replaced by the new one; on failure the new drum is deleted.
/// @param document      The synth preset, opened as a Value tree inside its root tag.
/// @param song          The song the drum will belong to.
/// @param clip          The clip it is being loaded for.
/// @param getInstrument The drum slot to fill.
/// @return Error::NONE, or why the document could not be read into a drum.
Error parseSynthDrumDocument(deluge::storage::TreeDocument& document, Song* song, InstrumentClip* clip,
                             SoundDrum** getInstrument);

Error checkSpaceOnCard();
} // namespace StorageManager

extern FirmwareVersion song_firmware_version;

inline bool isCardReady() {
	return Error::NONE == StorageManager::initSD();
}
