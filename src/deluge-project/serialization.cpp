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
#include "deluge-project/scale_settings.h"
#include "deluge-project/transport.h"
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

// --- Arrangement: session macros ---

void serializeMacros(const Arrangement& arrangement, Serializer& writer) {
	int32_t maxSessionMacroToSave = 0;
	for (int32_t y = 0; y < std::ssize(arrangement.session_macros); y++) {
		if (arrangement.session_macros[y].kind != SessionMacroKind::NO_MACRO) {
			maxSessionMacroToSave = y + 1;
		}
	}
	if (maxSessionMacroToSave == 0) {
		return;
	}
	writer.writeArrayStart("sessionMacros");
	for (int32_t y = 0; y < maxSessionMacroToSave; y++) {
		const auto& m = arrangement.session_macros[y];
		writer.writeOpeningTagBeginning("macro", true);
		switch (m.kind) {
		case SessionMacroKind::CLIP_LAUNCH:
			// Reference the target clip by its stable serial id (resolve via Song::clipForId on load),
			// so the macro survives clip reordering. A clip is always saved before the macros, so its
			// id is assigned by now.
			if (m.clip != nullptr && m.clip->id().has_value()) {
				writer.writeAttribute("kind", "clip_launch");
				writer.writeAttribute("clipId", *m.clip->id());
			}
			break;
		case SessionMacroKind::OUTPUT_CYCLE:
			if (m.output != nullptr && m.output->id().has_value()) {
				writer.writeAttribute("kind", "output_cycle");
				writer.writeAttribute("outputId", *m.output->id());
			}
			break;
		case SessionMacroKind::SECTION:
			writer.writeAttribute("kind", "section");
			writer.writeAttribute("section", m.section);
			break;
		case SessionMacroKind::NO_MACRO:
		case SessionMacroKind::NUM_KINDS:
			break;
		}
		writer.closeTag(true);
	}
	writer.writeArrayEnding("sessionMacros");
}

bool deserializeMacros(Arrangement& arrangement, Deserializer& reader, ::Song& song, char const* tagName) {
	if (strcmp(tagName, "sessionMacros") != 0) {
		return false;
	}
	int32_t slotIndex = 0;
	reader.match('[');
	while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "macro")) {
			int32_t y = slotIndex++;
			if (y >= std::ssize(arrangement.session_macros)) {
				reader.exitTag("macro", true);
				continue;
			}
			auto& m = arrangement.session_macros[y];
			reader.match('{'); // enter value object
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "kind")) {
					const char* kind = reader.readTagOrAttributeValue();
					if (!strcmp(kind, "clip_launch")) {
						m.kind = SessionMacroKind::CLIP_LAUNCH;
					}
					else if (!strcmp(kind, "output_cycle")) {
						m.kind = SessionMacroKind::OUTPUT_CYCLE;
					}
					else if (!strcmp(kind, "section")) {
						m.kind = SessionMacroKind::SECTION;
					}
				}
				else if (!strcmp(tagName, "clipId")) {
					m.clip = song.clipForId(reader.readTagOrAttributeValueInt());
				}
				else if (!strcmp(tagName, "clip")) {
					// Legacy pre-id format: a positional index into the session clips.
					int32_t index = reader.readTagOrAttributeValueInt();
					if (index >= 0 && index < std::ssize(song.project.clips.session_clips)) {
						m.clip = song.project.clips.session_clips[index];
					}
				}
				else if (!strcmp(tagName, "outputId")) {
					m.output = song.outputForId(reader.readTagOrAttributeValueInt());
				}
				else if (!strcmp(tagName, "output")) {
					// Legacy pre-id format: a positional index into the output list.
					int32_t index = reader.readTagOrAttributeValueInt();
					Output* thisOutput = song.project.media.first_output;
					for (; thisOutput != nullptr; thisOutput = thisOutput->next) {
						if (index == 0) {
							break;
						}
						index--;
					}
					m.output = thisOutput;
				}
				else if (!strcmp(tagName, "section")) {
					m.section = reader.readTagOrAttributeValueInt();
				}
			}

			if ((m.kind == SessionMacroKind::CLIP_LAUNCH && m.clip == nullptr)
			    || (m.kind == SessionMacroKind::OUTPUT_CYCLE && m.output == nullptr)) {
				m.kind = SessionMacroKind::NO_MACRO;
			}
		}
		else {
			reader.exitTag();
		}
		reader.match('}'); // end value object
		reader.match('}'); // end box
	}
	reader.match(']'); // end array
	reader.exitTag("sessionMacros");
	return true;
}

// --- Transport ---

void serialize(const Transport& transport, Serializer& out) {
	out.writeAttribute("timePerTimerTick", transport.samples_per_timer_tick_big >> 32);
	out.writeAttribute("timerTickFraction", (uint32_t)transport.samples_per_timer_tick_big);
	// The derived input-tick magnitude (Song::getInputTickMagnitude): the saved magnitude plus the
	// sync-scaling offset that is kept out of the displayed BPM.
	out.writeAttribute("inputTickMagnitude",
	                   transport.inside_world_tick_magnitude + transport.inside_world_tick_magnitude_offset_from_bpm);
}

bool deserialize(Transport& transport, Deserializer& in, uint64_t& timePerTimerTick, char const* tagName) {
	// The tempo is two 32-bit halves accumulated into `timePerTimerTick`, applied post-read by the
	// caller via Song::setTimePerTimerTick (see serialization.h).
	if (!strcmp(tagName, "timePerTimerTick")) {
		timePerTimerTick =
		    (timePerTimerTick & (uint64_t)0xFFFFFFFF) | ((uint64_t)in.readTagOrAttributeValueInt() << 32);
		in.exitTag("timePerTimerTick");
		return true;
	}
	if (!strcmp(tagName, "timerTickFraction")) {
		timePerTimerTick =
		    (timePerTimerTick & ((uint64_t)0xFFFFFFFF << 32)) | (uint64_t)(uint32_t)in.readTagOrAttributeValueInt();
		in.exitTag("timerTickFraction");
		return true;
	}
	if (!strcmp(tagName, "inputTickMagnitude")) {
		transport.inside_world_tick_magnitude = in.readTagOrAttributeValueInt();
		in.exitTag("inputTickMagnitude");
		return true;
	}
	return false;
}

// --- Scale / key ---

void serializeScaleKey(const ScaleSettings& scale, Serializer& out) {
	out.writeAttribute("rootNote", scale.key.rootNote);
}

void serializeScaleNotes(const ScaleSettings& scale, Serializer& out) {
	out.writeArrayStart("modeNotes");
	for (int32_t i = 0; i < scale.key.modeNotes.count(); i++) {
		out.writeTag("modeNote", scale.key.modeNotes[i], true);
	}
	out.writeArrayEnding("modeNotes");

	out.writeOpeningTag("scales");
	out.writeTag("userScale", scale.user_scale_notes.toBits());
	out.writeTag("disabledPresetScales", scale.disabled_preset_scales.to_ulong());
	out.writeClosingTag("scales");
}

bool deserializeScale(ScaleSettings& scale, Deserializer& in, char const* tagName) {
	if (!strcmp(tagName, "rootNote")) {
		scale.key.rootNote = in.readTagOrAttributeValueInt();
		in.exitTag("rootNote");
		return true;
	}
	if (!strcmp(tagName, "modeNotes")) {
		scale.key.modeNotes.clear();
		in.match('[');
		while (in.match('{') && *(tagName = in.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "modeNote")) {
				scale.key.modeNotes.addUntrusted(in.readTagOrAttributeValueInt());
				in.exitTag("modeNote", true);
			}
			else {
				in.exitTag(tagName);
			}
		}
		in.exitTag("modeNotes");
		in.match(']');
		return true;
	}
	if (!strcmp(tagName, "scales")) {
		in.match('{');
		while (*(tagName = in.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "userScale")) {
				scale.user_scale_notes = NoteSet(in.readTagOrAttributeValueInt());
			}
			else if (!strcmp(tagName, "disabledPresetScales")) {
				scale.disabled_preset_scales = std::bitset<NUM_PRESET_SCALES>(in.readTagOrAttributeValueInt());
			}
			in.exitTag(tagName);
		}
		in.exitTag("scales", true);
		return true;
	}
	return false;
}

} // namespace deluge::project
