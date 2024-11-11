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

    JsonSerializer

********************************************************************************/

JsonSerializer::JsonSerializer() {
	reset();
}

void JsonSerializer::reset() {
	resetWriter();
	firstItemHasBeenWritten = false;
}

void JsonSerializer::write(char const* output) {

	writeChars(output);
}

void JsonSerializer::writeTag(char const* tag, int32_t number, bool box) {
	char* buffer = shortStringBuffer;
	intToString(number, buffer);
	writeTag(tag, buffer, box, false);
}

void JsonSerializer::writeTag(char const* tag, char const* contents, bool box, bool quote) {
	insertCommaIfNeeded();
	write("\n");
	printIndents();
	if (box)
		write("{");
	write("\"");
	write(tag);
	write("\": ");
	if (quote)
		write("\"");
	write(contents);
	if (quote)
		write("\"");
	if (box)
		write("}");
	firstItemHasBeenWritten = true;
}

// Unlike other attributes, numbers in Json should not be quoted.
// So we don't.
void JsonSerializer::writeAttribute(char const* name, int32_t number, bool onNewLine) {

	char buffer[12];
	intToString(number, buffer);

	insertCommaIfNeeded();
	if (onNewLine) {
		write("\n");
		printIndents();
	}
	else {
		write(" ");
	}
	write("\"");
	write(name);
	write("\": ");
	write(buffer);

	firstItemHasBeenWritten = true;
}

// numChars may be up to 8
void JsonSerializer::writeAttributeHex(char const* name, int32_t number, int32_t numChars, bool onNewLine) {

	char buffer[11];
	buffer[0] = '0';
	buffer[1] = 'x';
	intToHex(number, &buffer[2], numChars);
	writeAttribute(name, buffer, onNewLine);
}

// numChars may be up to 8
void JsonSerializer::writeAttributeHexBytes(char const* name, uint8_t* data, int32_t numBytes, bool onNewLine) {

	insertCommaIfNeeded();

	if (onNewLine) {
		write("\n");
		printIndents();
	}
	else {
		write(" ");
	}
	write(name);
	write(":\"");

	char buffer[3];
	for (int i = 0; i < numBytes; i++) {
		intToHex(data[i], &buffer[0], 2);
		write(buffer);
	}
	write("\"");
	firstItemHasBeenWritten = true;
}

void JsonSerializer::writeAttribute(char const* name, char const* value, bool onNewLine) {

	insertCommaIfNeeded();
	if (onNewLine) {
		write("\n");
		printIndents();
	}
	else {
		write(" ");
	}
	write("\"");
	write(name);
	write("\": \"");
	write(value);
	write("\"");
	firstItemHasBeenWritten = true;
}

void JsonSerializer::writeTagNameAndSeperator(char const* tag) {
	write("\"");
	write(tag);
	write("\":");
}

void JsonSerializer::writeOpeningTag(char const* tag, bool startNewLineAfter, bool box) {
	writeOpeningTagBeginning(tag, box, false);
	writeOpeningTagEnd(startNewLineAfter);
}

// If passed NULL for the tag, then don't write one. Just start a new object.
void JsonSerializer::writeOpeningTagBeginning(char const* tag, bool box, bool newLineBefore) {
	insertCommaIfNeeded();
	if (newLineBefore) // prepend newLine almost always.
		write("\n");
	printIndents();
	if (box || !tag) {
		write("{");
	}
	if (tag) {
		write("\"");
		write(tag);
		write("\": {");
	}
	indentAmount++;
	firstItemHasBeenWritten = false;
}

void JsonSerializer::closeTag(bool box) {
	// printIndents();
	write("}");
	if (box)
		write("}");
	indentAmount--;
	firstItemHasBeenWritten = true;
}

void JsonSerializer::writeOpeningTagEnd(bool startNewLineAfter) {
}

void JsonSerializer::writeClosingTag(char const* tag, bool shouldPrintIndents, bool box) {
	indentAmount--;
	firstItemHasBeenWritten = true;
	write("}");
	if (box)
		write("}");
}

void JsonSerializer::printIndents() {
	if (memoryBased)
		return;
	for (int32_t i = 0; i < indentAmount; i++) {
		write("\t");
	}
}

Error JsonSerializer::closeFileAfterWriting(char const* path, char const* beginningString, char const* endString) {
	return closeAfterWriting(path, beginningString, NULL);
}

void JsonSerializer::writeArrayStart(char const* tag, bool startNewLineAfter, bool box) {

	insertCommaIfNeeded();
	write("\n");
	printIndents();
	if (box)
		write("{");
	write("\"");
	write(tag);
	write("\": [");
	indentAmount++;
	firstItemHasBeenWritten = false;
}

void JsonSerializer::writeArrayEnding(char const* tag, bool shouldPrintIndents, bool box) {
	indentAmount--;
	firstItemHasBeenWritten = true;
	if (shouldPrintIndents) {
		write("\n");
		printIndents();
	}
	write("]");
	if (box)
		write("}");
}

void JsonSerializer::insertCommaIfNeeded() {
	if (firstItemHasBeenWritten) {
		write(",");
	}
	else {
		firstItemHasBeenWritten = true;
	}
}
