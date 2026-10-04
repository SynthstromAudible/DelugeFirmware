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

// The model decodes a song as the engine loader reads it: load a song, save it (the save gives every clip, output and
// drum an id), load the saved bytes with the engine (E) and decode the same bytes with the model alone (D), and
// require D's records to equal the model records E's load left behind.
//
//   model_decode_diff <song.XML>
//
// Exit: 0 = equal; 1 = load error; 2 = usage/IO; 3 = a record differs (printed).

#include "deluge-project/document.h"
#include "memory/general_memory_allocator.h"
#include "model/global_effectable/global_effectable.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "scenarios/drivers/tree_load.h"
#include "serialization/tree.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "scenarios/drivers/driver.h"

namespace {

using deluge::project::SongDocument;
using deluge::scenarios::driver::Io;

Io io;

Song* load_song(char* xml, size_t xmlLen, deluge::storage::DocumentFormat format, Error* loadErr) {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	std::optional<deluge::storage::Value> tree = parseDocument(std::string_view(xml, xmlLen), format);
	Error e = Error::FILE_CORRUPTED;
	if (tree) {
		deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(format));
		activeReader = &adapter;
		e = openTreeFile(adapter, "song", format);
		if (e == Error::NONE) {
			e = deluge::bridge::loadSong(*song, adapter);
		}
		activeReader = &smDeserializer;
	}
	*loadErr = e;
	if (e != Error::NONE) {
		return nullptr;
	}
	currentSong = song;
	song->setCurrentClip(nullptr);
	return song;
}

int differs(const char* what) {
	std::fprintf(io.err, "DECODE DIFF: %s\n", what);
	return 3;
}

/// What differs between two records, for the report: the clip fields and note rows field by field; other records whole.
template <class Record>
std::string what_differs(const Record&, const Record&) {
	return "";
}

std::string what_differs(const deluge::project::DocClip& e, const deluge::project::DocClip& d) {
	std::string out;
	const auto field = [&](const char* name, long engine, long decoded) {
		if (engine != decoded) {
			out +=
			    std::string(" ") + name + " engine=" + std::to_string(engine) + " decoded=" + std::to_string(decoded);
		}
	};
	field("loop_length", e.loop_length, d.loop_length);
	field("colour_offset", e.colour_offset, d.colour_offset);
	field("launch_style", static_cast<long>(e.launch_style), static_cast<long>(d.launch_style));
	field("section", e.section, d.section);
	field("note_rows", static_cast<long>(e.note_rows.size()), static_cast<long>(d.note_rows.size()));
	auto er = e.note_rows.begin();
	auto dr = d.note_rows.begin();
	for (int i = 0; er != e.note_rows.end() && dr != d.note_rows.end(); ++er, ++dr, ++i) {
		if (!(*er == *dr)) {
			const std::string row = "row" + std::to_string(i) + ".";
			field((row + "y").c_str(), er->y, dr->y);
			field((row + "length").c_str(), er->loop_length_if_independent, dr->loop_length_if_independent);
			field((row + "colour").c_str(), er->colour_offset, dr->colour_offset);
			field((row + "direction").c_str(), static_cast<long>(er->sequence_direction),
			      static_cast<long>(dr->sequence_direction));
			field((row + "muted").c_str(), er->muted, dr->muted);
			break;
		}
	}
	return out;
}

std::string what_differs(const deluge::project::DocOutput& e, const deluge::project::DocOutput& d) {
	std::string out;
	const auto num = [&](const char* name, long engine, long decoded) {
		if (engine != decoded) {
			out +=
			    std::string(" ") + name + " engine=" + std::to_string(engine) + " decoded=" + std::to_string(decoded);
		}
	};
	const auto text = [&](const char* name, const std::string& engine, const std::string& decoded) {
		if (engine != decoded) {
			out += std::string(" ") + name + " engine='" + engine + "' decoded='" + decoded + "'";
		}
	};
	text("name", e.name, d.name);
	num("colour", e.colour, d.colour);
	num("mod_knob_mode", e.mod_knob_mode, d.mod_knob_mode);
	num("muted", e.muted_in_arrangement, d.muted_in_arrangement);
	num("soloing", e.soloing_in_arrangement, d.soloing_in_arrangement);
	num("armed", e.armed_for_recording, d.armed_for_recording);
	text("dir_path", e.dir_path, d.dir_path);
	num("default_velocity", e.default_velocity, d.default_velocity);
	num("channel", e.channel, d.channel);
	num("channel_suffix", e.channel_suffix, d.channel_suffix);
	text("device_definition", e.device_definition_file_name, d.device_definition_file_name);
	num("collapse_aftertouch", e.collapse_aftertouch, d.collapse_aftertouch);
	num("collapse_mpe", e.collapse_mpe, d.collapse_mpe);
	num("output_mpe_y", e.output_mpe_y, d.output_mpe_y);
	num("cv2_source", static_cast<long>(e.cv2_source), static_cast<long>(d.cv2_source));
	return out;
}

template <class Store, class Map>
int compare_store(const char* what, const Store& engine, const Map& decoded) {
	for (const auto& [id, record] : engine) {
		auto it = decoded.find(id);
		if (it == decoded.end()) {
			std::fprintf(io.err, "DECODE DIFF: %s %u is not in the decoded song\n", what, id);
			return 3;
		}
		// The slots are compared on their own, after the identity records (compare_slots).
		auto withoutSlot = [](auto r) {
			if constexpr (requires { r.instrument.reset(); }) {
				r.instrument.reset();
			}
			return r;
		};
		if (!(withoutSlot(it->second) == withoutSlot(record))) {
			std::fprintf(io.err, "DECODE DIFF: %s %u differs:%s\n", what, id, what_differs(record, it->second).c_str());
			return 3;
		}
	}
	if (decoded.size() != engine.size()) {
		std::fprintf(io.err, "DECODE DIFF: the decoded song has %zu %ss, the engine %zu\n", decoded.size(), what,
		             engine.size());
		return 3;
	}
	return 0;
}

/// 0 when every synth track's and sound kit row's slot in @p project equals the one decoded from the file, else 3 after
/// printing which and where.
int compare_slots(const char* label, const deluge::project::Song& project, const SongDocument& d) {
	auto check = [&](const char* what, uint32_t id, const auto* record, const auto& decoded) -> int {
		const bool same = record != nullptr && record->instrument == decoded.instrument;
		if (same) {
			return 0;
		}
		std::string where = "a slot is missing";
		if (record != nullptr && record->instrument && decoded.instrument) {
			where =
			    "at " + deluge::serialization::first_difference(decoded.instrument->state, record->instrument->state);
		}
		std::fprintf(io.err, "DECODE DIFF: %s slot of %s %u differs (%s)\n", label, what, id, where.c_str());
		return 3;
	};
	for (const auto& [id, decoded] : d.outputs) {
		auto it = project.output_structure.by_id.find(id);
		if (int r = check("output", id, it == project.output_structure.by_id.end() ? nullptr : &it->second, decoded)) {
			return r;
		}
	}
	for (const auto& [id, decoded] : d.drums) {
		auto it = project.drum_structure.by_id.find(id);
		if (int r = check("drum", id, it == project.drum_structure.by_id.end() ? nullptr : &it->second, decoded)) {
			return r;
		}
	}
	return 0;
}

int model_decode_diff_main(int argc, char** argv, Io driverIo) {
	io = driverIo;
	if (argc < 2) {
		std::fprintf(io.err, "usage: %s <song.XML>\n", argv[0]);
		return 2;
	}
	FILE* f = std::fopen(argv[1], "rb");
	if (!f) {
		std::fprintf(io.err, "cannot open %s\n", argv[1]);
		return 2;
	}
	std::fseek(f, 0, SEEK_END);
	long fileLen = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	const uint32_t kCap = 16u * 1024 * 1024;
	char* orig = static_cast<char*>(std::malloc(fileLen + 1));
	char* gen1 = static_cast<char*>(std::malloc(kCap));
	char* gen1copy = static_cast<char*>(std::malloc(kCap));
	fileLen = static_cast<long>(std::fread(orig, 1, fileLen, f));
	orig[fileLen] = 0;
	std::fclose(f);

	Error e;
	Song* song = load_song(orig, fileLen, deluge::storage::formatOfPath(argv[1]), &e);
	if (!song) {
		std::fprintf(io.err, "LOADFAIL -> Error %d\n", static_cast<int>(e));
		return 1;
	}
	const long n1 = deluge::scenarios::driver::saveSongCapture(*song, gen1, kCap);

	std::optional<deluge::serialization::Value> tree =
	    parseDocument(std::string_view(gen1, static_cast<size_t>(n1)), deluge::storage::kSongSaveFormat);
	std::memcpy(gen1copy, gen1, n1);
	Song* engine = load_song(gen1copy, n1, deluge::storage::kSongSaveFormat, &e);
	if (!engine || !tree) {
		std::fprintf(io.err, "LOADFAIL gen1 -> Error %d\n", static_cast<int>(e));
		return 3;
	}
	auto decoded = deluge::project::decode_song(*tree);
	if (!decoded) {
		return differs("the saved song does not decode");
	}
	const SongDocument& d = *decoded;
	const deluge::project::Song& m = engine->project;

	if (!(d.transport == m.transport)) {
		return differs("transport");
	}
	if (!(d.groove == m.groove)) {
		return differs("groove");
	}
	if (!(d.scale == m.scale)) {
		return differs("scale");
	}
	if (!(d.reverb == m.reverb)) {
		return differs("reverb");
	}
	if (!(d.arrangement.sections == m.arrangement.sections)) {
		return differs("sections");
	}
	if (!(d.arrangement.session_macros == m.arrangement.session_macros)) {
		return differs("session macros");
	}
	// A melodic clip's note-row colour is not saved (the engine picks one when it makes the row), so it is compared
	// only in kits, whose rows save theirs.
	std::map<uint32_t, deluge::project::DocClip> clips = d.clips;
	for (auto& [id, clip] : clips) {
		const SongDocument::ClipEntry* entry = d.clip_entry(id);
		auto engineIt = m.clip_structure.by_id.find(id);
		const deluge::project::DocClip* engineClip =
		    engineIt != m.clip_structure.by_id.end() ? &engineIt->second : nullptr;
		if (entry == nullptr || entry->in_kit || engineClip == nullptr) {
			continue;
		}
		auto engineRow = engineClip->note_rows.begin();
		for (auto& row : clip.note_rows) {
			if (engineRow == engineClip->note_rows.end()) {
				break;
			}
			row.colour_offset = engineRow->colour_offset;
			++engineRow;
		}
	}
	if (int r = compare_store("clip", m.clip_structure.by_id, clips)) {
		return r;
	}
	if (int r = compare_store("audio clip", m.audio_clip_structure.by_id, d.audio_clips)) {
		return r;
	}
	if (int r = compare_store("output", m.output_structure.by_id, d.outputs)) {
		return r;
	}
	if (int r = compare_store("drum", m.drum_structure.by_id, d.drums)) {
		return r;
	}
	// The slots: the song that wrote the file holds what it wrote (refreshed at save); the song read from it holds what
	// it read (filled at load); both are the file's elements without the model's keys.
	for (const auto& [label, project] : {std::pair{"saved", &song->project}, std::pair{"loaded", &engine->project}}) {
		if (int r = compare_slots(label, *project, d)) {
			return r;
		}
	}
	return 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(model_decode_diff, model_decode_diff_main)
