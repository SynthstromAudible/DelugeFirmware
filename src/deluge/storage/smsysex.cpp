#include "hid/hid_sysex.h"
#include "fatfs/ff.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/oled.h"
#include "hid/display/seven_segment.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/sysex.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "processing/engines/audio_engine.h"
#include "storage/smsysex.h"
#include "util/pack.h"
#include <cstring>


#define MAX_DIR_LINES 20
bool fileOpen = false;
FILINFO sxFIL;
JsonSerializer writer;

// Returns a block of directory entries as a tab-separated string
void smSysex::getDirEntries(MIDIDevice* device, Deserializer &reader) {
	String path;
	uint32_t lineOffset = 0;
	uint32_t linesWanted = 20;
	char const* tagName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "offset")) {
			lineOffset = reader.readTagOrAttributeValueInt();

		} else if (!strcmp(tagName, "lines")) {
			linesWanted = reader.readTagOrAttributeValueInt();

		} else if (!strcmp(tagName, "path")) {
			reader.readTagOrAttributeValueString(&path);
		}
	}
	reader.match('}');
	if (linesWanted > MAX_DIR_LINES) linesWanted = MAX_DIR_LINES;

	D_PRINTLN("PATH: %s", path.get());

	DIR dir;
	FILINFO fno;
	const char* pathVal =  path.get();
	const TCHAR* pathTC = (const TCHAR*) pathVal;
	FRESULT err = f_opendir(&dir, pathTC);
	if (err != FRESULT::FR_OK) return;

	writer.reset();
	writer.setMemoryBased();

	uint8_t reply_hdr[6] =  {0xf0, 0x00, 0x21, 0x7B, 0x01, SysEx::SysexCommands::JsonReply};
	writer.writeBlock(reply_hdr, 6);
	writer.writeArrayStart("dirlist", true, true);

	// skip to offset of interest
	for (uint32_t ix = 0; ix < lineOffset; ++ix) {
		err = f_readdir(&dir, &fno);
		if (err != FRESULT::FR_OK) break;
	}

	for (uint32_t ix = 0; ix < linesWanted; ++ix) {
		err = f_readdir(&dir, &fno);
		if (err != FRESULT::FR_OK) break;
		if (fno.altname[0] == 0) break;

		//if (fno.fattrib & AM_DIR) {
		//	D_PRINTLN("DIR:  %s", fno.fname);
		//} else {
		//	D_PRINTLN("FILE: %s", fno.fname);
		//}
		writer.writeOpeningTag("dir", true, true);
		writer.writeAttribute("name", fno.fname);
		writer.writeAttribute("size", fno.fsize);
		writer.writeClosingTag("dir", true, true);
	}
	writer.writeArrayEnding("dirlist", true, true);
	writer.writeByte(0xF7);
	char* bitz = writer.getBufferPtr();
	int32_t bw = writer.bytesWritten();
	device->sendSysex((const uint8_t*) bitz, bw); //
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
		parser.exitTag();
	}
}


void smSysex::sendMemoryBlock(MIDIDevice* device, Deserializer &reader) {
		const int32_t data_size = 768;
		const int32_t max_packed_size = 922;
		uint8_t reply_hdr[8] = {0xF0, 0x00, 0x21, 0x7B, 0x01, 0x02, 0x40, 0x00};
		uint8_t* reply = midiEngine.sysex_fmt_buffer;
		// 		memcpy(reply, reply_hdr, 5);
		memcpy(reply, reply_hdr, 8); //
		                             //		reply[5] = 0; // nominally 32*data[5] is start pos for a delta
		reply[8] = 0;                // nominally 32*data[8] is start pos for a delta //

		int32_t packed;

		//			packed = pack_8bit_to_7bit(reply + 6, max_packed_size,
		// deluge::hid::display::OLED::oledCurrentImage[0],
		uint8_t* srcBlock = (uint8_t*) smDeserializer.fileClusterBuffer; //  deluge::hid::display::OLED::oledCurrentImage[0];

		packed = pack_8bit_to_7bit(reply + 9, max_packed_size, srcBlock,
			                           data_size); //
		if (packed < 0) {
			display->popupTextTemporary("error: fail");
		}
		//		reply[6 + packed] = 0xf7; // end of transmission
		reply[9 + packed] = 0xf7;              // end of transmission
		                                       // 		device->sendSysex(reply, packed + 7); //
		device->sendSysex(reply, packed + 10); //
}
