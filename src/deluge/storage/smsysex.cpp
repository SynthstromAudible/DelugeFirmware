#include "storage/smsysex.h"
#include "fatfs/ff.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/oled.h"
#include "hid/display/seven_segment.h"
#include "hid/hid_sysex.h"
#include "io/debug/log.h"
#include "io/debug/print.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/sysex.h"
#include "memory/general_memory_allocator.h"
#include "processing/engines/audio_engine.h"
#include "task_scheduler.h"
#include "util/containers.h"
#include "util/pack.h"
#include <cstring>

#define MAX_DIR_LINES 25

extern "C" {
extern uint8_t currentlyAccessingCard;
}
DIR sxDIR;
uint32_t offsetCounter;

JsonSerializer jWriter;
String activeDirName;
const size_t blockBufferMax = 1024;
const size_t sysexBufferMax = blockBufferMax + 256;
uint8_t* writeBlockBuffer = NULL;
uint8_t* readBlockBuffer = NULL;
const uint32_t MAX_OPEN_FILES = 4;

struct FILdata {
	String fName;
	int32_t filID;
	uint32_t fSize = 0;
	bool fileOpen = false;
	bool forWrite = false;
	FIL file;
};

FILdata openFiles[MAX_OPEN_FILES];

const int MaxSysExLength = 1024;

struct SysExDataEntry {
	MIDIDevice* device;
	int32_t len;
	uint8_t data[sysexBufferMax];
};

deluge::deque<SysExDataEntry> SysExQ;

// Assign a FIL from our pool.
int32_t smSysex::findEmptyFIL() {
	for (int i = 0; i < MAX_OPEN_FILES; ++i)
		if (!openFiles[i].fileOpen)
			return i + 1;
	return -1;
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

void smSysex::sendMsg(MIDIDevice* device, JsonSerializer& writer) {
	writer.writeByte(0xF7);

	char* bitz = writer.getBufferPtr();
	int32_t bw = writer.bytesWritten();
	device->sendSysex((const uint8_t*)bitz, bw);
}

int smSysex::openFIL(const char* fPath, bool forWrite, uint32_t* fsize, FRESULT* eCode) {
	int32_t fx = findEmptyFIL();
	if (fx == -1)
		return -1;

	FILdata* fp = openFiles + (fx - 1);
	fp->fName.set(fPath);
	BYTE mode = FA_READ;
	if (forWrite)
		mode = FA_CREATE_ALWAYS | FA_WRITE;
	FRESULT err = f_open(&fp->file, fPath, mode);
	*eCode = err;
	if (err == FRESULT::FR_OK) {
		fp->fileOpen = true;
		fp->forWrite = forWrite;
		fp->fSize = f_size(&fp->file);
		return fx;
	}
	return -1;
}

FRESULT smSysex::closeFIL(int fx) {
	if (fx < 0 || fx > MAX_OPEN_FILES)
		return FRESULT::FR_INVALID_OBJECT;
	FILdata* fp = openFiles + (fx - 1);
	FRESULT err = f_close(&fp->file);
	fp->fileOpen = false;
	fp->forWrite = false;
	fp->fSize = 0;
	return err;
}

// Fill in missing directories for the full path name given.
// Unless the last character in the path is a /, we assume the
// path given ends with a filename (which we ignore).
FRESULT smSysex::createPathDirectories(String& path, uint32_t date, uint32_t time) {

	FRESULT errCode;
	if (path.getLength() > 256) {
		return FRESULT::FR_INVALID_PARAMETER;
	}

	char working[257];
	char pathPart[257];
	strcpy(working, path.get());
	// ignore the file name and extension part.
	int len = strlen(working);
	int lastSlash;
	for (lastSlash = len - 1; lastSlash >= 0; lastSlash--) {
		if (working[lastSlash] == '/')
			break;
	}
	if (lastSlash == 0)
		return FRESULT::FR_INVALID_PARAMETER;

	int jx = 1; // skip the leading slash.
	while (jx <= lastSlash) {
		if (working[jx] == '/') {
			DIR wDIR;
			working[jx] = 0;
			strcpy(pathPart, working);
			working[jx] = '/';
			if (strlen(pathPart)) {
				errCode = f_opendir(&wDIR, (TCHAR*)pathPart);
				if (errCode == FRESULT::FR_NO_PATH) {
					errCode = f_mkdir((TCHAR*)pathPart);
					if (errCode == FRESULT::FR_OK && (date != 0 || time != 0)) {
						FILINFO finfo;
						finfo.fdate = date;
						finfo.ftime = time;
						errCode = f_utime(pathPart, &finfo);
					}
				}
				else if (errCode == 0) {
					errCode = f_closedir(&wDIR);
				}
				else {
					return errCode;
				}
			}
		}
		jx++;
	}
	return errCode;
}

void smSysex::openFile(MIDIDevice* device, JsonDeserializer& reader) {
	bool forWrite = false;
	String path;
	int32_t rn = 0;
	char const* tagName;
	uint32_t date = 0;
	uint32_t time = 0;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "write")) {
			forWrite = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "path")) {
			reader.readTagOrAttributeValueString(&path);
		}
		// Since you can't change the date/time of an open file, we use date/time
		// only for created directoris.
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
	bool pathCreateTried = false;
retry:
	FRESULT errCode;
	uint32_t fSize = 0;

	int fd = openFIL(path.get(), forWrite, &fSize, &errCode);
	FILdata* fp = NULL;
	if (fd > 0)
		fp = openFiles + (fd - 1);
	if (fp != NULL) {
		fSize = fp->fSize;
	}
	if (forWrite && !pathCreateTried && errCode == FRESULT::FR_NO_PATH) { // was the path missing?
		createPathDirectories(path, date, time);
		pathCreateTried = true;
		goto retry;
	}

	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^open", false, true);
	jWriter.writeAttribute("fid", fd);
	jWriter.writeAttribute("size", fSize);
	jWriter.writeAttribute("err", errCode);
	jWriter.closeTag(true);

	sendMsg(device, jWriter);
}

void smSysex::closeFile(MIDIDevice* device, JsonDeserializer& reader) {
	int32_t fd = 0;
	char const* tagName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "fid")) {
			fd = reader.readTagOrAttributeValueInt();
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	FRESULT errCode = closeFIL(fd);

	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^close", false, true);
	jWriter.writeAttribute("fid", fd);
	jWriter.writeAttribute("err", errCode);
	jWriter.closeTag(true);

	sendMsg(device, jWriter);
}

void smSysex::deleteFile(MIDIDevice* device, JsonDeserializer& reader) {
	FRESULT errCode = FRESULT::FR_OK;

	char const* tagName;
	String path;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "path")) {
			reader.readTagOrAttributeValueString(&path);
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	const char* pathVal = path.get();
	const TCHAR* pathTC = (const TCHAR*)pathVal;

	if (pathTC && strlen(pathTC) > 0) {
		D_PRINTLN(pathTC);
		errCode = f_unlink(pathTC);
		startReply(jWriter, reader);
		jWriter.writeOpeningTag("^delete", false, true);
		jWriter.writeAttribute("err", errCode);
		jWriter.closeTag(true);
		sendMsg(device, jWriter);
	}
}

void smSysex::createDirectory(MIDIDevice* device, JsonDeserializer& reader) {
	FRESULT errCode = FRESULT::FR_OK;

	char const* tagName;
	String path;
	uint32_t date = 0;
	uint32_t time = 0;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "path")) {
			reader.readTagOrAttributeValueString(&path);
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

	const char* pathVal = path.get();
	const TCHAR* pathTC = (const TCHAR*)pathVal;

	if (pathTC && strlen(pathTC) > 0) {
		D_PRINTLN(pathTC);
		errCode = f_mkdir(pathTC);
		if (errCode == FRESULT::FR_OK && (date != 0 || time != 0)) {
			FILINFO finfo;
			finfo.fdate = date;
			finfo.ftime = time;
			errCode = f_utime(pathTC, &finfo);
		}
		startReply(jWriter, reader);
		jWriter.writeOpeningTag("^mkdir", false, true);
		jWriter.writeAttribute("path", pathVal);
		jWriter.writeAttribute("err", errCode);
		jWriter.closeTag(true);
		sendMsg(device, jWriter);
	}
}

void smSysex::rename(MIDIDevice* device, JsonDeserializer& reader) {
	FRESULT errCode = FRESULT::FR_OK;

	char const* tagName;
	String fromName;
	String toName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "from")) {
			reader.readTagOrAttributeValueString(&fromName);
		}
		else if (!strcmp(tagName, "to")) {
			reader.readTagOrAttributeValueString(&toName);
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	const char* fromVal = fromName.get();
	const TCHAR* fromTC = (const TCHAR*)fromVal;
	const char* toVal = toName.get();
	const TCHAR* toTC = (const TCHAR*)toVal;

	if (fromTC && strlen(fromTC) && toTC && strlen(toTC)) {
		D_PRINTLN(fromTC);
		D_PRINTLN(toTC);
		errCode = f_rename(fromTC, toTC);
		startReply(jWriter, reader);
		jWriter.writeOpeningTag("^mkdir", false, true);
		jWriter.writeAttribute("path", toName.get());
		jWriter.writeAttribute("err", errCode);
		jWriter.closeTag(true);
		sendMsg(device, jWriter);
	}
}

// Returns a block of directory entries as a Json array.
void smSysex::getDirEntries(MIDIDevice* device, JsonDeserializer& reader) {
	String path;
	path.set("/");
	uint32_t lineOffset = 0;
	uint32_t linesWanted = 20;

	FRESULT errCode = FRESULT::FR_OK;
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
			reader.readTagOrAttributeValueString(&path);
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');
	if (linesWanted > MAX_DIR_LINES)
		linesWanted = MAX_DIR_LINES;
	// We should pick up on path changes and out-of-order offset requests.

	const char* pathVal = path.get();
	const TCHAR* pathTC = (const TCHAR*)pathVal;
	if (lineOffset == 0 || strcmp(activeDirName.get(), pathVal) || lineOffset != offsetCounter) {
		errCode = f_opendir(&sxDIR, pathTC);
		if (errCode != FRESULT::FR_OK)
			goto errorFound;
		offsetCounter = 0;
		activeDirName.set(pathVal);
		if (lineOffset > 0) {
			FILINFO fno;
			for (uint32_t ix = 0; ix < lineOffset; ++ix) {
				errCode = f_readdir(&sxDIR, &fno);
				if (errCode != FRESULT::FR_OK)
					break;
				if (fno.altname[0] == 0)
					break;
				offsetCounter++;
			}
		}
	}
errorFound:;
	jWriter.reset();
	jWriter.setMemoryBased();
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^dir", false, true);
	jWriter.writeArrayStart("list", true, false);

	for (uint32_t ix = 0; ix < linesWanted; ++ix) {
		FILINFO fno;
		FRESULT err = f_readdir(&sxDIR, &fno);
		if (err != FRESULT::FR_OK)
			break;
		if (fno.altname[0] == 0)
			break;

		jWriter.writeOpeningTag(NULL, true);
		jWriter.writeAttribute("name", fno.fname);
		jWriter.writeAttribute("size", fno.fsize);
		jWriter.writeAttribute("date", fno.fdate);
		jWriter.writeAttribute("time", fno.ftime);

		// AM_RDO  0x01 Read only
		// AM_HID  0x02 Hidden
		// AM_SYS  0x04 System

		// AM_DIR  0x10 Directory
		// AM_ARC  0x20 Archive
		jWriter.writeAttribute("attr", fno.fattrib);

		jWriter.closeTag();
		offsetCounter++;
	}
	jWriter.writeArrayEnding("list", true, false);
	jWriter.writeAttribute("err", errCode);
	jWriter.closeTag(true);
	sendMsg(device, jWriter);
}

void smSysex::readBlock(MIDIDevice* device, JsonDeserializer& reader) {
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

	FILdata* fp = NULL;
	FRESULT errCode = FR_OK;
	if (fid > 0 && fid <= MAX_OPEN_FILES) {
		fp = openFiles + (fid - 1);
	}
	if (fp == NULL) {
		errCode = FRESULT::FR_NOT_ENABLED;
	}
	UINT actuallyRead = 0;
	uint8_t* srcAddr = (uint8_t*)addr;
	if (errCode == FRESULT::FR_OK) {
		if (!readBlockBuffer && fp) {
			readBlockBuffer = (uint8_t*)GeneralMemoryAllocator::get().allocLowSpeed(blockBufferMax);
		}

		if (readBlockBuffer && fp) {
			errCode = f_lseek(&fp->file, addr);
			if (errCode == FRESULT::FR_OK) {
				errCode = f_read(&fp->file, readBlockBuffer, size, &actuallyRead);
				size = actuallyRead;
				srcAddr = readBlockBuffer;
			}
			else {
				D_PRINTLN("lseek issue: %d", errCode);
			}
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
	jWriter.writeAttribute("err", errCode);
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
	sendMsg(device, jWriter);
}

void smSysex::writeBlock(MIDIDevice* device, JsonDeserializer& reader) {
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
		writeBlockBuffer = (uint8_t*)GeneralMemoryAllocator::get().allocLowSpeed(blockBufferMax);
	}
	reader.match('}');
	reader.match('}'); // skip box too.

	// We should be on the separator character, check to make
	char aChar;
	if (reader.peekChar(&aChar) && aChar != 0) {
		D_PRINTLN("Missing Separater error in writeBlock");
	}
	uint32_t decodedSize = decodeDataFromReader(reader, writeBlockBuffer, size);
	D_PRINTLN("Decoded block len: %d", decodedSize);

	// Here is where we actually write the buffer out.
	FRESULT errCode = FRESULT::FR_OK;
	FILdata* fp = NULL;

	if (fileId > 0 && fileId <= MAX_OPEN_FILES) {
		fp = openFiles + (fileId - 1);
	}
	if (fp == NULL) {
		errCode = FRESULT::FR_NOT_ENABLED;
	}
	if (writeBlockBuffer && fp) {
		// errCode = f_lseek(&fp->file, addr);
		if (errCode == FRESULT::FR_OK) {
			UINT actuallyWritten = 0;
			errCode = f_write(&fp->file, writeBlockBuffer, decodedSize, &actuallyWritten);
			size = actuallyWritten;
		}
	}
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^write", false, true);
	jWriter.writeAttribute("fid", fileId);
	jWriter.writeAttribute("addr", addr);
	jWriter.writeAttribute("size", size);
	jWriter.writeAttribute("err", errCode);
	jWriter.closeTag(true);

	sendMsg(device, jWriter);
}

void smSysex::updateTime(MIDIDevice* device, JsonDeserializer& reader) {

	FRESULT errCode;
	char const* tagName;
	uint32_t date = 0;
	uint32_t time = 0;
	String path;

	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "path")) {
			reader.readTagOrAttributeValueString(&path);
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

	if (!path.isEmpty() && (date != 0 || time != 0)) {
		FILINFO finfo;
		finfo.fdate = date;
		finfo.ftime = time;
		const char* pathVal = path.get();
		const TCHAR* pathTC = (const TCHAR*)pathVal;
		errCode = f_utime(pathTC, &finfo);
	}
	else {
		errCode = FRESULT::FR_INVALID_PARAMETER;
	}
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^utime", false, true);
	jWriter.writeAttribute("err", errCode);
	jWriter.closeTag(true);
	sendMsg(device, jWriter);
}

void smSysex::doPing(MIDIDevice* device, JsonDeserializer& reader) {
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^ping", false, true);
	jWriter.closeTag(true);
	sendMsg(device, jWriter);
}

uint32_t smSysex::decodeDataFromReader(JsonDeserializer& reader, uint8_t* dest, uint32_t destMax) {
	char zip = 0;
	if (!reader.readChar(&zip) || zip) // skip separator, fail if not there.
		return 0;
	uint32_t encodedSize = reader.bytesRemainingInBuffer() - 1; // don't count that 0xF7.
	uint32_t amount = unpack_7bit_to_8bit(dest, destMax, (uint8_t*)reader.GetCurrentAddressInBuffer(), encodedSize);
	return amount;
}

void smSysex::sysexReceived(MIDIDevice* device, uint8_t* data, int32_t len) {
	if (len < 3) {
		return;
	}

	SysExDataEntry& de = SysExQ.emplace_back();
	de.device = device;
	de.len = len;
	memcpy(de.data, data, len);
}

void smSysex::handleNextSysEx() {

	if (SysExQ.empty())
		return;
	if (currentlyAccessingCard != 0)
		return;

	SysExDataEntry& de = SysExQ.front();

	char const* tagName;
	uint8_t msgSeqNum = de.data[1];
	JsonDeserializer parser(de.data + 2, de.len - 2);
	parser.setReplySeqNum(msgSeqNum);

	parser.match('{');
	while (*(tagName = parser.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "open")) {
			openFile(de.device, parser);
			goto done;
		}
		else if (!strcmp(tagName, "close")) {
			closeFile(de.device, parser);
			goto done;
		}
		else if (!strcmp(tagName, "dir")) {
			getDirEntries(de.device, parser);
			goto done;
		}
		else if (!strcmp(tagName, "read")) {
			readBlock(de.device, parser);
			goto done;
		}
		else if (!strcmp(tagName, "write")) {
			writeBlock(de.device, parser);
			goto done; // Already skipped end.
		}
		else if (!strcmp(tagName, "delete")) {
			deleteFile(de.device, parser);
			goto done;
		}
		else if (!strcmp(tagName, "mkdir")) {
			createDirectory(de.device, parser);
			goto done;
		}
		else if (!strcmp(tagName, "rename")) {
			rename(de.device, parser);
			goto done;
		}
		else if (!strcmp(tagName, "utime")) {
			updateTime(de.device, parser);
			goto done;
		}
		else if (!strcmp(tagName, "ping")) {
			doPing(de.device, parser);
			goto done;
		}
		parser.exitTag();
	}
done:
	SysExQ.pop_front();
}
