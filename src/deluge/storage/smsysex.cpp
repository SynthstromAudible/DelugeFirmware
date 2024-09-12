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
#include "util/pack.h"
#include <cstring>

#define MAX_DIR_LINES 25
bool fileOpen = false;

DIR sxDIR;
uint32_t offsetCounter;

JsonSerializer writer;
String activeDirName;
const size_t blockBufferMax = 512;
uint8_t* blockBuffer = NULL;

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

	writer.reset();
	writer.setMemoryBased();

	uint8_t reply_hdr[6] = {0xf0, 0x00, 0x21, 0x7B, 0x01, SysEx::SysexCommands::JsonReply};
	writer.writeBlock(reply_hdr, 6);
	writer.writeArrayStart("dirlist", true, true);

	for (uint32_t ix = 0; ix < linesWanted; ++ix) {
		FILINFO fno;
		FRESULT err = f_readdir(&sxDIR, &fno);
		if (err != FRESULT::FR_OK)
			break;
		if (fno.altname[0] == 0)
			break;

		writer.writeOpeningTag(NULL, true);
		writer.writeAttribute("type", fno.fattrib & AM_DIR ? "dn" : "fn");
		writer.writeAttribute("name", fno.fname);
		writer.writeAttribute("size", fno.fsize);
		writer.closeTag();
		offsetCounter++;
	}
	writer.writeArrayEnding("dirlist", true, true);
	writer.writeByte(0xF7);
	char* bitz = writer.getBufferPtr();
	int32_t bw = writer.bytesWritten();
	device->sendSysex((const uint8_t*)bitz, bw);
}

void smSysex::sysexReceived(MIDIDevice* device, uint8_t* data, int32_t len) {
	if (len < 3) {
		return;
	}
	char const* tagName;
	JsonDeserializer parser(data + 1, len - 1);
	parser.match('{');
	while (*(tagName = parser.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "dir")) {
			getDirEntries(device, parser);
		}
		else if (!strcmp(tagName, "dump")) {
			sendMemoryBlock(device, parser);
		}
		else if (!strcmp(tagName, "put")) {
			putMemoryBlock(device, parser);
			return; // Already skipped end.
		}
		parser.exitTag();
	}
}

void smSysex::sendMemoryBlock(MIDIDevice* device, JsonDeserializer& reader) {
	char const* tagName;
	uint32_t addr = 0;
	uint32_t size = 512;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "addr")) {
			addr = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "size")) {
			size = reader.readTagOrAttributeValueInt();
		}
	}
	reader.match('}');

	writer.reset();
	writer.setMemoryBased();
	uint8_t reply_hdr[6] = {0xf0, 0x00, 0x21, 0x7B, 0x01, SysEx::SysexCommands::JsonReply};
	writer.writeBlock(reply_hdr, 6);

	writer.writeOpeningTag("dumped", false, true);
	writer.writeAttribute("addr", addr);
	writer.writeAttribute("size", size);
	writer.closeTag(true);

	writer.writeByte(0); // spacer between Json and encoded block.

	uint8_t* srcAddr = (uint8_t*)addr;
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
		writer.writeBlock(working, pktSize + 1);
	}
	writer.writeByte(0xF7);

	char* bitz = writer.getBufferPtr();
	int32_t bw = writer.bytesWritten();
	device->sendSysex((const uint8_t*)bitz, bw);
}

void smSysex::putMemoryBlock(MIDIDevice* device, JsonDeserializer& reader) {
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
		}
		else if (!strcmp(tagName, "fid")) {
			fileId = reader.readTagOrAttributeValueInt();
		}
	}
	if (!blockBuffer) {
		blockBuffer = (uint8_t*)GeneralMemoryAllocator::get().allocLowSpeed(blockBufferMax);
	}

	if (size > blockBufferMax)
		size = blockBufferMax;
	reader.match('}');
	reader.match('}'); // skip box too.
	// We should be on the separator character, check to make
	char aChar;
	if (reader.peekChar(&aChar) && aChar != 0) {
		D_PRINTLN("Missing Separater error in putBlock");
	}
	uint32_t decoded = decodeDataFromReader(reader, blockBuffer, size);
	D_PRINTLN("Decoded block len: %d", decoded);
}

uint32_t smSysex::decodeDataFromReader(JsonDeserializer& reader, uint8_t* dest, uint32_t destMax) {
	char zip = 0;
	if (!reader.readChar(&zip) || zip)
		return 0;                                               // skip separator.
	uint32_t encodedSize = reader.bytesRemainingInBuffer() - 1; // don't count 0xF7.
	uint32_t amount = unpack_7bit_to_8bit(dest, destMax, (uint8_t*)reader.GetCurrentAddressInBuffer(), encodedSize);
	return amount;
}
