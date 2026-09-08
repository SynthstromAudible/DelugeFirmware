#include "storage/smsysex.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/oled.h"
#include "hid/hid_sysex.h"
#include "io/debug/log.h"
#include "io/debug/print.h"
#include "io/file.hpp"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/sysex.h"
#include "memory/general_memory_allocator.h"
#include "processing/engines/audio_engine.h"
#include "scheduler_api.h"
#include "storage/fs_status.h"
#include "storage/owner.h"
#include "storage/smsysex_pending.h"
#include "sync/sd_access.h"
#include "util/containers.h"
#include "util/pack.h"
#include <cstdint>
#include <cstring>

#define MAX_DIR_LINES 25

using deluge::storage::FsWireStatus;

std::optional<deluge::io::Directory> sxDir;
uint32_t dirOffsetCounter;

JsonSerializer jWriter;
std::string activeDirName;
const size_t blockBufferMax = 1024;
const size_t sysexBufferMax = blockBufferMax + 256;
uint8_t* writeBlockBuffer = nullptr;
uint8_t* readBlockBuffer = nullptr;
const uint32_t MAX_OPEN_FILES = 4;

struct FILdata {
	std::string fName;
	uint32_t fileID;
	uint32_t LRUstamp = 0;
	uint32_t fSize = 0;
	uint32_t fPosition = 0; // file offset noted after last read/write operation.
	bool fileOpen = false;
	bool forWrite = false;
	// No `deluge::io::File` any more. A `fid` is a WIRE concept the client owns, and the transfer is
	// offset-addressed, so the path and position below are everything a block needs -- nothing has to
	// be held open across the ticks a deferred operation spans.
};

int32_t FIDcounter = 1;
uint32_t LRUcounter = 1;

PLACE_SDRAM_BSS FILdata openFiles[MAX_OPEN_FILES];

namespace {

// Freezes the wire's "err" attribute at today's FRESULT-numeric values (now carried by
// FsWireStatus, see storage/fs_status.h), independent of whatever backend implements
// file_io.h underneath — this is the permanent abstraction boundary between the companion
// protocol and deluge::io, not a migration shim. One accepted, documented collapse:
// Status::NOT_FOUND can't distinguish the original FR_NO_FILE from FR_NO_PATH (both already
// collapsed into DELUGE_ERR_NOT_FOUND at the boundary's original design); this always
// produces NoFile. Status::UNSUPPORTED has no real FRESULT analog; InvalidParameter is the
// closest fit. Status::NOT_EMPTY maps onto the original FRESULT's own FR_DENIED (7, already
// FsWireStatus::Denied above) — FatFS used that same code for "the target directory is not
// empty", so this isn't a new wire value, just a second Status producing an existing one.
FsWireStatus toWireStatus(deluge::io::Status status) {
	switch (status) {
	case deluge::io::Status::OK:
		return FsWireStatus::Ok;
	case deluge::io::Status::ERR:
	case deluge::io::Status::IO:
		return FsWireStatus::DiskErr;
	case deluge::io::Status::PARAM:
	case deluge::io::Status::UNSUPPORTED:
		return FsWireStatus::InvalidParameter;
	case deluge::io::Status::BUSY:
		return FsWireStatus::Locked;
	case deluge::io::Status::TIMEOUT:
		return FsWireStatus::Timeout;
	case deluge::io::Status::NODEV:
		return FsWireStatus::NotReady;
	case deluge::io::Status::NOT_FOUND:
		return FsWireStatus::NoFile;
	case deluge::io::Status::EXISTS:
		return FsWireStatus::Exist;
	case deluge::io::Status::NO_SPACE:
		return FsWireStatus::Denied;
	case deluge::io::Status::NO_FILESYSTEM:
		return FsWireStatus::NoFilesystem;
	case deluge::io::Status::WRITE_PROTECTED:
		return FsWireStatus::WriteProtected;
	case deluge::io::Status::NO_MEMORY:
		return FsWireStatus::NotEnoughCore;
	case deluge::io::Status::NOT_EMPTY:
		return FsWireStatus::Denied;
	}
	return FsWireStatus::DiskErr; // unreachable while the switch above stays exhaustive
}

// The wire's "date"/"time" ints are already FAT DOS-packed WORDs (the
// companion app speaks FatFS's native format directly) -- this decodes them
// into the boundary's portable DelugeTimestamp. Returns nullopt when both
// are zero, matching every existing call site's "date != 0 || time != 0"
// gate for "no timestamp requested."
std::optional<DelugeTimestamp> timestampFromWireDateTime(uint32_t date, uint32_t time) {
	if (date == 0 && time == 0) {
		return std::nullopt;
	}
	DelugeTimestamp ts{};
	ts.year = static_cast<uint16_t>(1980 + ((date >> 9) & 0x7F));
	ts.month = static_cast<uint8_t>((date >> 5) & 0x0F);
	ts.day = static_cast<uint8_t>(date & 0x1F);
	ts.hour = static_cast<uint8_t>((time >> 11) & 0x1F);
	ts.minute = static_cast<uint8_t>((time >> 5) & 0x3F);
	ts.second = static_cast<uint8_t>((time & 0x1F) * 2);
	return ts;
}

// Inverse of timestampFromWireDateTime — packs a portable DelugeTimestamp
// back into the wire's FAT DOS-packed WORD shape for getDirEntries's reply.
uint16_t wireDateFromTimestamp(const DelugeTimestamp& ts) {
	return static_cast<uint16_t>(((ts.year - 1980) << 9) | (ts.month << 5) | ts.day);
}

uint16_t wireTimeFromTimestamp(const DelugeTimestamp& ts) {
	return static_cast<uint16_t>((ts.hour << 11) | (ts.minute << 5) | (ts.second / 2));
}

// FAT attribute-byte bit flags for the wire's "attr" field (DOS/FAT fattrib format).
// Like FsWireStatus, these are frozen wire values inherited from FatFS, not live constants
// from the FatFS headers.
constexpr uint8_t kWireAttrReadOnly = 0x01;
constexpr uint8_t kWireAttrHidden = 0x02;
constexpr uint8_t kWireAttrSystem = 0x04;
constexpr uint8_t kWireAttrDirectory = 0x10;
constexpr uint8_t kWireAttrArchive = 0x20;

// Packs DelugeDirEntry's portable attribute flags back into the wire's raw
// FatFS fattrib byte shape.
uint8_t wireAttribFromFlags(const DelugeDirEntry& entry) {
	uint8_t attrib = 0;
	if (entry.is_read_only)
		attrib |= kWireAttrReadOnly;
	if (entry.is_hidden)
		attrib |= kWireAttrHidden;
	if (entry.is_system)
		attrib |= kWireAttrSystem;
	if (entry.is_directory)
		attrib |= kWireAttrDirectory;
	if (entry.is_archive)
		attrib |= kWireAttrArchive;
	return attrib;
}

} // namespace

const int MaxSysExLength = 1024;

struct SysExDataEntry {
	MIDICable& cable;
	int32_t len;
	uint8_t data[sysexBufferMax]{};

	SysExDataEntry(MIDICable& forCable, int32_t newLen) : cable{forCable}, len{newLen} {}
};

deluge::deque<SysExDataEntry> SysExQ;

// The following constants assume that the messageID part ranges from 1 to SYSEX_MSGID_MAX
// and that SYSEX_MSGID_MAX is 1 less than a power of 2.
// It also assumes that MAX_SYSEX_SESSIONS is also 1 less than a power of 2.
const uint32_t MAX_SYSEX_SESSIONS = 15;
const uint32_t SYSEX_MSGID_MAX = 7;
const uint32_t SYSEX_MSGID_MASK = 0x07;
const uint32_t SYSEX_SESSION_MASK = 0x78;
const uint32_t SYSEX_SESSION_SHIFT = 3;

uint32_t session_mono_counter = 1;
uint32_t sessionLRU_array[MAX_SYSEX_SESSIONS + 1] = {0};

void smSysex::noteSessionIdUse(uint8_t msgId) {
	uint32_t sessionNum = (msgId & SYSEX_SESSION_MASK) >> SYSEX_SESSION_SHIFT;
	sessionLRU_array[sessionNum] = session_mono_counter++;
}

void smSysex::noteFileIdUse(FILdata* fp) {
	fp->LRUstamp = LRUcounter++;
}

// Returns the entry in the FILdata array for the given fid
FILdata* smSysex::entryForFID(uint32_t fid) {
	for (int i = 0; i < MAX_OPEN_FILES; ++i) {
		if (openFiles[i].fileID == fid) {
			return openFiles + i;
		}
	}
	return nullptr;
}

// Assign a FIL from our pool.
FILdata* smSysex::findEmptyFIL() {
	uint32_t LRUtime = 0xFFFFFFFF;
	uint32_t LRUindex = 0;

	for (int i = 0; i < MAX_OPEN_FILES; ++i) {
		if (!openFiles[i].fileOpen) {
			return openFiles + i;
		}
		else if (openFiles[i].LRUstamp < LRUtime) {
			LRUtime = openFiles[i].LRUstamp;
			LRUindex = i;
		}
	}
	// Close the abandoned file before we reuse the entry.
	FILdata* oldest = openFiles + LRUindex;
	closeFIL(oldest);
	return oldest;
}

void smSysex::startDirect(JsonSerializer& writer) {
	writer.reset();
	writer.setMemoryBased();
	uint8_t reply_hdr[7] = {0xf0, 0x00, 0x21, 0x7B, 0x01, SysEx::SysexCommands::Json, 0};
	writer.writeBlock(reply_hdr, sizeof(reply_hdr));
}

void smSysex::startReply(JsonSerializer& writer, JsonDeserializer& reader) {
	writer.reset();
	writer.setMemoryBased();
	uint8_t reply_hdr[7] = {0xf0, 0x00, 0x21, 0x7B, 0x01, SysEx::SysexCommands::JsonReply, reader.getReplySeqNum()};
	writer.writeBlock(reply_hdr, sizeof(reply_hdr));
}

void smSysex::sendMsg(MIDICable& cable, JsonSerializer& writer) {
	writer.writeByte(0xF7);

	char* bitz = writer.getBufferPtr();
	int32_t bw = writer.bytesWritten();
	cable.sendSysex((const uint8_t*)bitz, bw);
};

/// Start the card work an open needs: a size for a read, an empty file for a write.
///
/// No handle is opened. What an open MEANS here is "the client may now name this file by fid", and
/// the transfer that follows is offset-addressed -- so a read needs only the size it must report, and
/// a write needs only the file to exist and be empty.
void smSysex::beginOpenFIL(const char* fPath, bool forWrite) {
	if (forWrite) {
		// Creates the parent, creates the file, truncates it -- exactly what DELUGE_FILE_WRITE_CREATE
		// meant. It also makes `openFile`'s old createPathDirectories retry unnecessary, because the
		// whole-file write creates the destination's parent itself.
		(void)deluge::storage::pathOpStart(deluge::storage::PathOp::CreateEmpty, fPath, nullptr, {});
		return;
	}
	(void)deluge::storage::pathOpStartBlock(deluge::storage::PathOp::Stat, fPath, 0, nullptr, 0);
}

/// Finish an open whose card work has landed, recording the fid the client will use.
FILdata* smSysex::finishOpenFIL(const char* fPath, bool forWrite, deluge::io::Status status,
                                deluge::storage::FsWireStatus* eCode) {
	*eCode = toWireStatus(status);
	if (status != deluge::io::Status::OK) {
		return nullptr;
	}
	FILdata* fp = findEmptyFIL();
	fp->fName = fPath;
	fp->fileID = FIDcounter++;
	noteFileIdUse(fp);
	// A write starts empty; a read reports the size the stat just measured.
	fp->fSize = forWrite ? 0 : deluge::storage::pathOpResultValue();
	fp->fileOpen = true;
	fp->forWrite = forWrite;
	fp->fPosition = 0;
	return fp;
}

FsWireStatus smSysex::closeFIL(FILdata* fp) {
	if (fp == nullptr) {
		return FsWireStatus::InvalidObject;
	}

	// Nothing to release: no handle was ever held. That is the point of the stateless design, and it
	// is why this cannot fail any more.
	const deluge::io::Status status = deluge::io::Status::OK;
	fp->fileOpen = false;
	fp->forWrite = false;
	fp->fSize = 0;
	return toWireStatus(status);
}

// Fill in missing directories for the full path name given.
// Unless the last character in the path is a /, we assume the
// path given ends with a filename (which we ignore).
FsWireStatus smSysex::createPathDirectories(std::string& path, std::optional<DelugeTimestamp> timestamp) {
	if (path.size() > 256) {
		return FsWireStatus::InvalidParameter;
	}

	char working[257];
	char pathPart[257];
	strcpy(working, path.c_str());
	int len = strlen(working);
	int lastSlash;
	for (lastSlash = len - 1; lastSlash >= 0; lastSlash--) {
		if (working[lastSlash] == '/')
			break;
	}
	if (lastSlash == 0) {
		return FsWireStatus::InvalidParameter;
	}

	deluge::io::Status status = deluge::io::Status::OK;
	int jx = 1; // skip the leading slash.
	while (jx <= lastSlash) {
		if (working[jx] == '/') {
			working[jx] = 0;
			strcpy(pathPart, working);
			working[jx] = '/';
			if (strlen(pathPart)) {
				auto dir = deluge::io::Directory::open(pathPart);
				if (!dir.has_value() && dir.error() == deluge::io::Status::NOT_FOUND) {
					auto made = deluge::io::mkdir(pathPart);
					// preserving pre-existing quirk: a failed mkdir here does not
					// return early, it just leaves `status` set and the loop
					// continues to the next path segment.
					status = made.has_value() ? deluge::io::Status::OK : made.error();
					if (made.has_value() && timestamp.has_value()) {
						auto timed = deluge::io::set_time(pathPart, *timestamp);
						status = timed.has_value() ? deluge::io::Status::OK : timed.error();
					}
				}
				else if (!dir.has_value()) {
					return toWireStatus(dir.error());
				}
				// else: pathPart already exists as a directory; `dir` closes via
				// RAII when it goes out of scope at the end of this block.
			}
		}
		jx++;
	}
	return toWireStatus(status);
}

bool smSysex::openFile(MIDICable& cable, JsonDeserializer& reader) {
	char const* tagName;
	std::string path;
	bool forWrite = false;
	uint32_t date = 0;
	uint32_t time = 0;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "write")) {
			forWrite = reader.readTagOrAttributeValueInt() != 0;
		}
		else if (!strcmp(tagName, "path")) {
			reader.readTagOrAttributeValueString(path);
		}
		else if (!strcmp(tagName, "date")) {
			date = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "time")) {
			time = reader.readTagOrAttributeValueInt();
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');
	(void)date;
	(void)time;

	if (path.empty()) {
		return true;
	}

	// The old path-missing retry is gone with the handle. An open-for-write is now a whole-file write
	// of zero bytes, and that creates the destination's parent itself -- so there is nothing left for
	// a createPathDirectories retry to fix.
	if (!deluge::storage::pathOpPending()) {
		beginOpenFIL(path.c_str(), forWrite);
		return false;
	}
	auto outcome = deluge::storage::pathOpCollectBlock(nullptr, nullptr);
	if (!outcome.has_value()) {
		return false;
	}
	FsWireStatus errCode = FsWireStatus::Ok;
	FILdata* fp = finishOpenFIL(path.c_str(), forWrite, *outcome, &errCode);
	const uint32_t fSize = (fp != nullptr) ? fp->fSize : 0;
	deluge::storage::pathOpClear();

	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^open", false, true);
	jWriter.writeAttribute("fid", fp != nullptr ? fp->fileID : 0);
	jWriter.writeAttribute("size", fSize);
	jWriter.writeAttribute("err", static_cast<uint8_t>(errCode));
	jWriter.closeTag(true);

	sendMsg(cable, jWriter);
	return true;
}

bool smSysex::closeFile(MIDICable& cable, JsonDeserializer& reader) {
	int32_t fid = 0;
	char const* tagName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "fid")) {
			fid = reader.readTagOrAttributeValueInt();
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	FILdata* fd = entryForFID(fid);
	FsWireStatus errCode = closeFIL(fd);

	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^close", false, true);
	jWriter.writeAttribute("fid", (uint32_t)fid);
	jWriter.writeAttribute("err", static_cast<uint8_t>(errCode));
	jWriter.closeTag(true);

	sendMsg(cable, jWriter);
	return true; // nothing to wait for: closing releases no handle any more
}

bool smSysex::deleteFile(MIDICable& cable, JsonDeserializer& reader) {
	char const* tagName;
	std::string path;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "path")) {
			reader.readTagOrAttributeValueString(path);
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	if (path.empty()) {
		return true; // nothing asked for; nothing to reply to
	}
	D_PRINTLN(path.c_str());

	if (!deluge::storage::pathOpPending()) {
		(void)deluge::storage::pathOpStart(deluge::storage::PathOp::Unlink, path.c_str(), nullptr, {});
		return false; // re-entered next tick, where this record is found and collected
	}
	auto outcome = deluge::storage::pathOpCollect();
	if (!outcome.has_value()) {
		return false; // still running
	}
	deluge::storage::pathOpClear();

	FsWireStatus errCode = toWireStatus(*outcome);
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^delete", false, true);
	jWriter.writeAttribute("err", static_cast<uint8_t>(errCode));
	jWriter.closeTag(true);
	sendMsg(cable, jWriter);
	return true;
}

bool smSysex::createDirectory(MIDICable& cable, JsonDeserializer& reader) {
	char const* tagName;
	std::string path;
	uint32_t date = 0;
	uint32_t time = 0;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "path")) {
			reader.readTagOrAttributeValueString(path);
		}
		else if (!strcmp(tagName, "date")) {
			date = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "time")) {
			time = reader.readTagOrAttributeValueInt();
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	if (path.empty()) {
		return true; // nothing asked for
	}
	D_PRINTLN(path.c_str());
	auto timestamp = timestampFromWireDateTime(date, time);

	// TWO operations in sequence, so this is the one handler that needs a stage. Stage 0 creates the
	// directory; stage 1 stamps it, and only if the creation succeeded and a timestamp was supplied --
	// exactly the condition the synchronous form tested.
	if (!deluge::storage::pathOpPending()) {
		(void)deluge::storage::pathOpStart(deluge::storage::PathOp::MkdirAll, path.c_str(), nullptr, {});
		deluge::storage::pathOpSetStage(0);
		return false;
	}
	auto outcome = deluge::storage::pathOpCollect();
	if (!outcome.has_value()) {
		return false;
	}
	deluge::io::Status status = *outcome;

	if (deluge::storage::pathOpStage() == 0 && status == deluge::io::Status::OK && timestamp.has_value()) {
		deluge::storage::pathOpClear();
		(void)deluge::storage::pathOpStart(deluge::storage::PathOp::SetTime, path.c_str(), nullptr, *timestamp);
		deluge::storage::pathOpSetStage(1);
		return false; // the stamp's own outcome is what the reply carries
	}
	deluge::storage::pathOpClear();

	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^mkdir", false, true);
	jWriter.writeAttribute("path", path.c_str());
	jWriter.writeAttribute("err", static_cast<uint8_t>(toWireStatus(status)));
	jWriter.closeTag(true);
	sendMsg(cable, jWriter);
	return true;
}

bool smSysex::rename(MIDICable& cable, JsonDeserializer& reader) {
	char const* tagName;
	std::string fromName;
	std::string toName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "from")) {
			reader.readTagOrAttributeValueString(fromName);
		}
		else if (!strcmp(tagName, "to")) {
			reader.readTagOrAttributeValueString(toName);
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	if (fromName.empty() || toName.empty()) {
		return true; // nothing asked for
	}
	{
		D_PRINTLN(fromName.c_str());
		D_PRINTLN(toName.c_str());
		if (!deluge::storage::pathOpPending()) {
			(void)deluge::storage::pathOpStart(deluge::storage::PathOp::Rename, fromName.c_str(), toName.c_str(), {});
			return false;
		}
		auto outcome = deluge::storage::pathOpCollect();
		if (!outcome.has_value()) {
			return false;
		}
		deluge::storage::pathOpClear();

		FsWireStatus errCode = toWireStatus(*outcome);
		startReply(jWriter, reader);
		jWriter.writeOpeningTag("^rename", false, true);
		jWriter.writeAttribute("from", fromName.c_str());
		jWriter.writeAttribute("to", toName.c_str());
		jWriter.writeAttribute("err", static_cast<uint8_t>(errCode));
		jWriter.closeTag(true);
		sendMsg(cable, jWriter);
	}
	return true;
}

// Returns a block of directory entries as a Json array.
void smSysex::getDirEntries(MIDICable& cable, JsonDeserializer& reader) {
	std::string path;
	path = "/";
	uint32_t lineOffset = 0;
	uint32_t linesWanted = 20;

	FsWireStatus errCode = FsWireStatus::Ok;
	char const* tagName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "offset")) {
			lineOffset = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "lines")) {
			linesWanted = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "path")) {
			reader.readTagOrAttributeValueString(path);
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');
	if (linesWanted > MAX_DIR_LINES)
		linesWanted = MAX_DIR_LINES;
	// We should pick up on path changes and out-of-order offset requests.

	if (lineOffset == 0 || activeDirName != path || lineOffset != dirOffsetCounter) {
		auto opened = deluge::io::Directory::open(path);
		if (!opened.has_value()) {
			errCode = toWireStatus(opened.error());
			goto errorFound;
		}
		sxDir = std::move(*opened);
		dirOffsetCounter = 0;
		activeDirName = path;
		if (lineOffset > 0) {
			for (uint32_t ix = 0; ix < lineOffset; ++ix) {
				auto entry = sxDir->read();
				if (!entry.has_value()) {
					errCode = toWireStatus(entry.error());
					break;
				}
				if (!entry->has_value()) {
					break;
				}
				dirOffsetCounter++;
			}
		}
	}
errorFound:;
	jWriter.reset();
	jWriter.setMemoryBased();
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^dir", false, true);
	jWriter.writeArrayStart("list", true, false);

	for (uint32_t ix = 0; ix < linesWanted && sxDir.has_value(); ++ix) {
		auto entry = sxDir->read();
		if (!entry.has_value() || !entry->has_value()) {
			break;
		}
		const DelugeDirEntry& fno = **entry;

		jWriter.writeOpeningTag(NULL, true);
		jWriter.writeAttribute("name", fno.name);
		jWriter.writeAttribute("size", fno.size);
		jWriter.writeAttribute("date", wireDateFromTimestamp(fno.modified_time));
		jWriter.writeAttribute("time", wireTimeFromTimestamp(fno.modified_time));

		// AM_RDO  0x01 Read only
		// AM_HID  0x02 Hidden
		// AM_SYS  0x04 System

		// AM_DIR  0x10 Directory
		// AM_ARC  0x20 Archive
		jWriter.writeAttribute("attr", wireAttribFromFlags(fno));

		jWriter.closeTag();
		dirOffsetCounter++;
	}
	jWriter.writeArrayEnding("list", true, false);
	jWriter.writeAttribute("err", static_cast<uint8_t>(errCode));
	jWriter.closeTag(true);
	sendMsg(cable, jWriter);
}

bool smSysex::readBlock(MIDICable& cable, JsonDeserializer& reader) {
	char const* tagName;
	uint32_t addr = 0;
	uint32_t size = blockBufferMax;
	int32_t fid = 0;

	auto repSN = reader.getReplySeqNum();
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "fid")) {
			fid = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "addr")) {
			addr = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "size")) {
			size = reader.readTagOrAttributeValueInt();
			if (size > blockBufferMax)
				size = blockBufferMax;
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	FILdata* fp = entryForFID(fid);
	FsWireStatus errCode = FsWireStatus::Ok;

	if (fp == nullptr || !fp->fileOpen) {
		errCode = FsWireStatus::NotEnabled;
	}
	uint8_t* srcAddr = (uint8_t*)addr;
	if (errCode == FsWireStatus::Ok) {
		if (!readBlockBuffer && fp) {
			readBlockBuffer = (uint8_t*)deluge::memory::alloc_sdram(blockBufferMax);
		}

		if (readBlockBuffer && fp) {
			noteFileIdUse(fp);
			// One request carrying the path and the address. The seek is gone: the operation is
			// offset-addressed, so there is no position to move and nothing held open between blocks.
			if (!deluge::storage::pathOpPending()) {
				(void)deluge::storage::pathOpStartBlock(deluge::storage::PathOp::ReadAt, fp->fName.c_str(), addr,
				                                        nullptr, size);
				return false;
			}
			uint32_t got = 0;
			auto outcome = deluge::storage::pathOpCollectBlock(readBlockBuffer, &got);
			if (!outcome.has_value()) {
				return false;
			}
			deluge::storage::pathOpClear();
			if (*outcome == deluge::io::Status::OK) {
				// A SHORT count is end-of-file, not a failure -- the client detects the end that way.
				size = got;
				srcAddr = readBlockBuffer;
				fp->fPosition = addr + got;
			}
			else {
				size = 0;
			}
			errCode = toWireStatus(*outcome);
		}
	}
	else {
		size = 0;
	}
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^read", false, true);
	jWriter.writeAttribute("fid", fid);
	jWriter.writeAttribute("addr", addr);
	jWriter.writeAttribute("size", size);
	jWriter.writeAttribute("err", static_cast<uint8_t>(errCode));
	jWriter.closeTag(true);

	jWriter.writeByte(0); // spacer between Json and encoded block.

	uint8_t working[8];
	if (size == 0) {
		D_PRINTLN("Read size 0");
	}
	for (uint32_t ix = 0; ix < size; ix += 7) {
		int pktSize = 7;
		if (ix + pktSize > size) {
			pktSize = size - ix;
		}
		uint8_t hiBits = 0;
		uint8_t rotBit = 1;
		for (int i = 1; i <= pktSize; ++i) {
			working[i] = (*srcAddr) & 0x7F;
			if ((*srcAddr) & 0x80) {
				hiBits |= rotBit;
			}
			srcAddr++;
			rotBit <<= 1;
		}
		working[0] = hiBits;
		jWriter.writeBlock(working, pktSize + 1);
	}
	sendMsg(cable, jWriter);
	return true;
}

bool smSysex::writeBlock(MIDICable& cable, JsonDeserializer& reader) {
	char const* tagName;
	uint32_t fileId = 0;
	uint32_t addr = 0;
	uint32_t size = blockBufferMax;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "addr")) {
			addr = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "size")) {
			size = reader.readTagOrAttributeValueInt();
			if (size > blockBufferMax)
				size = blockBufferMax;
		}
		else if (!strcmp(tagName, "fid")) {
			fileId = reader.readTagOrAttributeValueInt();
		}
		else {
			reader.exitTag();
		}
	}
	if (!writeBlockBuffer) {
		writeBlockBuffer = (uint8_t*)deluge::memory::alloc_sdram(blockBufferMax);
	}
	reader.match('}');
	reader.match('}'); // skip box too.

	char aChar;
	if (reader.peekChar(&aChar) && aChar != 0) {
		D_PRINTLN("Missing Separater error in writeBlock");
	}
	uint32_t decodedSize = decodeDataFromReader(reader, writeBlockBuffer, size);
	D_PRINTLN("Decoded block len: %d", decodedSize);

	FsWireStatus errCode = FsWireStatus::Ok;
	FILdata* fp = entryForFID(fileId);

	if (fp == nullptr || !fp->fileOpen) {
		errCode = FsWireStatus::NotEnabled;
	}
	if (writeBlockBuffer && (fp != nullptr) && fp->fileOpen) {
		// One request carrying the path, the address and the decoded bytes. The decode above happens
		// on every entry, which is why it must stay before this: a resumed handler re-parses and
		// re-decodes into the same buffer, and the request copies from it.
		if (!deluge::storage::pathOpPending()) {
			(void)deluge::storage::pathOpStartBlock(deluge::storage::PathOp::WriteAt, fp->fName.c_str(), addr,
			                                        writeBlockBuffer, decodedSize);
			return false;
		}
		auto outcome = deluge::storage::pathOpCollectBlock(nullptr, nullptr);
		if (!outcome.has_value()) {
			return false;
		}
		const uint32_t written = deluge::storage::pathOpResultValue();
		deluge::storage::pathOpClear();
		if (*outcome == deluge::io::Status::OK) {
			noteFileIdUse(fp);
			size = written;
			fp->fPosition = addr + written;
		}
		errCode = toWireStatus(*outcome);
	}
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^write", false, true);
	jWriter.writeAttribute("fid", fileId);
	jWriter.writeAttribute("addr", addr);
	jWriter.writeAttribute("size", size);
	jWriter.writeAttribute("err", static_cast<uint8_t>(errCode));
	jWriter.closeTag(true);

	sendMsg(cable, jWriter);
	return true;
}

bool smSysex::updateTime(MIDICable& cable, JsonDeserializer& reader) {
	char const* tagName;
	uint32_t date = 0;
	uint32_t time = 0;
	std::string path;

	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "path")) {
			reader.readTagOrAttributeValueString(path);
		}
		else if (!strcmp(tagName, "date")) {
			date = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "time")) {
			time = reader.readTagOrAttributeValueInt();
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	FsWireStatus errCode;
	auto timestamp = timestampFromWireDateTime(date, time);
	if (!path.empty() && timestamp.has_value()) {
		if (!deluge::storage::pathOpPending()) {
			(void)deluge::storage::pathOpStart(deluge::storage::PathOp::SetTime, path.c_str(), nullptr, *timestamp);
			return false;
		}
		auto outcome = deluge::storage::pathOpCollect();
		if (!outcome.has_value()) {
			return false;
		}
		deluge::storage::pathOpClear();
		errCode = toWireStatus(*outcome);
	}
	else {
		// No I/O at all on this path, so nothing to wait for: reply immediately, as before.
		errCode = FsWireStatus::InvalidParameter;
	}
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^utime", false, true);
	jWriter.writeAttribute("err", static_cast<uint8_t>(errCode));
	jWriter.closeTag(true);
	sendMsg(cable, jWriter);
	return true;
}

// A session ID or sid is a number used by clients to keep track of which messages belong to who.
void smSysex::assignSession(MIDICable& cable, JsonDeserializer& reader) {
	char const* tagName;
	std::string tag;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "tag")) {
			reader.readTagOrAttributeValueString(tag);
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	int sessionNum = 0;
	uint32_t minSeen = session_mono_counter;
	for (int i = 1; i <= MAX_SYSEX_SESSIONS; ++i) {
		if (sessionLRU_array[i] == 0) {
			sessionNum = i;
			break;
		}
		if (sessionLRU_array[i] < minSeen) {
			minSeen = sessionLRU_array[i];
			sessionNum = i;
		}
	}
	// Note the sessionNum as MRU to claim it.
	sessionLRU_array[sessionNum] = session_mono_counter++;

	startDirect(jWriter);
	jWriter.writeOpeningTag("^session", false, true);
	jWriter.writeAttribute("sid", sessionNum);
	jWriter.writeAttribute("tag", tag.c_str());
	jWriter.writeAttribute("midBase", sessionNum << SYSEX_SESSION_SHIFT);
	jWriter.writeAttribute("midMin", (sessionNum << SYSEX_SESSION_SHIFT) + 1);
	jWriter.writeAttribute("midMax", (sessionNum << SYSEX_SESSION_SHIFT) + SYSEX_MSGID_MAX);
	jWriter.closeTag(true);
	sendMsg(cable, jWriter);
}

void smSysex::doPing(MIDICable& cable, JsonDeserializer& reader) {
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^ping", false, true);
	jWriter.closeTag(true);
	sendMsg(cable, jWriter);
}

uint32_t smSysex::decodeDataFromReader(JsonDeserializer& reader, uint8_t* dest, uint32_t destMax) {
	char zip = 0;
	if (!reader.readChar(&zip) || zip) // skip separator, fail if not there.
		return 0;
	uint32_t encodedSize = reader.bytesRemainingInBuffer() - 1; // don't count that 0xF7.
	uint32_t amount = unpack_7bit_to_8bit(dest, destMax, (uint8_t*)reader.GetCurrentAddressInBuffer(), encodedSize);
	return amount;
}

void smSysex::sysexReceived(MIDICable& cable, uint8_t* data, int32_t len) {
	if (len < 3) {
		return;
	}

	SysExDataEntry& de = SysExQ.emplace_back(cable, len);
	memcpy(de.data, data, len);
}

// smSysex is a namespace (no instance state), so what little state the pump needs lives at file
// scope. processFrontSysEx() is the parse+handle+reply body; handleNextSysEx() is the per-tick
// trigger.
namespace smSysex {
void processFrontSysEx();
}

void smSysex::handleNextSysEx() {

	if (SysExQ.empty()) {
		return;
	}
	if (deluge::sync::sd_busy()) {
		return;
	}

	// Called DIRECTLY rather than dispatched onto the storage owner, and that is the whole point of
	// this conversion: `usingByteChannel()` is false on the owner by design, so an owner-side handler
	// would take the synchronous arm and nothing would be relocated. Parsing and replying touch no
	// card; each path operation goes through the channel, or through `runOnStorageOwner` when it will
	// not -- see `smsysex_pending.h`.
	//
	// The single-flight guard that used to sit here is gone WITH the dispatch, and the two must go
	// together. A direct call cannot overlap itself, so the guard is redundant here -- but restoring
	// the dispatch without it makes a second tick process a queue the first already popped, which
	// aborts in `SysExQ.front()`. That was confirmed by trying exactly that. The pending path operation
	// is the single-flight state now, and the front entry staying unpopped is what makes the next tick
	// resume the same request rather than start another.
	//
	// `sd_busy()` stays, though it now guards less than it did -- neither arm touches FatFS from this
	// stack. Keeping it is one fewer behaviour change, and it was silently dead once before and had to
	// be fixed, so removing a live one deserves its own deliberate decision.
	processFrontSysEx();
}

void smSysex::processFrontSysEx() {

	SysExDataEntry& de = SysExQ.front();

	char const* tagName;
	uint8_t msgSeqNum = de.data[1];
	noteSessionIdUse(msgSeqNum);
	JsonDeserializer parser(de.data + 2, de.len - 2);
	parser.setReplySeqNum(msgSeqNum);

	// `done` is false only while a converted handler has a path operation in flight. Every other
	// handler finishes within its tick, exactly as all of them used to.
	bool done = true;

	parser.match('{');
	while (*(tagName = parser.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "open")) {
			done = openFile(de.cable, parser);
			goto finished;
		}
		else if (!strcmp(tagName, "close")) {
			done = closeFile(de.cable, parser);
			goto finished;
		}
		else if (!strcmp(tagName, "dir")) {
			getDirEntries(de.cable, parser);
			goto finished;
		}
		else if (!strcmp(tagName, "read")) {
			done = readBlock(de.cable, parser);
			goto finished;
		}
		else if (!strcmp(tagName, "write")) {
			done = writeBlock(de.cable, parser);
			goto finished; // Already skipped end.
		}
		else if (!strcmp(tagName, "delete")) {
			done = deleteFile(de.cable, parser);
			goto finished;
		}
		else if (!strcmp(tagName, "mkdir")) {
			done = createDirectory(de.cable, parser);
			goto finished;
		}
		else if (!strcmp(tagName, "rename")) {
			done = rename(de.cable, parser);
			goto finished;
		}
		else if (!strcmp(tagName, "copy")) {
			copyFile(de.cable, parser);
			goto finished;
		}
		else if (!strcmp(tagName, "move")) {
			moveFile(de.cable, parser);
			goto finished;
		}
		else if (!strcmp(tagName, "utime")) {
			done = updateTime(de.cable, parser);
			goto finished;
		}
		else if (!strcmp(tagName, "session")) {
			assignSession(de.cable, parser);
			goto finished;
		}
		else if (!strcmp(tagName, "ping")) {
			doPing(de.cable, parser);
			goto finished;
		}
		parser.exitTag();
	}
finished:
	// Popped only when the handler says so. Leaving the entry in place is what makes the next tick
	// re-enter this same request -- it re-parses the unchanged bytes and finds its pending operation.
	if (done) {
		SysExQ.pop_front();
	}
}

// Helper function to parse file operation parameters
bool smSysex::parseFileOpParams(JsonDeserializer& reader, FileOpParams& params) {
	char const* tagName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "from")) {
			reader.readTagOrAttributeValueString(params.fromName);
		}
		else if (!strcmp(tagName, "to")) {
			reader.readTagOrAttributeValueString(params.toName);
		}
		else if (!strcmp(tagName, "date")) {
			params.date = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "time")) {
			params.time = reader.readTagOrAttributeValueInt();
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	return !params.fromName.empty() && !params.toName.empty();
}

// Helper function to set file timestamp
void smSysex::setFileTimestamp(std::string_view path, uint32_t date, uint32_t time) {
	auto timestamp = timestampFromWireDateTime(date, time);
	if (timestamp.has_value()) {
		(void)deluge::io::set_time(path, *timestamp);
	}
}

// Helper function to perform file copy operation
FsWireStatus smSysex::performFileCopy(const FileOpParams& params) {
	D_PRINTLN(params.fromName.c_str());
	D_PRINTLN(params.toName.c_str());

	bool pathCreateTried = false;
	for (;;) {
		auto src = deluge::io::File::open(params.fromName, DELUGE_FILE_READ);
		if (!src.has_value()) {
			return toWireStatus(src.error());
		}

		auto dst = deluge::io::File::open(params.toName, DELUGE_FILE_WRITE_CREATE);
		if (!dst.has_value()) {
			if (dst.error() == deluge::io::Status::NOT_FOUND && !pathCreateTried) {
				// Don't set timestamps on directories - let them use current time
				std::string toNameCopy = params.toName;
				createPathDirectories(toNameCopy, std::nullopt);
				pathCreateTried = true;
				continue; // `src` closes via RAII at the end of this iteration
			}
			return toWireStatus(dst.error());
		}

		deluge::io::Status status = deluge::io::Status::OK;
		if (!readBlockBuffer) {
			readBlockBuffer = (uint8_t*)deluge::memory::alloc_sdram(blockBufferMax);
		}
		if (readBlockBuffer) {
			for (;;) {
				auto bytesRead = src->read(std::span{reinterpret_cast<std::byte*>(readBlockBuffer), blockBufferMax});
				if (!bytesRead.has_value()) {
					status = bytesRead.error();
					break;
				}
				if (bytesRead->empty()) {
					break;
				}
				auto bytesWritten = dst->write(*bytesRead);
				if (!bytesWritten.has_value()) {
					status = bytesWritten.error();
					break;
				}
				if (*bytesWritten != bytesRead->size()) {
					// preserving pre-existing quirk: a short write here is not
					// itself flagged as an error, `status` stays OK.
					break;
				}
				if (bytesRead->size() < blockBufferMax) {
					break;
				}
			}
		}
		else {
			status = deluge::io::Status::NO_MEMORY;
		}

		if (status == deluge::io::Status::OK && params.hasTimestamp()) {
			setFileTimestamp(params.toName, params.date, params.time);
		}
		return toWireStatus(status);
	}
}

void smSysex::copyFile(MIDICable& cable, JsonDeserializer& reader) {
	FileOpParams params;

	if (!parseFileOpParams(reader, params)) {
		return;
	}

	FsWireStatus errCode = performFileCopy(params);

	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^copy", false, true);
	jWriter.writeAttribute("from", params.getFromPath());
	jWriter.writeAttribute("to", params.getToPath());
	jWriter.writeAttribute("err", static_cast<uint8_t>(errCode));
	jWriter.closeTag(true);
	sendMsg(cable, jWriter);
}

void smSysex::moveFile(MIDICable& cable, JsonDeserializer& reader) {
	FileOpParams params;

	if (!parseFileOpParams(reader, params)) {
		return;
	}

	D_PRINTLN(params.fromName.c_str());
	D_PRINTLN(params.toName.c_str());

	// Try rename first (works if source and destination are on same filesystem)
	auto renamed = deluge::io::rename(params.fromName, params.toName);
	FsWireStatus errCode = toWireStatus(renamed.has_value() ? deluge::io::Status::OK : renamed.error());

	// If rename failed due to missing path, try creating directories
	if (!renamed.has_value() && renamed.error() == deluge::io::Status::NOT_FOUND) {
		// Don't set timestamps on directories - let them use current time
		std::string toNameCopy = params.toName;
		createPathDirectories(toNameCopy, std::nullopt);
		renamed = deluge::io::rename(params.fromName, params.toName);
		errCode = toWireStatus(renamed.has_value() ? deluge::io::Status::OK : renamed.error());
	}

	// If rename still fails (e.g., cross-filesystem move), fall back to copy+delete
	if (!renamed.has_value()) {
		// Use the shared copy function
		errCode = performFileCopy(params);

		// If copy was successful, delete the source file
		if (errCode == FsWireStatus::Ok) {
			auto deleted = deluge::io::unlink(params.fromName);

			// For move operation, both copy and delete must succeed
			if (!deleted.has_value()) {
				FsWireStatus deleteResult = toWireStatus(deleted.error());
				D_PRINTLN("Move: copy succeeded but delete failed: %d", static_cast<uint8_t>(deleteResult));
				// Clean up the destination file since move failed
				(void)deluge::io::unlink(params.toName);
				errCode = deleteResult;
			}
		}
	}
	else {
		// Rename was successful, set timestamp if provided
		if (params.hasTimestamp()) {
			setFileTimestamp(params.toName, params.date, params.time);
		}
	}

	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^move", false, true);
	jWriter.writeAttribute("from", params.getFromPath());
	jWriter.writeAttribute("to", params.getToPath());
	jWriter.writeAttribute("err", static_cast<uint8_t>(errCode));
	jWriter.closeTag(true);
	sendMsg(cable, jWriter);
}
