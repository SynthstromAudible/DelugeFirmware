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

#include "definitions_cxx.hpp"
#include "drivers/pic/pic.h"
#include "fatfs/fatfs.hpp"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/storage_manager.h"
#include "util/firmware_version.h"
#include "util/functions.h"
#include "util/try.h"
#include "version.h"
#include <string.h>

extern "C" {
#include "RZA1/oled/oled_low_level.h"
#include "fatfs/diskio.h"
#include "fatfs/ff.h"
}

/*******************************************************************************

    XMLSerializer

********************************************************************************/

void Serializer::writeAbsoluteSyncLevelToFile(Song* song, char const* name, SyncLevel internalValue, bool onNewLine) {
	writeAttribute(name, song->convertSyncLevelFromInternalValueToFileValue(internalValue), onNewLine);
}

void Serializer::writeFirmwareVersion() {
	writeAttribute("firmwareVersion", kFirmwareVersionStringShort);
}

XMLSerializer::XMLSerializer() {
	reset();
}

void XMLSerializer::reset() {
	resetWriter();
}

// TODO: this is really inefficient
void XMLSerializer::write(char const* output) {

	while (*output) {

		if (fileWriteBufferCurrentPos == audioFileManager.clusterSize) {

			if (!fileAccessFailedDuringWrite) {
				Error error = writeBufferToFile();
				if (error != Error::NONE) {
					fileAccessFailedDuringWrite = true;
					return;
				}
			}

			fileWriteBufferCurrentPos = 0;
		}

		writeClusterBuffer[fileWriteBufferCurrentPos] = *output;

		output++;
		fileWriteBufferCurrentPos++;

		// Ensure we do some of the audio routine once in a while
		if (!(fileWriteBufferCurrentPos & 0b11111111)) {
			AudioEngine::logAction("writeCharXML");

			// AudioEngine::routineWithClusterLoading();

			uiTimerManager.routine();

			if (display->haveOLED()) {
				oledRoutine();
			}
			PIC::flush();
		}
	}
}

void XMLSerializer::writeTag(char const* tag, int32_t number) {
	char* buffer = shortStringBuffer;
	intToString(number, buffer);
	writeTag(tag, buffer);
}

void XMLSerializer::writeTag(char const* tag, char const* contents) {

	printIndents();
	write("<");
	write(tag);
	write(">");
	write(contents);
	write("</");
	write(tag);
	write(">\n");
}

void XMLSerializer::writeAttribute(char const* name, int32_t number, bool onNewLine) {

	char buffer[12];
	intToString(number, buffer);

	writeAttribute(name, buffer, onNewLine);
}

// numChars may be up to 8
void XMLSerializer::writeAttributeHex(char const* name, int32_t number, int32_t numChars, bool onNewLine) {

	char buffer[11];
	buffer[0] = '0';
	buffer[1] = 'x';
	intToHex(number, &buffer[2], numChars);

	writeAttribute(name, buffer, onNewLine);
}

// numChars may be up to 8
void XMLSerializer::writeAttributeHexBytes(char const* name, uint8_t* data, int32_t numBytes, bool onNewLine) {

	if (onNewLine) {
		write("\n");
		printIndents();
	}
	else {
		write(" ");
	}
	write(name);
	write("=\"");

	char buffer[3];
	for (int i = 0; i < numBytes; i++) {
		intToHex(data[i], &buffer[0], 2);
		write(buffer);
	}
	write("\"");
}

void XMLSerializer::writeAttribute(char const* name, char const* value, bool onNewLine) {

	if (onNewLine) {
		write("\n");
		printIndents();
	}
	else {
		write(" ");
	}

	write(name);
	write("=\"");
	write(value);
	write("\"");
}

void XMLSerializer::writeOpeningTag(char const* tag, bool startNewLineAfter) {
	writeOpeningTagBeginning(tag);
	writeOpeningTagEnd(startNewLineAfter);
}

void XMLSerializer::writeOpeningTagBeginning(char const* tag) {
	printIndents();
	write("<");
	write(tag);
	indentAmount++;
}

void XMLSerializer::closeTag() {
	write(" /");
	writeOpeningTagEnd();
	indentAmount--;
}

void XMLSerializer::writeOpeningTagEnd(bool startNewLineAfter) {
	if (startNewLineAfter) {
		write(">\n");
	}
	else {
		write(">");
	}
}

void XMLSerializer::writeClosingTag(char const* tag, bool shouldPrintIndents) {
	indentAmount--;
	if (shouldPrintIndents) {
		printIndents();
	}
	write("</");
	write(tag);
	write(">\n");
}

void XMLSerializer::printIndents() {
	for (int32_t i = 0; i < indentAmount; i++) {
		write("\t");
	}
}

Error XMLSerializer::closeFileAfterWriting(char const* path, char const* beginningString, char const* endString) {
	return closeAfterWriting(path, beginningString, endString);
}
