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

// engine_settings <song|preset> <SONG|SYNTH|KIT> — a sound's settings through its slot. Every output's and sound
// drum's sound in a loaded song or preset is bound to its slot; so is a synth added to a running song and one that
// replaces another. Every legal value of every setting path an Edit maps is set on every bound sound through
// SetEngineSetting: after each, the slot holds the value, the slot's instrument entries are the encoding of the
// sound's state (equation 1), and the sound's snapshot is the one its state projects (equation 2); then the old value
// is restored. A kit's affect-entire leg sets one path on all its sound drums at once. Exit: 0 every check holds; 1 the
// file does not load; 2 usage/IO; 3 a check fails (printed).

#include "deluge-project/instrument_slot.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/settings.h"
#include "memory/general_memory_allocator.h"
#include "model/drum/drum.h"
#include "model/edits/edit.h"
#include "model/edits/set_engine_setting.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/tree_load.h"
#include "serialization/tree.h"
#include "storage/document_format.h"
#include "storage/storage_manager.h"
#include "storage/tree_document.h"
#include "storage/value_tree_deserializer.h"

#include <array>
#include <charconv>
#include <cstdio>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using deluge::serialization::Value;

using deluge::scenarios::driver::Io;

/// Where this driver's prints go.
Io io{};

/// A song for a file to be loaded into.
Song* freshSong() {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	return song;
}

/// The song in @p bytes (of @p format), loaded as the app loads a song; nullptr on a load failure.
Song* loadSong(std::string_view bytes, deluge::storage::DocumentFormat format) {
	Song* song = freshSong();
	std::optional<deluge::storage::Value> tree = parseDocument(bytes, format);
	if (!tree) {
		return nullptr;
	}
	deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(format));
	activeReader = &adapter;
	Error e = openTreeFile(adapter, "song", format);
	if (e == Error::NONE) {
		e = deluge::bridge::loadSong(*song, adapter);
	}
	activeReader = &smDeserializer;
	if (e != Error::NONE) {
		return nullptr;
	}
	currentSong = song;
	song->setCurrentClip(nullptr);
	return song;
}

/// The instrument the preset in @p bytes holds, read into @p song as the app reads a preset (not yet in its outputs).
Instrument* loadPreset(std::string_view bytes, OutputType type, Song& song) {
	char const* firstTag = nullptr;
	char const* altTag = nullptr;
	StorageManager::instrumentDocumentTags(type, &firstTag, &altTag);
	deluge::storage::TreeDocument document;
	if (document.open(std::span{reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}, /*isJson=*/false,
	                  firstTag, altTag)
	    != Error::NONE) {
		return nullptr;
	}
	Instrument* instrument = nullptr;
	std::string name = "PRESET";
	std::string dir = type == OutputType::SYNTH ? "SYNTHS" : "KITS";
	const Error e =
	    StorageManager::parseInstrumentDocument(document, &song, nullptr, type, false, &instrument, &name, &dir);
	document.release();
	return e == Error::NONE ? instrument : nullptr;
}

/// Every sound of @p output: a synth's own, or a kit's sound drums.
void forEachSound(Output& output, const std::function<void(Sound&, const char*)>& visit) {
	if (output.type == OutputType::SYNTH) {
		visit(*static_cast<SoundInstrument*>(&output), "synth");
	}
	else if (output.type == OutputType::KIT) {
		for (Drum* drum = static_cast<Kit*>(&output)->firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type == DrumType::SOUND) {
				visit(*static_cast<SoundDrum*>(drum), "drum");
			}
		}
	}
}

/// 0 when every sound of @p output is bound to its slot, else 3 after printing the first that is not.
int expectBound(Output& output, const char* how) {
	int code = 0;
	if (output.docOutput_ == nullptr) {
		std::fprintf(io.err, "UNBOUND %s: output %s has no record\n", how, output.name.c_str());
		return 3;
	}
	forEachSound(output, [&](Sound& sound, const char* kind) {
		const bool slotted = kind == std::string_view{"synth"}
		                         ? output.docOutput_->instrument.has_value()
		                         : (static_cast<SoundDrum&>(sound).docDrum_ != nullptr
		                            && static_cast<SoundDrum&>(sound).docDrum_->instrument.has_value());
		if (code == 0 && (sound.boundState() == nullptr || !slotted)) {
			std::fprintf(io.err, "UNBOUND %s: a %s of %s has no %s\n", how, kind, output.name.c_str(),
			             sound.boundState() == nullptr ? "bound state" : "slot");
			code = 3;
		}
	});
	return code;
}

/// The setting paths an Edit maps so far.
constexpr std::array<std::string_view, 7> kEditedPaths{
    "polyphonic", "voicePriority", "maxVoices", "mode", "transpose", "cents", "sideChainSend",
};

/// The node at @p path (slash-separated) in @p tree, or nullptr.
const Value* leafAt(const Value& tree, std::string_view path) {
	const Value* at = &tree;
	while (at != nullptr && !path.empty()) {
		const size_t slash = path.find('/');
		at = at->isMap() ? at->find(path.substr(0, slash)) : nullptr;
		path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
	}
	return at;
}

/// The first instrument entry where @p slot and @p encoded differ, by key: one holds a key the other lacks, or the two
/// values differ. Empty when they agree. Order is not compared: a slot keeps a file's order, and appends what the file
/// lacked.
std::string stateDifference(const Value& slot, const Value& encoded) {
	for (const auto& [key, value] : encoded.entries()) {
		const Value* held = slot.find(key);
		if (held == nullptr) {
			return key + " (missing)";
		}
		if (std::string d = deluge::serialization::first_difference(*held, value, key); !d.empty()) {
			return d;
		}
	}
	for (const auto& [key, value] : slot.entries()) {
		if (deluge::instrument::is_state_key(key) && encoded.find(key) == nullptr) {
			return key + " (not encoded)";
		}
	}
	return {};
}

/// The legal values of @p row worth setting, given the slot's @p current text: an integer row's ends and the value
/// after the current one, a choice row's every name, a bool's both.
std::vector<Value> legalValues(const deluge::engine::SettingInfo& row, const std::string* current) {
	using Kind = deluge::engine::SettingKind;
	std::vector<Value> values;
	switch (row.kind) {
	case Kind::INTEGER: {
		values.push_back(Value::makeNumber(row.min));
		values.push_back(Value::makeNumber(row.max));
		int64_t now = 0;
		if (current != nullptr
		    && std::from_chars(current->data(), current->data() + current->size(), now).ec == std::errc{}
		    && now + 1 >= row.min && now + 1 <= row.max) {
			values.push_back(Value::makeNumber(now + 1));
		}
		break;
	}
	case Kind::CHOICE:
		for (std::string_view name : row.choices) {
			values.push_back(Value::makeScalar(std::string(name)));
		}
		break;
	case Kind::BOOL:
		values.push_back(Value::makeNumber(0));
		values.push_back(Value::makeNumber(1));
		break;
	case Kind::STRUCTURED:
		break;
	}
	return values;
}

/// Empty when @p sound's slot agrees with its state after setting @p path to @p value, else which check fails.
std::string_view coherence(Sound& sound, Song& song, std::string_view path, const Value& value) {
	const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(sound);
	deluge::project::InstrumentSlot* slot = address ? song.project.resolveSlot(*address) : nullptr;
	if (slot == nullptr) {
		return "no slot";
	}
	const Value encoded =
	    deluge::instrument::encode(*sound.boundState(), {.input_tick_magnitude = song.getInputTickMagnitude()});
	const std::string* held = deluge::serialization::text_of(leafAt(slot->state, path));
	if (leafAt(encoded, path) != nullptr && (held == nullptr || *held != value.str())) {
		return "value";
	}
	if (std::string d = stateDifference(slot->state, encoded); !d.empty()) {
		std::fprintf(io.err, "equation 1 at %s\n", d.c_str());
		return "equation 1";
	}
	const deluge::snapshot::SoundSnapshot before = sound.snapshot();
	sound.projectSnapshot();
	if (!(sound.snapshot() == before)) {
		return "equation 2";
	}
	return {};
}

/// 0 when every legal value of every edited path, set on @p sound through SetEngineSetting and then undone, leaves its
/// slot coherent; else 3 after printing the first that does not.
int settingLegs(Sound& sound, Song& song, const char* name) {
	const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(sound);
	if (!address) {
		return 0;
	}
	deluge::edits::Context ctx{song, song.project};
	for (std::string_view path : kEditedPaths) {
		const deluge::engine::SettingInfo* row = deluge::engine::find_setting(deluge::instrument::kSettings, path);
		const Value* now = leafAt(song.project.resolveSlot(*address)->state, path);
		const std::optional<Value> old = now != nullptr ? std::optional<Value>{*now} : std::nullopt;
		for (const Value& value : legalValues(*row, deluge::serialization::text_of(now))) {
			deluge::edits::SetEngineSetting edit{*address, std::string(path), value};
			std::string_view failed =
			    deluge::edits::apply(ctx, edit) != Error::NONE ? "refused" : coherence(sound, song, path, value);
			if (failed.empty() && old && deluge::instrument::check_setting(path, *old)) {
				deluge::edits::SetEngineSetting restore{*address, std::string(path), *old};
				failed = deluge::edits::apply(ctx, restore) != Error::NONE ? "restore refused"
				                                                           : coherence(sound, song, path, *old);
			}
			if (!failed.empty()) {
				std::fprintf(io.err, "%s %.*s=%s: %.*s\n", name, static_cast<int>(path.size()), path.data(),
				             value.str().c_str(), static_cast<int>(failed.size()), failed.data());
				return 3;
			}
		}
	}
	return 0;
}

/// The mode pair: @p sound set to subtractive, FM, then subtractive again, its slot coherent after each, holding the
/// modulators only while FM; then its old mode back. 0 when each holds, else 3 after printing the first that does not.
int modePairLeg(Sound& sound, Song& song, const char* name) {
	const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(sound);
	if (!address) {
		return 0;
	}
	deluge::edits::Context ctx{song, song.project};
	const Value old = *leafAt(song.project.resolveSlot(*address)->state, "mode");
	for (std::string_view mode : {"subtractive", "fm", "subtractive"}) {
		const Value value = Value::makeScalar(std::string(mode));
		deluge::edits::SetEngineSetting edit{*address, "mode", value};
		std::string_view failed =
		    deluge::edits::apply(ctx, edit) != Error::NONE ? "refused" : coherence(sound, song, "mode", value);
		const bool modulators = song.project.resolveSlot(*address)->state.find("modulator1") != nullptr;
		if (failed.empty() && modulators != (mode == "fm")) {
			failed = modulators ? "modulators kept" : "no modulators";
		}
		if (!failed.empty()) {
			std::fprintf(io.err, "%s mode pair %.*s: %.*s\n", name, static_cast<int>(mode.size()), mode.data(),
			             static_cast<int>(failed.size()), failed.data());
			return 3;
		}
	}
	deluge::edits::SetEngineSetting restore{*address, "mode", old};
	return deluge::edits::apply(ctx, restore) == Error::NONE ? 0 : 3;
}

/// The affect-entire leg: @p path set to @p value on every sound drum of @p kit, one Edit each, then every drum's slot
/// checked. 0 when each holds, else 3 after printing the first that does not.
int affectEntireLeg(Kit& kit, Song& song, std::string_view path, const Value& value) {
	deluge::edits::Context ctx{song, song.project};
	for (Drum* drum = kit.firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->type != DrumType::SOUND) {
			continue;
		}
		if (const auto address = deluge::bridge::slotAddressOf(*static_cast<SoundDrum*>(drum))) {
			deluge::edits::SetEngineSetting edit{*address, std::string(path), value};
			if (deluge::edits::apply(ctx, edit) != Error::NONE) {
				std::fprintf(io.err, "kit %s affect-entire %.*s: refused\n", kit.name.c_str(),
				             static_cast<int>(path.size()), path.data());
				return 3;
			}
		}
	}
	for (Drum* drum = kit.firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->type != DrumType::SOUND || !deluge::bridge::slotAddressOf(*static_cast<SoundDrum*>(drum))) {
			continue;
		}
		const std::string_view failed = coherence(*static_cast<SoundDrum*>(drum), song, path, value);
		if (!failed.empty()) {
			std::fprintf(io.err, "kit %s affect-entire %.*s=%s: %.*s\n", kit.name.c_str(),
			             static_cast<int>(path.size()), path.data(), value.str().c_str(),
			             static_cast<int>(failed.size()), failed.data());
			return 3;
		}
	}
	return 0;
}

/// Every setting leg on every sound of @p output, and the affect-entire leg on a kit.
int settingsOf(Output& output, Song& song) {
	int code = 0;
	forEachSound(output, [&](Sound& sound, const char* kind) {
		if (code == 0) {
			std::string name = std::string(kind) + " of " + output.name.c_str();
			code = settingLegs(sound, song, name.c_str());
			if (code == 0) {
				code = modePairLeg(sound, song, name.c_str());
			}
		}
	});
	if (code == 0 && output.type == OutputType::KIT) {
		code = affectEntireLeg(static_cast<Kit&>(output), song, "polyphonic", Value::makeScalar("mono"));
	}
	return code;
}

/// The hibernation leg, for a song with no hibernating sound: a synth added to it and hibernated (as replacing an
/// edited instrument hibernates it) keeps its slot, and its settings' Edits reach it there. 0 when each holds, else 3
/// after printing the first that does not.
int hibernationLeg(Song& song) {
	for (Instrument* sleeping = song.live.media.first_hibernating_instrument; sleeping != nullptr;
	     sleeping = static_cast<Instrument*>(sleeping->next)) {
		if (sleeping->type == OutputType::SYNTH || sleeping->type == OutputType::KIT) {
			return 0; // the song's own hibernating sound had every leg
		}
	}
	ParamManagerForTimeline paramManager;
	Instrument* synth = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	if (synth == nullptr) {
		std::fprintf(io.err, "hibernation: no new synth\n");
		return 3;
	}
	// Its params go to the song's backups, as an instrument with no clip keeps them; a setting's side effects reach
	// them.
	song.live.param_backup.backUp(static_cast<ModControllableAudio*>(synth->toModControllable()), nullptr,
	                              &paramManager, true);
	song.addOutput(synth);
	synth->editedByUser = true;
	song.deleteOrHibernateOutput(synth);
	if (song.live.media.first_hibernating_instrument != synth) {
		std::fprintf(io.err, "hibernation: the synth did not hibernate\n");
		return 3;
	}
	if (int r = expectBound(*synth, "hibernated")) {
		return r;
	}
	return settingsOf(*synth, song);
}

int engine_settings_main(int argc, char** argv, Io driverIo) {
	io = driverIo;
	if (argc < 3) {
		std::fprintf(io.err, "usage: %s <file> <SONG|SYNTH|KIT>\n", argv[0]);
		return 2;
	}
	FILE* f = std::fopen(argv[1], "rb");
	if (f == nullptr) {
		std::fprintf(io.err, "cannot open %s\n", argv[1]);
		return 2;
	}
	std::string bytes;
	char chunk[4096];
	for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) {
		bytes.append(chunk, n);
	}
	std::fclose(f);
	const std::string_view kind = argv[2];

	if (kind == "SONG") {
		Song* song = loadSong(bytes, deluge::storage::formatOfPath(argv[1]));
		if (song == nullptr) {
			std::fprintf(io.err, "LOADFAIL\n");
			return 1;
		}
		for (Output* output = song->live.media.first_output; output != nullptr; output = output->next) {
			if (int r = expectBound(*output, "loaded song")) {
				return r;
			}
			if (int r = settingsOf(*output, *song)) {
				return r;
			}
		}
		for (Instrument* sleeping = song->live.media.first_hibernating_instrument; sleeping != nullptr;
		     sleeping = static_cast<Instrument*>(sleeping->next)) {
			if (int r = expectBound(*sleeping, "hibernating")) {
				return r;
			}
			if (int r = settingsOf(*sleeping, *song)) {
				return r;
			}
		}
		if (int r = hibernationLeg(*song)) {
			return r;
		}
		std::fprintf(io.err, "every sound of the song is bound and its settings are coherent\n");
		return 0;
	}

	const OutputType type = kind == "KIT" ? OutputType::KIT : OutputType::SYNTH;
	currentSong = freshSong();
	Instrument* first = loadPreset(bytes, type, *currentSong);
	if (first == nullptr) {
		std::fprintf(io.err, "LOADFAIL\n");
		return 1;
	}
	currentSong->addOutput(first);
	if (int r = expectBound(*first, "added to a running song")) {
		return r;
	}
	Instrument* second = loadPreset(bytes, type, *currentSong);
	if (second == nullptr) {
		std::fprintf(io.err, "LOADFAIL second copy\n");
		return 1;
	}
	currentSong->replaceInstrument(first, second);
	if (int r = expectBound(*second, "replacing another")) {
		return r;
	}
	if (int r = settingsOf(*second, *currentSong)) {
		return r;
	}
	std::fprintf(io.err, "added and replacing sounds are bound and their settings are coherent\n");
	return 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(engine_settings, engine_settings_main)
