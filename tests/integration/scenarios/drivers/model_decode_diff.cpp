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

#include "deluge-project/arp_codec.h"
#include "deluge-project/document.h"
#include "memory/general_memory_allocator.h"
#include "model/global_effectable/global_effectable.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "scenarios/drivers/tree_load.h"
#include "serialization/tree.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "model/project_bridge/controls_bridge.h"
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

/// Where two arp records differ, for the report: the first attribute of their elements, or which is missing.
std::string arp_differs(const std::optional<deluge::project::ArpRecord>& e,
                        const std::optional<deluge::project::ArpRecord>& d) {
	if (e == d) {
		return "";
	}
	if (!e || !d) {
		return std::string(" arp engine=") + (e ? "a record" : "none") + " decoded=" + (d ? "a record" : "none");
	}
	return " arp at "
	       + deluge::serialization::first_difference(deluge::project::encode(*d, std::nullopt),
	                                                 deluge::project::encode(*e, std::nullopt));
}

std::string what_differs(const deluge::project::DocDrum& e, const deluge::project::DocDrum& d) {
	return arp_differs(e.arp, d.arp);
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
	if (e.name != d.name) {
		out += " name engine='" + e.name + "' decoded='" + d.name + "'";
	}
	field("sequence_direction", static_cast<long>(e.sequence_direction), static_cast<long>(d.sequence_direction));
	if (!(e.values == d.values)) {
		out += " values" + arp_differs(e.values.arp, d.values.arp);
	}
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

/// Whether @p a and @p b hold the same chain: its order, its view, and its effects as they encode (a decode starts
/// from the default effects, a song's owner from its own seed, which no file holds).
bool sameChain(const deluge::project::FxChain& a, const deluge::project::FxChain& b) {
	const deluge::engine::CodecContext context{.input_tick_magnitude = 1};
	return a.slots == b.slots && a.reverb_return == b.reverb_return && a.mod_fx_param == b.mod_fx_param
	       && a.filter_type == b.filter_type && a.effects.encode(context) == b.effects.encode(context);
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
	num("hide_unlabeled_cc", e.hide_unlabeled_cc, d.hide_unlabeled_cc);
	num("cc_labels", static_cast<long>(e.cc_labels.size()), static_cast<long>(d.cc_labels.size()));
	num("input_channel", e.input_channel, d.input_channel);
	num("audio_mode", e.audio_mode, d.audio_mode);
	num("recording_from", e.recording_from_output_id ? long(*e.recording_from_output_id) : -1,
	    d.recording_from_output_id ? long(*d.recording_from_output_id) : -1);
	if (!(e.controls == d.controls)) {
		out += " controls (engine " + std::to_string(e.controls.size()) + ", decoded "
		       + std::to_string(d.controls.size()) + ")";
	}
	if (e.effects.has_value() != d.effects.has_value() || (e.effects && !sameChain(*e.effects, *d.effects))) {
		out += std::string(" effects (engine ") + (e.effects ? "a chain" : "none") + ", decoded "
		       + (d.effects ? "a chain" : "none") + ")";
	}
	return out;
}

/// One binding, for the report: what it controls and the command that does.
std::string describe(const deluge::project::Mapping& mapping) {
	using namespace deluge::project;
	std::string what = std::visit(
	    [](const auto& target) -> std::string {
		    using T = std::decay_t<decltype(target)>;
		    if constexpr (std::is_same_v<T, ParamTarget>) {
			    static constexpr const char* kKinds[] = {"output", "drum", "song"};
			    return std::string("knob of ") + kKinds[static_cast<int>(target.owner.kind)] + " "
			           + std::to_string(target.owner.id) + " on param " + std::to_string(target.param.value);
		    }
		    else if constexpr (std::is_same_v<T, ClipMuteTarget>) {
			    return "mute of clip " + std::to_string(target.clip_id);
		    }
		    else if constexpr (std::is_same_v<T, DrumMuteTarget>) {
			    return "mute of drum " + std::to_string(target.drum_id);
		    }
		    else if constexpr (std::is_same_v<T, SectionLaunchTarget>) {
			    return "launch of section " + std::to_string(target.section);
		    }
		    else if constexpr (std::is_same_v<T, TrackInputTarget>) {
			    return "input of output " + std::to_string(target.output_id);
		    }
		    else {
			    return "input of drum " + std::to_string(target.drum_id);
		    }
	    },
	    mapping.target);
	return what + " from channel " + std::to_string(mapping.source.channel_or_zone) + " note/CC "
	       + std::to_string(mapping.source.note_or_cc) + (mapping.source.device ? " on a device" : "")
	       + (mapping.relative ? " relative" : "");
}

/// 0 when @p a and @p b hold the same bindings in the same order, else 3 after printing @p what and the first that
/// differs.
int compare_mappings(const char* what, const deluge::project::MappingTable& a, const char* aName,
                     const deluge::project::MappingTable& b, const char* bName) {
	if (a == b) {
		return 0;
	}
	const auto as = a.entries();
	const auto bs = b.entries();
	size_t at = 0;
	while (at < as.size() && at < bs.size() && as[at] == bs[at]) {
		at++;
	}
	std::fprintf(io.err, "DECODE DIFF: %s (%s %zu, %s %zu) at entry %zu: %s %s, %s %s\n", what, aName, as.size(), bName,
	             bs.size(), at, aName, at < as.size() ? describe(as[at]).c_str() : "none", bName,
	             at < bs.size() ? describe(bs[at]).c_str() : "none");
	return 3;
}

/// 0 when the knobs and bindings the app holds after loading a file (the song's table, and each output's and drum's
/// mod knobs the file names by id) are those the file holds, else 3 after printing which differ.
int compare_loaded_controls(const deluge::project::Song& loaded, const SongDocument& file) {
	if (int r = compare_mappings("the song's mappings after loading are not the file's", loaded.mappings, "loaded",
	                             file.mappings, "file")) {
		return r;
	}
	for (const auto& [id, output] : file.outputs) {
		auto it = loaded.output_structure.by_id.find(id);
		if (it != loaded.output_structure.by_id.end() && !(it->second.controls == output.controls)) {
			std::fprintf(io.err, "DECODE DIFF: output %u's knobs after loading are not the file's\n", id);
			return 3;
		}
	}
	for (const auto& [id, drum] : file.drums) {
		auto it = loaded.drum_structure.by_id.find(id);
		if (it != loaded.drum_structure.by_id.end() && !(it->second.controls == drum.controls)) {
			std::fprintf(io.err, "DECODE DIFF: drum %u's knobs after loading are not the file's\n", id);
			return 3;
		}
	}
	return 0;
}

template <class Store, class Map>
int compare_store(const char* what, const Store& engine, const Map& decoded) {
	for (const auto& [id, record] : engine) {
		auto it = decoded.find(id);
		if (it == decoded.end()) {
			std::fprintf(io.err, "DECODE DIFF: %s %u is not in the decoded song\n", what, id);
			return 3;
		}
		// The slots are compared on their own, after the identity records (compare_slots); a chain by sameChain.
		auto withoutSlot = [](auto r) {
			if constexpr (requires { r.instrument.reset(); }) {
				r.instrument.reset();
			}
			if constexpr (requires { r.effects.reset(); }) {
				r.effects.reset();
			}
			return r;
		};
		bool chainsDiffer = false;
		if constexpr (requires { record.effects.has_value(); }) {
			chainsDiffer = record.effects.has_value() != it->second.effects.has_value()
			               || (record.effects && !sameChain(*record.effects, *it->second.effects));
		}
		if (chainsDiffer || !(withoutSlot(it->second) == withoutSlot(record))) {
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
	// Compared by what each writes: the file's decode starts from a new sound, so a field no file holds (an FM sound's
	// oscillator types) may differ from the loaded sound's without the slots saying anything different.
	const auto encoded = [](const deluge::project::InstrumentSlot& slot) {
		return slot.state.encode({.input_tick_magnitude = slot.tick_magnitude});
	};
	auto check = [&](const char* what, uint32_t id, const auto* record, const auto& decoded) -> int {
		const bool present = record != nullptr && record->instrument.has_value() == decoded.instrument.has_value();
		const bool same = present
		                  && (!decoded.instrument
		                      || (record->instrument->engine == decoded.instrument->engine
		                          && record->instrument->tick_magnitude == decoded.instrument->tick_magnitude
		                          && encoded(*record->instrument) == encoded(*decoded.instrument)));
		if (same) {
			return 0;
		}
		std::string where = "a slot is missing";
		if (record != nullptr && record->instrument && decoded.instrument) {
			where =
			    "at "
			    + deluge::serialization::first_difference(encoded(*decoded.instrument), encoded(*record->instrument));
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

/// Every synth track's arp @p file holds is the one @p project's record holds: what loading kept, or what a save and
/// reload kept.
int compare_synth_arps(const char* when, const deluge::project::Song& project, const SongDocument& file) {
	for (const auto& [id, output] : file.outputs) {
		if (!output.arp) {
			continue;
		}
		const deluge::project::DocOutput* record = project.output_structure.find(id);
		if (record == nullptr || !record->arp || !(*record->arp == *output.arp)) {
			std::fprintf(io.err, "FAIL %s: output %u's arpeggiator is not the file's\n", when,
			             static_cast<unsigned>(id));
			return 4;
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

	// The file as written, decoded before the app reads it: the knobs the app holds after loading it must be these.
	std::optional<deluge::serialization::Value> originalTree =
	    parseDocument(std::string_view(orig, static_cast<size_t>(fileLen)), deluge::storage::formatOfPath(argv[1]));
	std::optional<SongDocument> original;
	if (originalTree) {
		if (auto decodedOriginal = deluge::project::decode_song(*originalTree, deluge::bridge::codecEngines())) {
			original = std::move(*decodedOriginal);
		}
	}

	Error e;
	Song* song = load_song(orig, fileLen, deluge::storage::formatOfPath(argv[1]), &e);
	if (!song) {
		std::fprintf(io.err, "LOADFAIL -> Error %d\n", static_cast<int>(e));
		return 1;
	}
	if (original) {
		if (int r = compare_loaded_controls(song->project, *original)) {
			return r;
		}
		if (int r = compare_synth_arps("loaded", song->project, *original)) {
			return r;
		}
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
	auto decoded = deluge::project::decode_song(*tree, deluge::bridge::codecEngines());
	if (!decoded) {
		return differs("the saved song does not decode");
	}
	const SongDocument& d = *decoded;
	const deluge::project::Song& m = engine->project;
	if (original) {
		if (int r = compare_synth_arps("saved and reloaded", m, *original)) {
			return r;
		}
	}

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
	if (int r = compare_mappings("the song's mappings differ", m.mappings, "engine", d.mappings, "decoded")) {
		return r;
	}
	if (!sameChain(m.master_effects, d.master_effects)) {
		std::fprintf(io.err, "DECODE DIFF: the master's effects differ (engine %zu slots, decoded %zu)\n",
		             m.master_effects.slots.size(), d.master_effects.slots.size());
		return 3;
	}
	// The slots: each sound plays its slot's state, so the song that wrote the file holds what it wrote and the song
	// read from it holds what it read; both encode to the file's elements without the model's keys.
	for (const auto& [label, project] : {std::pair{"saved", &song->project}, std::pair{"loaded", &engine->project}}) {
		if (int r = compare_slots(label, *project, d)) {
			return r;
		}
	}
	return 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(model_decode_diff, model_decode_diff_main)
