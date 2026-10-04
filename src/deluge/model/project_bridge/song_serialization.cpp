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

#include "model/project_bridge/song_serialization.h"
#include "deluge-project/arrangement.h"
#include "deluge-project/arrangement_codec.h"
#include "deluge-project/codec.h"
#include "deluge-project/groove.h"
#include "deluge-project/scale_settings.h"
#include "deluge-project/song_codec.h"
#include "deluge-project/transport.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/midi_device.h" // MIDICable::writeReferenceToFile
#include "io/midi/midi_device_manager.h"
#include "model/project_bridge/midi_command.h"
#include "model/song/song.h"
#include "storage/storage_manager.h" // Serializer / Deserializer
#include "storage/value_tree_deserializer.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace deluge::bridge {

using namespace deluge::project;

void saveSong(::Song& song, Serializer& out) {
	song.writeToFile(out);
}

namespace {

/// The song's tags the model decodes; Song::readFromFile skips them.
constexpr std::array<std::string_view, 10> kDecodedSongTags{
    "timePerTimerTick", "timerTickFraction", "inputTickMagnitude", "swingAmount",
    "swingInterval",    "rootNote",          "modeNotes",          "scales",
    "sections",         "sessionMacros",
};

/// Points the session macros at the clips and outputs the song holds, as reading them always has: an id the song does
/// not hold names nothing, a position names the session clip or output at that position, and a macro left naming
/// nothing it needs does nothing.
void resolveMacros(Arrangement& arrangement, const LegacyMacroTargets& legacy, ::Song& song) {
	for (size_t y = 0; y < arrangement.session_macros.size(); y++) {
		SessionMacro& m = arrangement.session_macros[y];
		if (m.clip_id && song.clipForId(*m.clip_id) == nullptr) {
			m.clip_id.reset();
		}
		if (m.output_id && song.outputForId(*m.output_id) == nullptr) {
			m.output_id.reset();
		}
		if (auto index = legacy.clip_index[y]) {
			if (*index >= 0 && *index < std::ssize(song.live.clips.session_clips)) {
				m.clip_id = song.live.clips.session_clips[*index]->id();
			}
		}
		if (auto index = legacy.output_index[y]) {
			Output* output = song.live.media.first_output;
			for (int32_t i = *index; output != nullptr && i > 0; i--) {
				output = output->next;
			}
			if (output != nullptr && *index >= 0) {
				m.output_id = song.outputIdFor(*output);
			}
		}
		if ((m.kind == SessionMacroKind::CLIP_LAUNCH && !m.clip_id)
		    || (m.kind == SessionMacroKind::OUTPUT_CYCLE && !m.output_id)) {
			m.kind = SessionMacroKind::NO_MACRO;
		}
	}
}

/// Makes the device list know every hosted device a section's launch command names, and records the reference as the
/// list then holds it, as reading a device reference always has.
void registerSectionDevices(Arrangement& arrangement) {
	for (Section& section : arrangement.sections) {
		std::optional<MidiDeviceRef>& device = section.launch_midi_command.device;
		if (!device || device->port != MidiDeviceRef::Port::HOSTED) {
			continue;
		}
		std::string name = device->name;
		if (MIDICable* cable = MIDIDeviceManager::getOrCreateHostedMIDIDeviceFromDetails(&name, device->vendor_id,
		                                                                                 device->product_id)) {
			device = reference_of(*cable);
		}
		else {
			device.reset();
		}
	}
}

} // namespace

bool isDecodedSongTag(char const* tagName) {
	return std::ranges::find(kDecodedSongTags, std::string_view{tagName}) != kDecodedSongTags.end();
}

Error loadSong(::Song& song, Deserializer& in) {
	const deluge::storage::ValueTreeDeserializer& tree = deluge::storage::treeReaderOf(in);
	const deluge::serialization::Value* songNode = tree.currentValue();
	if (songNode == nullptr || !songNode->isMap()) {
		return Error::FILE_CORRUPTED;
	}
	const DecodeContext context = decode_context(tree.root(), *songNode);

	// The tempo starts from the default tick length when the file gives none.
	Transport transport = song.project.transport;
	transport.samples_per_timer_tick_big = uint64_t{1} << 32;
	(void)decode(*songNode, transport);
	song.project.transport.inside_world_tick_magnitude = transport.inside_world_tick_magnitude;
	(void)decode(*songNode, song.project.groove, context);
	(void)decode(*songNode, song.project.scale);
	if (const deluge::serialization::Value* reverb = songNode->find("reverb")) {
		(void)decode(*reverb, song.project.reverb, context);
	}
	for (Section& section : song.project.arrangement.sections) {
		section.num_repetitions = 0;
	}
	for (SessionMacro& macro : song.project.arrangement.session_macros) {
		macro.kind = SessionMacroKind::NO_MACRO;
	}
	LegacyMacroTargets legacy;
	(void)decode(*songNode, song.project.arrangement, &legacy);
	registerSectionDevices(song.project.arrangement);

	const Error error = song.readFromFile(in, transport.samples_per_timer_tick_big);
	if (error != Error::NONE) {
		return error;
	}
	resolveMacros(song.project.arrangement, legacy, song);
	return Error::NONE;
}

// --- Groove ---

void serialize(const Groove& groove, Serializer& out, ::Song* song) {
	out.writeAttribute("swingAmount", groove.swing_amount);
	out.writeAbsoluteSyncLevelToFile(song, "swingInterval", groove.swing_interval, true);
}

// --- Arrangement: sections ---

void serializeSections(const Arrangement& arrangement, Serializer& writer) {
	writer.writeArrayStart("sections");
	for (int32_t s = 0; s < kMaxNumSections; s++) {
		if (true || arrangement.sections[s].launch_midi_command.contains_something()
		    || arrangement.sections[s].num_repetitions) {
			writer.writeOpeningTagBeginning("section", true);
			writer.writeAttribute("id", s, false);
			if (true || arrangement.sections[s].num_repetitions) {
				writer.writeAttribute("numRepeats", arrangement.sections[s].num_repetitions, false);
			}
			if (arrangement.sections[s].launch_midi_command.contains_something()) {
				// Annoyingly, I used one-off tag names here, rather than it conforming to what the LearnedMIDI
				// class now uses.
				const MidiCommand& command = arrangement.sections[s].launch_midi_command;
				writer.writeAttribute("midiCommandChannel", command.channel_or_zone, false);
				writer.writeAttribute("midiCommandNote", command.note_or_cc, false);
				if (command.device) {
					writer.writeOpeningTagEnd();
					write_reference(writer, "midiCommandDevice", *command.device);
					writer.writeClosingTag("section", true, true);
					continue; // We now don't need to close the tag.
				}
			}
			writer.closeTag(true);
		}
	}
	writer.writeArrayEnding("sections");
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
			if (m.clip_id) {
				writer.writeAttribute("kind", "clip_launch");
				writer.writeAttribute("clipId", *m.clip_id);
			}
			break;
		case SessionMacroKind::OUTPUT_CYCLE:
			if (m.output_id) {
				writer.writeAttribute("kind", "output_cycle");
				writer.writeAttribute("outputId", *m.output_id);
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

// --- Transport ---

void serialize(const Transport& transport, Serializer& out) {
	out.writeAttribute("timePerTimerTick", transport.samples_per_timer_tick_big >> 32);
	out.writeAttribute("timerTickFraction", (uint32_t)transport.samples_per_timer_tick_big);
	// The derived input-tick magnitude (Song::getInputTickMagnitude): the saved magnitude plus the
	// sync-scaling offset that is kept out of the displayed BPM.
	out.writeAttribute("inputTickMagnitude",
	                   transport.inside_world_tick_magnitude + transport.inside_world_tick_magnitude_offset_from_bpm);
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

} // namespace deluge::bridge
