/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "deluge-project/document.h"
#include "deluge-project/audio_input.h"

#include "deluge-project/arp_codec.h"
#include "deluge-project/controls_codec.h"
#include "deluge-project/mapping_codec.h"
#include "deluge-project/song_codec.h"
#include "deluge-project/structure_codec.h"
#include "foundation/panic.h"
#include "serialization/tree.h"

#include <algorithm>
#include <initializer_list>

namespace deluge::project {

using serialization::children;
using serialization::elements_named;
using serialization::int_of;
using serialization::NamedChild;
using serialization::Value;

namespace {

/// Whether an output @p element runs a host chain of effects: a kit or an audio track.
bool has_chain(std::string_view element) {
	return element == "kit" || element == "audioTrack";
}

/// The naming of the engine whose params the controls of a @p element (`sound`, `kit`, `audioTrack`, `song`) point
/// at, or nullptr when that element has no controls or @p engines has no naming for its engine.
const deluge::engine::ParamNaming* naming_of(std::string_view element, const CodecEngines& engines) {
	if (element == "sound") {
		return deluge::engine::naming_for(engines.namings, deluge::engine::kDelugeInstrument);
	}
	if (element == "kit" || element == "audioTrack" || element == "song") {
		return deluge::engine::naming_for(engines.namings, deluge::engine::kGlobalEffectable);
	}
	return nullptr;
}

/// Whether an output @p element is an instrument, which has a MIDI input: anything but an audio track.
bool has_input(std::string_view element) {
	return element != "audioTrack";
}

bool is_clip_element(std::string_view name) {
	return name == "instrumentClip" || name == "track" || name == "audioClip";
}

bool is_output_element(std::string_view name) {
	return name == "sound" || name == "kit" || name == "audioTrack" || name == "midi" || name == "midiChannel"
	       || name == "mpeZone" || name == "cvChannel";
}

bool is_drum_element(std::string_view name) {
	return name == "sound" || name == "sample" || name == "synth" || name == "midiOutput" || name == "gateOutput";
}

/// The first of @p names that @p song holds.
const Value* first_of(const Value& song, std::initializer_list<std::string_view> names) {
	for (std::string_view name : names) {
		if (const Value* found = song.find(name)) {
			return found;
		}
	}
	return nullptr;
}

/// Gives elements without an id the next id after the largest of their kind, in the order they are asked for.
class IdMinter {
public:
	void see(const Value& element) {
		if (auto id = int_of(element.find("id"))) {
			next_ = std::max(next_, static_cast<uint32_t>(*id) + 1);
		}
	}
	uint32_t id_of(const Value& element) {
		if (auto id = int_of(element.find("id"))) {
			return static_cast<uint32_t>(*id);
		}
		return next_++;
	}

private:
	uint32_t next_{1};
};

std::vector<NamedChild> clip_list(const Value& song, std::initializer_list<std::string_view> names) {
	const Value* list = first_of(song, names);
	return list != nullptr ? children(*list) : std::vector<NamedChild>{};
}

/// Copies every entry of the Map @p from to the end of @p to. In a debug build, a key @p to already holds freezes: two
/// owners would each write it, and a decode keeps both.
void append_entries(Value& to, const Value& from) {
#if !defined(NDEBUG)
	for (const auto& entry : from.entries()) {
		if (to.find(entry.first) != nullptr) {
			FREEZE_WITH_ERROR("E466");
		}
	}
#endif
	for (const auto& [key, value] : from.entries()) {
		to.set(key, value);
	}
}

Value boxed(std::string_view name, Value element) {
	auto box = Value::makeMap();
	box.set(std::string(name), std::move(element));
	return box;
}

} // namespace

const SongDocument::ClipEntry* SongDocument::clip_entry(uint32_t id) const {
	auto it = std::ranges::find(clip_order, id, &ClipEntry::id);
	return it != clip_order.end() ? &*it : nullptr;
}

std::expected<SongDocument, LoadError> decode_song(const Value& root, const CodecEngines& engines) {
	if (!root.isMap()) {
		return std::unexpected(LoadError::NOT_A_MAP);
	}
	const Value* song = root.find("song");
	if (song == nullptr || !song->isMap()) {
		return std::unexpected(LoadError::MISSING_SONG);
	}
	SongDocument document;
	const DecodeContext context = decode_context(*song);
	(void)decode(*song, document.transport);
	(void)decode(*song, document.groove, context);
	(void)decode(*song, document.scale);
	if (const Value* reverb = song->find("reverb")) {
		(void)decode(*reverb, document.reverb, context);
	}
	LegacyMacroTargets legacy;
	(void)decode(*song, document.arrangement, document.mappings, &legacy);
	decode_mappings(*song, ControlsAddress{ControlsAddress::Kind::SONG, 0}, document.mappings,
	                naming_of("song", engines));
	const int32_t magnitude =
	    document.transport.inside_world_tick_magnitude + document.transport.inside_world_tick_magnitude_offset_from_bpm;
	if (engines.chains.decode != nullptr) {
		document.master_effects = engines.chains.decode(*song, ChainHome::MASTER, magnitude);
	}

	const auto sessionClips = clip_list(*song, {"sessionClips", "tracks"});
	const auto arrangementClips = clip_list(*song, {"arrangementOnlyTracks", "arrangementOnlyClips"});
	const Value* instruments = song->find("instruments");
	const auto outputs = instruments != nullptr ? children(*instruments) : std::vector<NamedChild>{};

	IdMinter clipIds;
	IdMinter outputIds;
	IdMinter drumIds;
	for (const auto* list : {&sessionClips, &arrangementClips}) {
		for (const NamedChild& clip : *list) {
			if (is_clip_element(clip.name) && clip.node->isMap()) {
				clipIds.see(*clip.node);
			}
		}
	}
	for (const NamedChild& output : outputs) {
		if (!is_output_element(output.name) || !output.node->isMap()) {
			continue;
		}
		outputIds.see(*output.node);
		if (const Value* sources = output.node->find("soundSources")) {
			for (const NamedChild& drum : children(*sources)) {
				if (is_drum_element(drum.name) && drum.node->isMap()) {
					drumIds.see(*drum.node);
				}
			}
		}
	}

	std::vector<std::pair<uint32_t, int64_t>> recordingFrom; ///< an audio track's id, and its source's index
	for (const NamedChild& output : outputs) {
		if (!is_output_element(output.name) || !output.node->isMap()) {
			continue;
		}
		SongDocument::OutputEntry entry{.id = outputIds.id_of(*output.node), .element = std::string(output.name)};
		DocOutput& record = document.outputs[entry.id] = new_output(output.name);
		(void)decode_identity(*output.node, output.name, record);
		// The source is read only for a track whose channel is a specific track: the file writes an index either way.
		if (auto index = int_of(output.node->find("outputRecordingIndex"));
		    index && output.name == "audioTrack" && record.input_channel == kInputChannelSpecificTrack) {
			recordingFrom.emplace_back(entry.id, *index);
		}
		if (output.name == "sound") {
			if (engines.instruments.decode != nullptr) {
				record.instrument = InstrumentSlot{.engine = std::string(deluge::engine::kDelugeInstrument.name),
				                                   .state = engines.instruments.decode(*output.node, magnitude),
				                                   .tick_magnitude = magnitude};
			}
			if (const Value* arp = output.node->find("arpeggiator")) {
				// A sound's element holds the arp's sync level relative to the song's tick magnitude.
				decode(*arp, record.arp.emplace(), std::optional<int32_t>{magnitude});
			}
		}
		if (const deluge::engine::ParamNaming* naming = naming_of(output.name, engines)) {
			record.controls = decode_controls(*output.node, *naming);
			decode_mappings(*output.node, ControlsAddress{ControlsAddress::Kind::OUTPUT, entry.id}, document.mappings,
			                naming);
		}
		if (has_input(output.name)) {
			decode_mappings(*output.node, TrackOwner{entry.id, output.name == "kit"}, document.mappings);
		}
		if (has_chain(output.name) && engines.chains.decode != nullptr) {
			record.effects = engines.chains.decode(*output.node, ChainHome::CLIP, magnitude);
		}
		if (const Value* sources = output.node->find("soundSources")) {
			for (const NamedChild& drum : children(*sources)) {
				if (!is_drum_element(drum.name) || !drum.node->isMap()) {
					continue;
				}
				const uint32_t id = drumIds.id_of(*drum.node);
				DocDrum& drumRecord = document.drums[id];
				(void)decode_identity(*drum.node, drum.name, drumRecord);
				if (const Value* arp = drum.node->find("arpeggiator")) {
					// A sound's element holds the arp's sync level relative to the song's tick magnitude.
					decode(*arp, drumRecord.arp.emplace(),
					       drumRecord.drum_type == DrumType::SOUND ? std::optional<int32_t>{magnitude} : std::nullopt);
				}
				if (drumRecord.drum_type == DrumType::SOUND) {
					if (engines.instruments.decode != nullptr) {
						drumRecord.instrument =
						    InstrumentSlot{.engine = std::string(deluge::engine::kDelugeInstrument.name),
						                   .state = engines.instruments.decode(*drum.node, magnitude),
						                   .tick_magnitude = magnitude};
					}
					if (const deluge::engine::ParamNaming* naming = naming_of("sound", engines)) {
						drumRecord.controls = decode_controls(*drum.node, *naming);
						decode_mappings(*drum.node, ControlsAddress{ControlsAddress::Kind::DRUM, id}, document.mappings,
						                naming);
					}
				}
				decode_mappings(*drum.node, DrumOwner{id}, document.mappings);
				entry.drums.push_back({.id = id, .element = std::string(drum_element(document.drums[id].drum_type))});
			}
		}
		document.output_order.push_back(std::move(entry));
	}

	for (const auto* list : {&sessionClips, &arrangementClips}) {
		const bool arrangementOnly = list == &arrangementClips;
		for (const NamedChild& clip : *list) {
			if (!is_clip_element(clip.name) || !clip.node->isMap()) {
				continue;
			}
			SongDocument::ClipEntry entry{.id = clipIds.id_of(*clip.node),
			                              .audio = clip.name == "audioClip",
			                              .arrangement_only = arrangementOnly};
			DocClip& record = document.clips[entry.id];
			(void)decode(*clip.node, record);
			decode_mappings(*clip.node, ClipOwner{entry.id}, document.mappings);
			const Value* sync = clip.node->find("isSyncScaleClip");
			if (sync == nullptr) {
				sync = clip.node->find("isSyncScaleTrack");
			}
			if (auto scaling = int_of(sync); scaling && *scaling != 0) {
				document.transport.sync_scaling_clip_id = entry.id;
			}
			if (const Value* rows = clip.node->find("noteRows")) {
				int32_t minY = -32768;
				bool everyRowHasY = true;
				for (const Value* row : elements_named(*rows, "noteRow")) {
					everyRowHasY = everyRowHasY && row->isMap() && row->find("y") != nullptr;
					DocNoteRow& docRow = record.note_rows.emplace_back();
					docRow.y = -32768; // a new row's place, before the file or the rows above it move it
					docRow.sequence_direction = SequenceDirection::OBEY_PARENT;
					(void)decode(*row, docRow, minY);
				}
				entry.in_kit = !record.note_rows.empty() && !everyRowHasY;
			}
			entry.in_kit = entry.in_kit || clip.node->find("kitParams") != nullptr;
			if (arrangementOnly) {
				record.section = 255; // an arranger-only clip launches from no section
			}
			if (entry.audio) {
				(void)decode(*clip.node, document.audio_clips[entry.id]);
			}
			document.clip_order.push_back(entry);
		}
	}

	// An audio track records from another by its place in the file; the record holds the other's id.
	for (const auto& [id, index] : recordingFrom) {
		if (index >= 0 && index < std::ssize(document.output_order)) {
			document.outputs.at(id).recording_from_output_id = document.output_order[index].id;
		}
	}

	std::vector<uint32_t> sessionIds;
	for (const SongDocument::ClipEntry& entry : document.clip_order) {
		if (!entry.arrangement_only) {
			sessionIds.push_back(entry.id);
		}
	}
	for (size_t slot = 0; slot < document.arrangement.session_macros.size(); slot++) {
		SessionMacro& m = document.arrangement.session_macros[slot];
		if (auto index = legacy.clip_index[slot]) {
			if (*index >= 0 && *index < std::ssize(sessionIds)) {
				m.clip_id = sessionIds[*index];
			}
		}
		if (auto index = legacy.output_index[slot]) {
			if (*index >= 0 && *index < std::ssize(document.output_order)) {
				m.output_id = document.output_order[*index].id;
			}
		}
		if ((m.kind == SessionMacroKind::CLIP_LAUNCH && !m.clip_id)
		    || (m.kind == SessionMacroKind::OUTPUT_CYCLE && !m.output_id)) {
			m.kind = SessionMacroKind::NO_MACRO;
		}
	}
	return document;
}

Value encode_song(const SongDocument& song, std::string_view firmware_version, const CodecEngines& engines) {
	auto map = Value::makeMap();
	map.set("firmwareVersion", Value::makeScalar(std::string(firmware_version)));
	const int32_t magnitude =
	    song.transport.inside_world_tick_magnitude + song.transport.inside_world_tick_magnitude_offset_from_bpm;
	append_entries(map, encode(song.transport));
	append_entries(map, encode(song.groove, magnitude));
	append_entries(map, encode(song.scale));
	map.set("reverb", encode(song.reverb, magnitude));
	map.set("sections", encode_sections(song.arrangement, song.mappings));
	encode_mappings(song.mappings, ControlsAddress{ControlsAddress::Kind::SONG, 0}, map, naming_of("song", engines));
	if (engines.chains.encode != nullptr) {
		engines.chains.encode(song.master_effects, map, magnitude);
	}
	if (Value macros = encode_macros(song.arrangement); !macros.items().empty()) {
		map.set("sessionMacros", std::move(macros));
	}

	auto instruments = Value::makeArray();
	for (const SongDocument::OutputEntry& entry : song.output_order) {
		auto element = Value::makeMap();
		element.set("id", Value::makeNumber(entry.id));
		const DocOutput& output = song.outputs.at(entry.id);
		append_entries(element, encode_identity(output, entry.element));
		if (entry.element == "audioTrack") {
			int64_t index = 0; // the engine writes the first output's place when there is no source
			for (size_t i = 0; i < song.output_order.size(); i++) {
				if (output.recording_from_output_id == song.output_order[i].id) {
					index = static_cast<int64_t>(i);
				}
			}
			element.set("outputRecordingIndex", Value::makeNumber(index));
		}
		if (const deluge::engine::ParamNaming* naming = naming_of(entry.element, engines)) {
			encode_controls(output.controls, *naming, element);
			encode_mappings(song.mappings, ControlsAddress{ControlsAddress::Kind::OUTPUT, entry.id}, element, naming);
		}
		if (has_input(entry.element)) {
			encode_mappings(song.mappings, TrackOwner{entry.id, entry.element == "kit"}, element);
		}
		if (output.effects && engines.chains.encode != nullptr) {
			engines.chains.encode(*output.effects, element, magnitude);
		}
		if (output.instrument && engines.instruments.encode != nullptr) {
			engines.instruments.encode(output.instrument->state, element, magnitude);
		}
		if (output.arp) {
			// A sound's element holds the arp's sync level relative to the song's tick magnitude.
			element.set("arpeggiator", encode(*output.arp, magnitude));
		}
		if (!entry.drums.empty()) {
			auto sources = Value::makeArray();
			for (const SongDocument::DrumEntry& drum : entry.drums) {
				auto drumElement = Value::makeMap();
				drumElement.set("id", Value::makeNumber(drum.id));
				const DocDrum& drumRecord = song.drums.at(drum.id);
				append_entries(drumElement, encode_identity(drumRecord));
				if (drumRecord.drum_type == DrumType::SOUND) {
					if (const deluge::engine::ParamNaming* naming = naming_of("sound", engines)) {
						encode_controls(drumRecord.controls, *naming, drumElement);
						encode_mappings(song.mappings, ControlsAddress{ControlsAddress::Kind::DRUM, drum.id},
						                drumElement, naming);
					}
				}
				encode_mappings(song.mappings, DrumOwner{drum.id}, drumElement);
				if (drumRecord.arp && drumRecord.drum_type != DrumType::SOUND) {
					drumElement.set("arpeggiator", encode(*drumRecord.arp, std::nullopt));
				}
				if (drumRecord.instrument && engines.instruments.encode != nullptr) {
					engines.instruments.encode(drumRecord.instrument->state, drumElement, magnitude);
				}
				if (drumRecord.arp && drumRecord.drum_type == DrumType::SOUND) {
					// A sound's element holds the arp's sync level relative to the song's tick magnitude.
					drumElement.set("arpeggiator", encode(*drumRecord.arp, magnitude));
				}
				sources.append(boxed(drum.element, std::move(drumElement)));
			}
			element.set("soundSources", std::move(sources));
		}
		instruments.append(boxed(entry.element, std::move(element)));
	}
	map.set("instruments", std::move(instruments));

	auto sessionClips = Value::makeArray();
	auto arrangementClips = Value::makeArray();
	for (const SongDocument::ClipEntry& entry : song.clip_order) {
		const DocClip& record = song.clips.at(entry.id);
		auto element = Value::makeMap();
		element.set("id", Value::makeNumber(entry.id));
		append_entries(element, encode(record));
		encode_mappings(song.mappings, ClipOwner{entry.id}, element);
		if (song.transport.sync_scaling_clip_id == entry.id) {
			element.set("isSyncScaleClip", Value::makeNumber(1));
		}
		if (entry.audio) {
			append_entries(element, encode(song.audio_clips.at(entry.id)));
		}
		if (!record.note_rows.empty()) {
			auto rows = Value::makeArray();
			for (const DocNoteRow& row : record.note_rows) {
				rows.append(boxed("noteRow", encode(row, entry.in_kit)));
			}
			element.set("noteRows", std::move(rows));
		}
		(entry.arrangement_only ? arrangementClips : sessionClips)
		    .append(boxed(entry.audio ? "audioClip" : "instrumentClip", std::move(element)));
	}
	map.set("sessionClips", std::move(sessionClips));
	map.set("arrangementOnlyTracks", std::move(arrangementClips));

	auto root = Value::makeMap();
	root.set("song", std::move(map));
	return root;
}

} // namespace deluge::project
