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

// preset_roundtrip <preset.XML|preset.json> <SYNTH|KIT> [gen1_out_path] — load a synth or kit preset, save it as a
// preset is saved, load that and save again; the two saves must be identical.
//
// Exit: 0 settled; 1 load failure; 2 usage/IO; 3 not settled. PRESET_MISMATCH_DUMP=<path> keeps both saves of a
// preset that does not settle. PRESET_JSON_OUT=<path> also writes the preset, loaded, as a JSON preset stamped with
// this firmware's version.

#include "memory/general_memory_allocator.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/instrument.h"
#include "model/song/song.h"
#include "scenarios/drivers/driver.h"
#include "storage/storage_manager.h"
#include "storage/tree_document.h"
#include "util/firmware_version.h"
#include "version.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

namespace {

using deluge::scenarios::driver::Io;

/// Where this driver's prints go: what its process wrote to stdout and stderr.
Io io{};

/// Load the preset in @p bytes and save it into @p out; -1 on a load failure, its code in @p loadError.
///
/// The save is the instrument's own writer without a clip, as a song writes it (a preset save also passes the
/// instrument's clip, which this driver has none of, and writes the firmware version). So @p savedHere says the bytes
/// are a save this firmware made, and they are read as the current firmware's, as a preset's firmwareVersion makes the
/// real reload read them.
long loadThenSave(std::string_view bytes, OutputType type, bool savedHere, char* out, uint32_t cap, Error* loadError,
                  bool isJson = false, Serializer& saveAs = smSerializer) {
	char const* firstTag = nullptr;
	char const* altTag = nullptr;
	StorageManager::instrumentDocumentTags(type, &firstTag, &altTag);
	deluge::storage::TreeDocument document;
	*loadError = document.open(std::span{reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}, isJson,
	                           firstTag, altTag);
	if (*loadError != Error::NONE) {
		return -1;
	}
	if (savedHere) { // after the open, which reads an unstamped document as the oldest official firmware
		song_firmware_version = current_firmware_version();
	}
	Instrument* instrument = nullptr;
	std::string name = "PRESET";
	std::string dir = type == OutputType::SYNTH ? "SYNTHS" : "KITS";
	*loadError =
	    StorageManager::parseInstrumentDocument(document, currentSong, nullptr, type, false, &instrument, &name, &dir);
	document.release();
	if (*loadError != Error::NONE) {
		return -1;
	}
	const bool json = &saveAs == &smJsonSerializer;
	FileWriter& writer = json ? static_cast<FileWriter&>(smJsonSerializer) : static_cast<FileWriter&>(smSerializer);
	deluge::scenarios::driver::beginCapture(writer);
	long n = -1;
	{
		ActiveWriterScope scope(saveAs);
		saveAs.reset();
		if (!json) {
			saveAs.write(StorageManager::kXmlDocumentHead);
		}
		instrument->writeToFile(nullptr, currentSong);
		n = deluge::scenarios::driver::endCapture(writer, out, cap);
	}
	currentSong->deleteOutput(instrument); // as the app deletes an output: its param managers, then retirement
	return n;
}

/// A song for the preset to be loaded into, as a preset is always loaded into one.
Song* freshSong();

/// Writes the preset in @p bytes, loaded, to @p path as a JSON preset stamped with this firmware's version, as a
/// preset save stamps it.
bool writeJson(std::string_view bytes, OutputType type, bool isJson, const char* path) {
	currentSong = freshSong();
	constexpr uint32_t kCap = 4u * 1024 * 1024;
	char* json = static_cast<char*>(std::malloc(kCap));
	Error error = Error::NONE;
	const long n = loadThenSave(bytes, type, false, json, kCap, &error, isJson, smJsonSerializer);
	std::string text = n > 0 ? std::string(json, static_cast<size_t>(n)) : std::string{};
	const size_t open = text.find('{');
	bool ok = open != std::string::npos;
	if (ok) {
		// The save opens the document with `{"sound": {`: the stamp goes first, as the document's own member.
		text.insert(open + 1, std::string("\n\"firmwareVersion\": \"") + kFirmwareVersionStringShort + "\",");
		FILE* f = std::fopen(path, "wb");
		ok = f != nullptr && std::fwrite(text.data(), 1, text.size(), f) == text.size();
		if (f != nullptr) {
			std::fclose(f);
		}
	}
	std::free(json);
	return ok;
}

/// A song for the preset to be loaded into, as a preset is always loaded into one.
Song* freshSong() {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	return song;
}

int preset_roundtrip_main(int argc, char** argv, Io driverIo) {
	io = driverIo;
	currentSong = freshSong(); // the driver step puts the booted song back afterwards
	if (argc < 3) {
		std::fprintf(io.err, "usage: %s <preset.XML> <SYNTH|KIT> [gen1_out_path]\n", argv[0]);
		return 2;
	}
	const OutputType type = std::string_view{argv[2]} == "KIT" ? OutputType::KIT : OutputType::SYNTH;
	FILE* f = std::fopen(argv[1], "rb");
	if (f == nullptr) {
		std::fprintf(io.err, "cannot open %s\n", argv[1]);
		return 2;
	}
	std::string orig;
	char chunk[4096];
	for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) {
		orig.append(chunk, n);
	}
	std::fclose(f);

	const bool inputIsJson = std::string_view{argv[1]}.ends_with(".json");
	constexpr uint32_t kCap = 4u * 1024 * 1024;
	char* gen1 = static_cast<char*>(std::malloc(kCap));
	char* gen2 = static_cast<char*>(std::malloc(kCap));
	Error e1 = Error::NONE;
	Error e2 = Error::NONE;
	const long n1 = loadThenSave(orig, type, false, gen1, kCap, &e1, inputIsJson);
	if (n1 < 0) {
		std::fprintf(io.err, "LOADFAIL original -> Error %d\n", static_cast<int>(e1));
		return 1;
	}
	// A drum's id is its song's serial: the second generation is read into a song of its own, as the first was, so the
	// ids it mints are the first's.
	currentSong = freshSong();
	const long n2 = loadThenSave(std::string_view{gen1, static_cast<size_t>(n1)}, type, true, gen2, kCap, &e2);
	if (n2 < 0) {
		std::fprintf(io.err, "LOADFAIL gen1 -> Error %d\n", static_cast<int>(e2));
		return 1;
	}
	const bool settled = n1 == n2 && std::memcmp(gen1, gen2, static_cast<size_t>(n1)) == 0;
	std::fprintf(io.err, "gen1=%ld gen2=%ld %s\n", n1, n2, settled ? "SETTLED" : "MISMATCH");
	int code = settled ? 0 : 3;
	// PRESET_MISMATCH_DUMP=<path>: a preset that does not settle leaves both saves at <path>.gen1 and <path>.gen2.
	if (const char* dump = std::getenv("PRESET_MISMATCH_DUMP"); dump != nullptr && !settled) {
		for (auto [suffix, bytes, len] : {std::tuple{".gen1", gen1, n1}, std::tuple{".gen2", gen2, n2}}) {
			FILE* df = std::fopen((std::string(dump) + suffix).c_str(), "wb");
			std::fwrite(bytes, 1, static_cast<size_t>(len), df);
			std::fclose(df);
		}
	}
	if (const char* jsonOut = std::getenv("PRESET_JSON_OUT"); jsonOut != nullptr && settled) {
		code = writeJson(orig, type, inputIsJson, jsonOut) ? code : 2;
	}
	if (settled && argc > 3) {
		FILE* of = std::fopen(argv[3], "wb");
		if (of == nullptr || std::fwrite(gen1, 1, static_cast<size_t>(n1), of) != static_cast<size_t>(n1)) {
			std::fprintf(io.err, "cannot write %s\n", argv[3]);
			code = 2;
		}
		if (of != nullptr) {
			std::fclose(of);
		}
	}
	std::free(gen1);
	std::free(gen2);
	return code;
}

} // namespace

DELUGE_DRIVER_SCENARIO(preset_roundtrip, preset_roundtrip_main)
