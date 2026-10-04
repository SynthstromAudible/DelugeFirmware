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

// The Deluge instrument's codec against the sounds of a document as the app loads and writes them. One scenario per
// check, each taking <song.XML|song.json|preset.XML> <SONG|SYNTH|KIT>:
//
//   instrument_codec_encode  every sound's element as the app writes it, cut to the instrument's keys, equals the
//                            codec's encode of the sound's state
//
// Exit: 0 agree; 1 load failure; 2 usage/IO; 3 a sound differs (its name and the first differing path are printed).

#include "engines/deluge/codec.h"
#include "memory/general_memory_allocator.h"
#include "model/drum/drum.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "scenarios/drivers/driver.h"
#include "scenarios/drivers/tree_load.h"
#include "serialization/tree.h"
#include "storage/storage_manager.h"
#include "storage/tree_document.h"
#include "storage/value_serializer.h"
#include "storage/value_tree_deserializer.h"
#include "util/firmware_version.h"

#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using deluge::scenarios::driver::Io;
using deluge::serialization::Value;

/// Where this driver's prints go: what its process wrote to stdout and stderr.
Io io{};

Song* freshSong() {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	return song;
}

/// One sound of the document: what to call it, the sound, and its element as the app writes it now.
struct SoundEntry {
	std::string name;
	Sound* sound;
	Value element;
};

/// The element an output writes, unboxed: @p output written as an item of its song's list.
std::optional<Value> writtenElement(Output& output, Song& song) {
	ValueSerializer writer(ValueSerializer::Writes::ITEMS);
	{
		ActiveWriterScope scope(writer);
		output.writeToFile(nullptr, &song);
	}
	std::optional<Value> items = writer.finish();
	if (!items || !items->isArray() || items->items().size() != 1 || items->items().front().entries().size() != 1) {
		return std::nullopt;
	}
	return items->items().front().entries().front().second;
}

/// The sounds of @p output (a synth's own, or a kit's sound drums in order) with their written elements.
void collect(Output& output, Song& song, const std::string& label, std::vector<SoundEntry>& into) {
	if (output.type != OutputType::SYNTH && output.type != OutputType::KIT) {
		return;
	}
	std::optional<Value> element = writtenElement(output, song);
	if (!element) {
		return;
	}
	if (output.type == OutputType::SYNTH) {
		into.push_back({label, static_cast<Sound*>(static_cast<SoundInstrument*>(&output)), *element});
		return;
	}
	const Value* sources = element->find("soundSources");
	if (sources == nullptr) {
		return;
	}
	auto children = deluge::serialization::children(*sources);
	size_t index = 0;
	int32_t drumIndex = 0;
	for (Drum* drum = static_cast<Kit&>(output).firstDrum; drum != nullptr; drum = drum->next, drumIndex++) {
		if (index >= children.size()) {
			break;
		}
		const Value* drumElement = children[index++].node;
		if (drum->type == DrumType::SOUND) {
			into.push_back({label + " drum " + std::to_string(drumIndex),
			                static_cast<Sound*>(static_cast<SoundDrum*>(drum)), *drumElement});
		}
	}
}

/// The entries of @p element the instrument writes, in order.
Value stateKeysOf(const Value& element) {
	auto map = Value::makeMap();
	for (const auto& [key, value] : element.entries()) {
		if (deluge::instrument::is_state_key(key)) {
			map.set(key, value);
		}
	}
	return map;
}

int encodeDiff(std::vector<SoundEntry>& sounds, int32_t magnitude) {
	for (SoundEntry& entry : sounds) {
		const deluge::instrument::SoundState state =
		    entry.sound->boundState() != nullptr ? *entry.sound->boundState() : entry.sound->seededState();
		const Value encoded = deluge::instrument::encode(state, {.input_tick_magnitude = magnitude});
		const Value written = stateKeysOf(entry.element);
		if (std::string d = deluge::serialization::first_difference(written, encoded); !d.empty()) {
			std::fprintf(io.err, "CODEC DIFF: %s: the written element and the encode differ at %s\n",
			             entry.name.c_str(), d.c_str());
			return 3;
		}
	}
	std::fprintf(io.err, "codec agrees on %zu sounds\n", sounds.size());
	return 0;
}

/// What a scenario checks of the sounds it loads.
enum class Check : uint8_t {
	ENCODE,
};

int run(Check check, int argc, char** argv, Io driverIo) {
	io = driverIo;
	if (argc < 3) {
		std::fprintf(io.err, "usage: %s <file> <SONG|SYNTH|KIT>\n", argv[0]);
		return 2;
	}
	const std::string_view kind = argv[2];
	FILE* f = std::fopen(argv[1], "rb");
	if (f == nullptr) {
		std::fprintf(io.err, "cannot open %s\n", argv[1]);
		return 2;
	}
	std::string text;
	char chunk[4096];
	for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) {
		text.append(chunk, n);
	}
	std::fclose(f);

	Song* song = freshSong();
	currentSong = song;
	std::vector<SoundEntry> sounds;
	if (kind == "SONG") {
		const auto format = deluge::storage::formatOfPath(argv[1]);
		std::optional<Value> tree = parseDocument(text, format);
		if (!tree) {
			std::fprintf(io.err, "LOADFAIL does not parse\n");
			return 1;
		}
		Song* loaded = freshSong();
		deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(format));
		activeReader = &adapter;
		Error e = openTreeFile(adapter, "song", format);
		if (e == Error::NONE) {
			e = deluge::bridge::loadSong(*loaded, adapter);
		}
		activeReader = &smDeserializer;
		if (e != Error::NONE) {
			std::fprintf(io.err, "LOADFAIL -> Error %d\n", static_cast<int>(e));
			return 1;
		}
		song = loaded;
		currentSong = song;
		song->setCurrentClip(nullptr);
		int index = 0;
		for (Output* output = song->live.media.first_output; output != nullptr; output = output->next, index++) {
			collect(*output, *song, "output " + std::to_string(index), sounds);
		}
	}
	else {
		const OutputType type = kind == "KIT" ? OutputType::KIT : OutputType::SYNTH;
		char const* firstTag = nullptr;
		char const* altTag = nullptr;
		StorageManager::instrumentDocumentTags(type, &firstTag, &altTag);
		deluge::storage::TreeDocument document;
		Error e = document.open(std::span{reinterpret_cast<const uint8_t*>(text.data()), text.size()},
		                        /*isJson=*/false, firstTag, altTag);
		Instrument* instrument = nullptr;
		if (e == Error::NONE) {
			std::string name = "PRESET";
			std::string dir = type == OutputType::SYNTH ? "SYNTHS" : "KITS";
			e = StorageManager::parseInstrumentDocument(document, song, nullptr, type, false, &instrument, &name, &dir);
		}
		if (e != Error::NONE) {
			std::fprintf(io.err, "LOADFAIL -> Error %d\n", static_cast<int>(e));
			return 1;
		}
		collect(*instrument, *song, "preset", sounds);
	}

	switch (check) {
	case Check::ENCODE:
		return encodeDiff(sounds, song->getInputTickMagnitude());
	}
	return 2;
}

int instrument_codec_encode_main(int argc, char** argv, Io driverIo) {
	return run(Check::ENCODE, argc, argv, driverIo);
}

} // namespace

DELUGE_DRIVER_SCENARIO(instrument_codec_encode, instrument_codec_encode_main)
