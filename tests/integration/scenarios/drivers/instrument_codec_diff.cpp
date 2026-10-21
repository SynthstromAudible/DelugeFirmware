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
//   instrument_codec_decode  every sound's element as read (its slot, or a preset's element) decodes, into a new
//                            sound's state, to the state the app's reader left; INSTRUMENT_DUMP_STATE=<path> writes
//                            the first sound's state as the reader left it (dump_state, JSON) to <path>
//   instrument_state_roundtrip  every sound's element as the app writes it, decoded into a new sound's state and
//                            encoded again, is the same tree (cut to the instrument's keys)
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
#include "serialization/write_json.h"
#include "storage/storage_manager.h"
#include "storage/tree_document.h"
#include "storage/value_serializer.h"
#include "storage/value_tree_deserializer.h"
#include "util/firmware_version.h"

#include <cstdio>
#include <cstdlib>
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

/// One sound of the document: what to call it, the sound, its element as the app writes it, and the element it
/// was read from.
struct SoundEntry {
	std::string name;
	Sound* sound;
	Value element;
	std::optional<Value> read;
	bool drum{false};
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
		Sound* sound = static_cast<Sound*>(static_cast<SoundInstrument*>(&output));
		// The element the app wrote and read back, as the sound read it, which is what this compares with the encoding.
		std::optional<Value> read = sound->takeLoadedElement();
		into.push_back({label, sound, *element, std::move(read)});
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
			Sound* sound = static_cast<Sound*>(static_cast<SoundDrum*>(drum));
			std::optional<Value> read = sound->takeLoadedElement();
			into.push_back({label + " drum " + std::to_string(drumIndex), sound, *drumElement, std::move(read), true});
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

/// The scalar at @p path (slash-separated keys and indexes) in @p tree, as text, or "(none)".
std::string valueAt(const Value& tree, std::string_view path) {
	const Value* node = &tree;
	while (node != nullptr && !path.empty()) {
		const size_t slash = path.find('/');
		const std::string_view part = path.substr(0, slash);
		path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
		if (node->isArray()) {
			const size_t index = static_cast<size_t>(std::atoi(std::string(part).c_str()));
			node = index < node->items().size() ? &node->items()[index] : nullptr;
		}
		else if (node->isMap()) {
			node = node->find(part);
		}
		else {
			node = nullptr;
		}
	}
	return node != nullptr && node->isScalar() ? node->str() : std::string("(none)");
}

/// The state the app's reader left in @p sound.
deluge::instrument::SoundState loadedState(Sound& sound) {
	return sound.boundState() != nullptr ? *sound.boundState() : sound.seededState();
}

int decodeDiff(std::vector<SoundEntry>& sounds, int32_t magnitude) {
	// A new sound of each kind: what a reader starts from.
	Instrument* freshSynth = StorageManager::createNewInstrument(OutputType::SYNTH);
	Drum* freshDrum = StorageManager::createNewDrum(DrumType::SOUND);
	if (freshSynth == nullptr || freshDrum == nullptr) {
		std::fprintf(io.err, "cannot make a new sound\n");
		return 2;
	}
	const deluge::instrument::SoundState newSynth =
	    static_cast<Sound*>(static_cast<SoundInstrument*>(freshSynth))->seededState();
	const deluge::instrument::SoundState newDrum =
	    static_cast<Sound*>(static_cast<SoundDrum*>(freshDrum))->seededState();
	if (std::string d = deluge::serialization::first_difference(
	        deluge::instrument::dump_state(newSynth),
	        deluge::instrument::dump_state(deluge::instrument::new_synth_state()));
	    !d.empty()) {
		const Value fresh = deluge::instrument::dump_state(newSynth);
		const Value constant = deluge::instrument::dump_state(deluge::instrument::new_synth_state());
		std::fprintf(io.err, "CODEC DIFF: a new synth and new_synth_state() differ at %s (synth %s, constant %s)\n",
		             d.c_str(), valueAt(fresh, d).c_str(), valueAt(constant, d).c_str());
		return 3;
	}
	bool dumped = false;
	for (SoundEntry& entry : sounds) {
		const Value loaded = deluge::instrument::dump_state(loadedState(*entry.sound));
		if (const char* dump = std::getenv("INSTRUMENT_DUMP_STATE"); dump != nullptr && !dumped) {
			if (FILE* df = std::fopen(dump, "wb")) {
				const std::string json = deluge::serialization::write_json(loaded);
				std::fwrite(json.data(), 1, json.size(), df);
				std::fclose(df);
			}
			dumped = true;
		}
		if (!entry.read) {
			std::fprintf(io.err, "CODEC DIFF: %s: no element was kept from the read\n", entry.name.c_str());
			return 3;
		}
		deluge::instrument::SoundState decoded = entry.drum ? newDrum : newSynth;
		if (auto result = deluge::instrument::decode(*entry.read, decoded, {.input_tick_magnitude = magnitude});
		    !result) {
			std::fprintf(io.err, "CODEC DIFF: %s: decode failed (%d)\n", entry.name.c_str(),
			             static_cast<int>(result.error()));
			return 3;
		}
		if (std::string d = deluge::serialization::first_difference(loaded, deluge::instrument::dump_state(decoded));
		    !d.empty()) {
			const Value decodedDump = deluge::instrument::dump_state(decoded);
			std::fprintf(io.err, "CODEC DIFF: %s: the read state and the decode differ at %s (read %s, decoded %s)\n",
			             entry.name.c_str(), d.c_str(), valueAt(loaded, d).c_str(), valueAt(decodedDump, d).c_str());
			return 3;
		}
	}
	std::fprintf(io.err, "codec decodes %zu sounds as the reader does\n", sounds.size());
	return 0;
}

int roundtrip(std::vector<SoundEntry>& sounds, int32_t magnitude) {
	Instrument* freshSynth = StorageManager::createNewInstrument(OutputType::SYNTH);
	Drum* freshDrum = StorageManager::createNewDrum(DrumType::SOUND);
	if (freshSynth == nullptr || freshDrum == nullptr) {
		std::fprintf(io.err, "cannot make a new sound\n");
		return 2;
	}
	const deluge::instrument::SoundState newSynth =
	    static_cast<Sound*>(static_cast<SoundInstrument*>(freshSynth))->seededState();
	const deluge::instrument::SoundState newDrum =
	    static_cast<Sound*>(static_cast<SoundDrum*>(freshDrum))->seededState();
	const deluge::engine::CodecContext context{.input_tick_magnitude = magnitude};
	for (SoundEntry& entry : sounds) {
		deluge::instrument::SoundState state = entry.drum ? newDrum : newSynth;
		if (auto result = deluge::instrument::decode(entry.element, state, context); !result) {
			std::fprintf(io.err, "CODEC DIFF: %s: the written element does not decode\n", entry.name.c_str());
			return 3;
		}
		const Value again = deluge::instrument::encode(state, context);
		const Value written = stateKeysOf(entry.element);
		if (std::string d = deluge::serialization::first_difference(written, again); !d.empty()) {
			std::fprintf(io.err,
			             "CODEC DIFF: %s: decode then encode changes the element at %s (written %s, again %s)\n",
			             entry.name.c_str(), d.c_str(), valueAt(written, d).c_str(), valueAt(again, d).c_str());
			return 3;
		}
	}
	std::fprintf(io.err, "state round-trips for %zu sounds\n", sounds.size());
	return 0;
}

/// What a scenario checks of the sounds it loads.
enum class Check : uint8_t {
	ENCODE,
	DECODE,
	ROUNDTRIP,
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
		                        deluge::storage::formatOfPath(argv[1]) == deluge::storage::DocumentFormat::JSON,
		                        firstTag, altTag);
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
	case Check::DECODE:
		return decodeDiff(sounds, song->getInputTickMagnitude());
	case Check::ROUNDTRIP:
		return roundtrip(sounds, song->getInputTickMagnitude());
	}
	return 2;
}

int instrument_codec_encode_main(int argc, char** argv, Io driverIo) {
	return run(Check::ENCODE, argc, argv, driverIo);
}

int instrument_codec_decode_main(int argc, char** argv, Io driverIo) {
	return run(Check::DECODE, argc, argv, driverIo);
}

int instrument_state_roundtrip_main(int argc, char** argv, Io driverIo) {
	return run(Check::ROUNDTRIP, argc, argv, driverIo);
}

} // namespace

DELUGE_DRIVER_SCENARIO(instrument_codec_encode, instrument_codec_encode_main)
DELUGE_DRIVER_SCENARIO(instrument_codec_decode, instrument_codec_decode_main)
DELUGE_DRIVER_SCENARIO(instrument_state_roundtrip, instrument_state_roundtrip_main)
