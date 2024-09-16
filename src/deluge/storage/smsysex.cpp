#include "storage/smsysex.h"
#include "fatfs/ff.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/oled.h"
#include "hid/display/seven_segment.h"
#include "hid/hid_sysex.h"
#include "io/debug/log.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/sysex.h"
#include "memory/general_memory_allocator.h"
#include "processing/engines/audio_engine.h"
#include "util/containers.h"
#include "util/pack.h"
#include <cstring>

#define MAX_DIR_LINES 25

DIR sxDIR;
uint32_t offsetCounter;

JsonSerializer jWriter;
String activeDirName;
const size_t blockBufferMax = 512;
uint8_t* writeBlockBuffer = NULL;
uint8_t* readBlockBuffer = NULL;
const uint32_t MAX_OPEN_FILES = 4;

struct FILdata {
	String fName;
	int32_t filID;
	bool fileOpen = false;
	bool forWrite = false;
	FIL file;
};

FILdata openFiles[MAX_OPEN_FILES];

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

int smSysex::openFIL(const char* fPath, bool forWrite, FRESULT* eCode) {
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
	return err;
}

void smSysex::openFile(MIDIDevice* device, JsonDeserializer& reader) {
	bool forWrite = false;
	String path;
	int32_t rn = 0;
	char const* tagName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "write")) {
			forWrite = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "path")) {
			reader.readTagOrAttributeValueString(&path);
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');
	FRESULT errCode;
	int fd = openFIL(path.get(), forWrite, &errCode);
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^open", false, true);
	jWriter.writeAttribute("fid", fd);
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

// Returns a block of directory entries as a Json array.
void smSysex::getDirEntries(MIDIDevice* device, JsonDeserializer& reader) {
	String path;
	uint32_t lineOffset = 0;
	uint32_t linesWanted = 20;
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
		FRESULT err = f_opendir(&sxDIR, pathTC);
		if (err != FRESULT::FR_OK)
			return;
		offsetCounter = 0;
		activeDirName.set(pathVal);
		if (lineOffset > 0) {
			FILINFO fno;
			for (uint32_t ix = 0; ix < lineOffset; ++ix) {
				FRESULT err = f_readdir(&sxDIR, &fno);
				if (err != FRESULT::FR_OK)
					break;
				if (fno.altname[0] == 0)
					break;
				offsetCounter++;
			}
		}
	}

	jWriter.reset();
	jWriter.setMemoryBased();
	startReply(jWriter, reader);
	jWriter.writeArrayStart("^dir", true, true);

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
		// Maybe add fdate and ftime?

		// AM_RDO  0x01 Read only
		// AM_HID  0x02 Hidden
		// AM_SYS  0x04 System
		// AM_DIR  0x10 Directory
		// AM_ARC  0x20 Archive
		jWriter.writeAttribute("attr", fno.fattrib);

		jWriter.closeTag();
		offsetCounter++;
	}
	jWriter.writeArrayEnding("^dir", true, true);
	sendMsg(device, jWriter);
}

void smSysex::readBlock(MIDIDevice* device, JsonDeserializer& reader) {
	char const* tagName;
	uint32_t addr = 0;
	uint32_t size = 512;
	int32_t fid = 0;

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

	UINT actuallyRead = 0;
	uint8_t* srcAddr = (uint8_t*)addr;

	if (!readBlockBuffer && fp) {
		readBlockBuffer = (uint8_t*)GeneralMemoryAllocator::get().allocLowSpeed(blockBufferMax);
	}

	if (readBlockBuffer && fp) {
		errCode = f_read(&fp->file, readBlockBuffer, size, &actuallyRead);
		size = actuallyRead;
		srcAddr = readBlockBuffer;
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
	uint32_t size = 512;
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
		D_PRINTLN("Missing Separater error in putBlock");
	}
	uint32_t decoded = decodeDataFromReader(reader, writeBlockBuffer, size);
	D_PRINTLN("Decoded block len: %d", decoded);
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
	char const* tagName;
	uint8_t msgSeqNum = data[1];
	JsonDeserializer parser(data + 2, len - 2);
	parser.setReplySeqNum(msgSeqNum);

	parser.match('{');
	while (*(tagName = parser.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "open")) {
			openFile(device, parser);
			return;
		}
		else if (!strcmp(tagName, "close")) {
			closeFile(device, parser);
			return;
		}
		else if (!strcmp(tagName, "dir")) {
			getDirEntries(device, parser);
			return;
		}
		else if (!strcmp(tagName, "read")) {
			readBlock(device, parser);
			return;
		}
		else if (!strcmp(tagName, "write")) {
			writeBlock(device, parser);
			return; // Already skipped end.
		}
		parser.exitTag();
	}
}
