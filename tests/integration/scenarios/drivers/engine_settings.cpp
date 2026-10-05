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
// replaces another. Exit: 0 every check holds; 1 the file does not load; 2 usage/IO; 3 a check fails (printed).

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
#include "storage/document_format.h"
#include "storage/storage_manager.h"
#include "storage/tree_document.h"
#include "storage/value_tree_deserializer.h"

#include <cstdio>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace {

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
		}
		for (Instrument* sleeping = song->live.media.first_hibernating_instrument; sleeping != nullptr;
		     sleeping = static_cast<Instrument*>(sleeping->next)) {
			if (int r = expectBound(*sleeping, "hibernating")) {
				return r;
			}
		}
		std::fprintf(io.err, "every sound of the song is bound\n");
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
	std::fprintf(io.err, "added and replacing sounds are bound\n");
	return 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(engine_settings, engine_settings_main)
