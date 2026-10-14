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
#include "serialization/value.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/storage_manager.h"
#include "util/firmware_version.h"
#include "util/functions.h"
#include "util/try.h"
#include "version.h"
#include <string.h>

/*******************************************************************************

    XMLSerializer

********************************************************************************/

void Serializer::writeAbsoluteSyncLevelToFile(Song* song, char const* name, SyncLevel internalValue, bool onNewLine) {
	writeAttribute(name, song->convertSyncLevelFromInternalValueToFileValue(deluge::engine::index_of(internalValue)),
	               onNewLine);
}

void Serializer::writeValue(const deluge::serialization::Value& /*members*/) {
	FREEZE_WITH_ERROR("SV01");
}

void XMLSerializer::writeValue(const deluge::serialization::Value& members) {
	bool sawChild = false;
	for (const auto& [name, value] : members.entries()) {
		if (value.isScalar()) {
			if (sawChild) {
				FREEZE_WITH_ERROR("SV02");
				return;
			}
			writeAttribute(name.c_str(), value.str().c_str());
			continue;
		}
		sawChild = true;
		writeElement(name.c_str(), value);
	}
}

void XMLSerializer::writeElement(char const* name, const deluge::serialization::Value& value) {
	if (value.isArray()) {
		writeArrayStart(name);
		for (const auto& item : value.items()) {
			for (const auto& [itemName, itemValue] : item.entries()) {
				writeElement(itemName.c_str(), itemValue);
			}
		}
		writeArrayEnding(name);
		return;
	}
	writeOpeningTagBeginning(name);
	bool hasChildren = false;
	for (const auto& [key, member] : value.entries()) {
		if (member.isScalar()) {
			writeAttribute(key.c_str(), member.str().c_str());
		}
		else {
			hasChildren = true;
		}
	}
	if (!hasChildren) {
		closeTag();
		return;
	}
	writeOpeningTagEnd();
	for (const auto& [key, member] : value.entries()) {
		if (!member.isScalar()) {
			writeElement(key.c_str(), member);
		}
	}
	writeClosingTag(name);
}

void Serializer::writeFirmwareVersion() {
	writeAttribute("firmwareVersion", kFirmwareVersionStringShort);
}

XMLSerializer::XMLSerializer() {
	reset();
}

void XMLSerializer::reset() {
	resetWriter();
	// FileWriter's counter is separate from this serializer's private counter.
	indentAmount = 0;
}

void XMLSerializer::write(char const* output) {
	writeChars(output);
}

void XMLSerializer::writeTag(char const* tag, int32_t number, bool box) {
	char buffer[12];
	intToString(number, buffer);
	writeTag(tag, buffer, box);
}

void XMLSerializer::writeTag(char const* tag, char const* contents, bool box, bool quote) {

	printIndents();
	write("<");
	write(tag);
	write(">");
	writeEscaped(contents);
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
	writeEscaped(value);
	write("\"");
}

void XMLSerializer::writeEscaped(char const* text) {
	// Only what would end the value early is escaped: a quote, and a '<'. Everything else, a bare '&' included, goes
	// out as it is, which every reader of these files (this firmware's and older) reads back unchanged.
	char chunk[64];
	size_t used = 0;
	const auto flush = [&] {
		chunk[used] = 0;
		write(chunk);
		used = 0;
	};
	for (char const* c = text; *c != 0; c++) {
		char const* escape = *c == '"' ? "&quot;" : *c == '<' ? "&lt;" : nullptr;
		if (escape == nullptr) {
			chunk[used++] = *c;
			if (used == sizeof chunk - 1) {
				flush();
			}
			continue;
		}
		if (used > 0) {
			flush();
		}
		write(escape);
	}
	if (used > 0) {
		flush();
	}
}

void XMLSerializer::writeTagNameAndSeperator(char const* tag) {
	write(tag);
	write("=");
}

void XMLSerializer::writeOpeningTag(char const* tag, bool startNewLineAfter, bool box) {
	writeOpeningTagBeginning(tag);
	writeOpeningTagEnd(startNewLineAfter);
}

void XMLSerializer::writeOpeningTagBeginning(char const* tag, bool box, bool newLineBefore) {
	printIndents();
	write("<");
	write(tag);
	indentAmount++;
}

void XMLSerializer::closeTag(bool box) {
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

void XMLSerializer::writeClosingTag(char const* tag, bool shouldPrintIndents, bool box) {
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

void XMLSerializer::writeArrayStart(char const* tag, bool startNewLineAfter, bool box) {
	writeOpeningTag(tag, startNewLineAfter);
}

void XMLSerializer::writeArrayEnding(char const* tag, bool shouldPrintIndents, bool box) {
	writeClosingTag(tag, shouldPrintIndents);
}

Error XMLSerializer::closeFileAfterWriting(char const* path, char const* beginningString, char const* endString) {
	return closeAfterWriting(path, beginningString, endString);
}