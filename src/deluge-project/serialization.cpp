/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "deluge-project/serialization.h"
#include "deluge-project/arrangement.h"
#include "deluge-project/groove.h"
#include "io/midi/midi_device.h"         // MIDICable::writeReferenceToFile
#include "io/midi/midi_device_manager.h" // MIDIDeviceManager::readDeviceReferenceFromFile
#include "model/song/song.h"
#include "storage/storage_manager.h" // Serializer / Deserializer
#include <algorithm>
#include <cstring>

namespace deluge::project {

void saveSong(::Song& song, Serializer& out) {
	song.writeToFile(out);
}

Error loadSong(::Song& song, Deserializer& in) {
	return song.readFromFile(in);
}

// --- Groove ---

void serialize(const Groove& groove, Serializer& out, ::Song* song) {
	out.writeAttribute("swingAmount", groove.swing_amount);
	out.writeAbsoluteSyncLevelToFile(song, "swingInterval", (SyncLevel)groove.swing_interval, true);
}

bool deserialize(Groove& groove, Deserializer& in, char const* tagName) {
	if (!strcmp(tagName, "swingAmount")) {
		groove.swing_amount = in.readTagOrAttributeValueInt();
		groove.swing_amount = std::min(groove.swing_amount, (int8_t)49);
		groove.swing_amount = std::max(groove.swing_amount, (int8_t)-49);
		in.exitTag("swingAmount");
		return true;
	}
	if (!strcmp(tagName, "swingInterval")) {
		// swingInterval, unlike other "sync" type params, we read as just its plain int32_t value, and only shift it by
		// insideWorldTickMagnitude after reading the whole song (the two attributes can be stored in either order, so
		// we won't know both until the end; also pre-V3.1.0-alpha stored all "sync" values as plain ints).
		groove.swing_interval = in.readTagOrAttributeValueInt();
		groove.swing_interval = std::min(groove.swing_interval, (uint8_t)9);
		in.exitTag("swingInterval");
		return true;
	}
	return false;
}

// --- Arrangement: sections ---

void serializeSections(const Arrangement& arrangement, Serializer& writer) {
	writer.writeArrayStart("sections");
	for (int32_t s = 0; s < kMaxNumSections; s++) {
		if (true || arrangement.sections[s].launch_midi_command.containsSomething()
		    || arrangement.sections[s].num_repetitions) {
			writer.writeOpeningTagBeginning("section", true);
			writer.writeAttribute("id", s, false);
			if (true || arrangement.sections[s].num_repetitions) {
				writer.writeAttribute("numRepeats", arrangement.sections[s].num_repetitions, false);
			}
			if (arrangement.sections[s].launch_midi_command.containsSomething()) {
				// Annoyingly, I used one-off tag names here, rather than it conforming to what the LearnedMIDI
				// class now uses.
				writer.writeAttribute("midiCommandChannel", arrangement.sections[s].launch_midi_command.channelOrZone,
				                      false);
				writer.writeAttribute("midiCommandNote", arrangement.sections[s].launch_midi_command.noteOrCC, false);
				if (arrangement.sections[s].launch_midi_command.cable) {
					writer.writeOpeningTagEnd();
					arrangement.sections[s].launch_midi_command.cable->writeReferenceToFile(writer,
					                                                                        "midiCommandDevice");
					writer.writeClosingTag("section", true, true);
					continue; // We now don't need to close the tag.
				}
			}
			writer.closeTag(true);
		}
	}
	writer.writeArrayEnding("sections");
}

bool deserializeSections(Arrangement& arrangement, Deserializer& reader, char const* tagName) {
	if (strcmp(tagName, "sections") != 0) {
		return false;
	}
	// Read in all the sections
	reader.match('[');
	while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "section")) {

			uint8_t id = 255;
			MIDICable* cable = nullptr;
			uint8_t channel = 255;
			uint8_t note = 255;
			int16_t numRepeats = 0;
			reader.match('{');
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "id")) {
					id = reader.readTagOrAttributeValueInt();
				}

				else if (!strcmp(tagName, "numRepeats")) {
					numRepeats = reader.readTagOrAttributeValueInt();
					if (numRepeats < -2 || numRepeats > 9999) {
						numRepeats = 0;
					}
				}

				// Annoyingly, I used one-off tag names here, rather than it conforming to what the
				// LearnedMIDI class now uses.
				else if (!strcmp(tagName, "midiCommandDevice")) {
					cable = MIDIDeviceManager::readDeviceReferenceFromFile(reader);
				}

				else if (!strcmp(tagName, "midiCommandChannel")) {
					channel = reader.readTagOrAttributeValueInt();
				}

				else if (!strcmp(tagName, "midiCommandNote")) {
					note = reader.readTagOrAttributeValueInt();
				}

				reader.exitTag(tagName); // exit undefined tag.
			}

			if (id < kMaxNumSections) {
				if (channel < 16 && note < 128) {
					arrangement.sections[id].launch_midi_command.cable = cable;
					arrangement.sections[id].launch_midi_command.channelOrZone = channel;
					arrangement.sections[id].launch_midi_command.noteOrCC = note;
				}
				arrangement.sections[id].num_repetitions = numRepeats;
			}
			reader.match('}');               // leave values object
			reader.exitTag("section", true); // leave box.
		}
		else {
			reader.exitTag(tagName);
		}
	}
	reader.exitTag("sections");
	reader.match(']');
	return true;
}

} // namespace deluge::project
