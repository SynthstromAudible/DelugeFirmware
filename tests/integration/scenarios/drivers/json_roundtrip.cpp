// JSON round-trip fixture (deserializer step 2b): exercises the JSON side of the record-first
// deserializer end-to-end -- JsonSerializer output, parseJson, and ValueTreeDeserializer JSON mode --
// which the corpus (all XML) otherwise never touches. Run as the json_roundtrip scenario
// (scenarios/drivers/driver.h):
//
//   json_roundtrip <song.XML>
//
// For each song:
//   1. load it via the XML tree (as song_roundtrip does),
//   2. save it to a JSON buffer via JsonSerializer  (jsonA),
//   3. load jsonA via parseJson + ValueTreeDeserializer{Json} into a fresh Song,
//   4. save that to a JSON buffer (jsonB),
//   5. require jsonA == jsonB.
//
// A settled JSON fixed point proves parseJson + JSON-mode replay reproduce exactly what JsonSerializer
// wrote -- i.e. the JSON read path is faithful to the JSON write path. Exit: 0 = settled JSON round
// trip; 1 = load error; 2 = usage/IO; 3 = JSON fixed-point mismatch.

#include "memory/general_memory_allocator.h"
#include "model/global_effectable/global_effectable.h"
#include "model/output.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "scenarios/drivers/tree_load.h"
#include "storage/parse_json.h"
#include "storage/parse_xml.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"

#include "serialization/write_json.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>

#include "scenarios/drivers/driver.h"

namespace {

using deluge::scenarios::driver::Io;

/// Where this driver's prints go: what its process wrote to stdout and stderr.
Io io{};

// JSON counterpart of openTreeFile: JSON objects have an explicit '{' the caller descends through
// (XMLDeserializer::openJsonFile does match('{') before reading names), so enter the root object first,
// then read top-level keys until firstTag -- leaving the adapter positioned inside it.
static Error openTreeFileJson(Deserializer& reader, char const* firstTagName) {
	reader.match('{'); // descend into the root object
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, firstTagName)) {
			return Error::NONE;
		}
		reader.tryReadingFirmwareTagFromFile(tagName, false);
		reader.exitTag(tagName);
	}
	return Error::FILE_CORRUPTED;
}

// Allocate + construct a fresh headless Song ready for loadSong.
static Song* freshSong() {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	return song;
}

// Save `song` to `out` as JSON via JsonSerializer. Returns the serialized length.
static long saveJson(Song* song, char* out, uint32_t outCap) {
	song->setCurrentClip(nullptr); // drop transient view state, as song_roundtrip does
	deluge::scenarios::driver::beginCapture(smJsonSerializer);
	deluge::bridge::saveSong(*song, smJsonSerializer);
	return deluge::scenarios::driver::endCapture(smJsonSerializer, out, outCap);
}

// Load a song from XML bytes via the Value tree (parseXml -> ValueTreeDeserializer{Xml}). `xml` must be
// writable/outlive the call. Returns the Song or nullptr (code in *loadErr).
static Song* loadFromXml(char* xml, size_t xmlLen, Error* loadErr) {
	Song* song = freshSong();
	std::optional<deluge::storage::Value> tree =
	    deluge::storage::parseXml(std::string_view(reinterpret_cast<const char*>(xml), static_cast<size_t>(xmlLen)));
	Error e = Error::FILE_CORRUPTED;
	if (tree) {
		deluge::storage::ValueTreeDeserializer adapter(*tree, deluge::storage::SourceFormat::Xml);
		activeReader = &adapter;
		e = openTreeFile(adapter, "song");
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
	return song;
}

// Load a song from JSON bytes via the Value tree (parseJson -> ValueTreeDeserializer{Json}).
static Song* loadFromJson(char* json, size_t jsonLen, Error* loadErr) {
	Song* song = freshSong();
	std::optional<deluge::storage::Value> tree =
	    deluge::storage::parseJson(std::string_view(reinterpret_cast<const char*>(json), static_cast<size_t>(jsonLen)));
	Error e = Error::FILE_CORRUPTED;
	if (tree) {
		deluge::storage::ValueTreeDeserializer adapter(*tree, deluge::storage::SourceFormat::Json);
		activeReader = &adapter;
		e = openTreeFileJson(adapter, "song");
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
	return song;
}

int json_roundtrip_main(int argc, char** argv, Io driverIo) {
	io = driverIo;
	if (argc < 2) {
		std::fprintf(io.err, "usage: %s <song.XML>\n", argv[0]);
		return 2;
	}
	const char* songPath = argv[1];

	FILE* f = std::fopen(songPath, "rb");
	if (!f) {
		std::fprintf(io.err, "cannot open %s\n", songPath);
		return 2;
	}
	std::fseek(f, 0, SEEK_END);
	long fileLen = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);

	const uint32_t kCap = 16u * 1024 * 1024;
	char* orig = (char*)std::malloc(fileLen + 1);
	char* jsonA = (char*)std::malloc(kCap);
	char* jsonAcopy = (char*)std::malloc(kCap); // writable input for the JSON load
	char* jsonB = (char*)std::malloc(kCap);
	fileLen = (long)std::fread(orig, 1, fileLen, f);
	orig[fileLen] = 0;
	std::fclose(f);

	// Select JSON output. GetSerializer() (used by every deep write site: Output/Sound/MIDI/etc.) and the
	// prologue/file-creation paths all key off this global, so it -- not the serializer argument -- is what
	// makes the whole save emit JSON. Reads go through the Value tree and ignore it.
	writeJsonFlag = true;

	// (1) XML -> Song, (2) Song -> JSON (jsonA)
	Error e;
	Song* song1 = loadFromXml(orig, fileLen, &e);
	if (!song1) {
		std::fprintf(io.err, "LOADFAIL xml -> Error %d\n", (int)e);
		return 1;
	}
	// A name holding characters JSON must escape (a quote, a backslash, a tab) must save as valid JSON and come back
	// unchanged.
	constexpr std::string_view kEscapedName = "ESC \"Q\" \\ T\tEND";
	if (song1->live.media.first_output != nullptr) {
		song1->live.media.first_output->setName(kEscapedName);
	}
	long nA = saveJson(song1, jsonA, kCap);
	if (const char* dump = getenv("JSON_DUMP")) { // debug aid: write the intermediate JSON for inspection
		FILE* df = std::fopen(dump, "wb");
		if (df) {
			std::fwrite(jsonA, 1, nA, df);
			std::fclose(df);
		}
	}

	// The JSON save must be valid JSON. Then the Value JSON writer must reproduce its tree: parse jsonA, write it back
	// with write_json, parse that, and require the same tree (scalar kinds included).
	std::optional<deluge::serialization::Value> today =
	    deluge::serialization::parseJson(std::string_view(jsonA, static_cast<size_t>(nA)));
	if (!today) {
		std::fprintf(io.err, "INVALID JSON: the song's JSON save does not parse\n");
		return 3;
	}
	{
		std::optional<deluge::serialization::Value> rewritten =
		    deluge::serialization::parseJson(deluge::serialization::write_json(*today));
		if (!rewritten || !(*rewritten == *today)) {
			std::fprintf(io.err, "WRITER MISMATCH: write_json does not reproduce today's JSON tree\n");
			return 3;
		}
	}

	// (3) JSON -> Song, (4) Song -> JSON (jsonB)
	std::memcpy(jsonAcopy, jsonA, nA);
	Song* song2 = loadFromJson(jsonAcopy, nA, &e);
	if (!song2) {
		std::fprintf(io.err, "LOADFAIL json -> Error %d\n", (int)e);
		return 1;
	}
	if (song2->live.media.first_output != nullptr && song2->live.media.first_output->name != kEscapedName) {
		std::fprintf(io.err, "ESCAPE MISMATCH: an escaped output name came back as \"%s\"\n",
		             song2->live.media.first_output->name.c_str());
		return 3;
	}
	long nB = saveJson(song2, jsonB, kCap);
	if (const char* dump = getenv("JSON_DUMP2")) {
		FILE* df = std::fopen(dump, "wb");
		if (df) {
			std::fwrite(jsonB, 1, nB, df);
			std::fclose(df);
		}
	}

	// (5) require jsonA == jsonB
	bool settled = (nA == nB) && (std::memcmp(jsonA, jsonB, nA) == 0);
	std::fprintf(io.err, "jsonA=%ld jsonB=%ld %s\n", nA, nB, settled ? "SETTLED" : "MISMATCH");
	if (!settled) {
		return 3;
	}
	return 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(json_roundtrip, json_roundtrip_main)
