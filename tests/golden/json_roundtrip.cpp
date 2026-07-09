// JSON round-trip fixture (deserializer step 2b): exercises the JSON side of the record-first
// deserializer end-to-end -- JsonSerializer output, parseJson, and ValueTreeDeserializer JSON mode --
// which the corpus (all XML) otherwise never touches.
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

#include "deluge-project/serialization.h"
#include "hid/display/display.h"
#include "hid/display/seven_segment.h"
#include "memory/general_memory_allocator.h"
#include "model/global_effectable/global_effectable.h"
#include "model/song/song.h"
#include "storage/parse_json.h"
#include "storage/parse_xml.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"
#include "tree_load.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>
#include <unistd.h>

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
	smJsonSerializer.setMemoryBuffer(out, outCap);
	deluge::project::saveSong(*song, smJsonSerializer);
	return smJsonSerializer.bytesWritten();
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
			e = deluge::project::loadSong(*song, adapter);
		}
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
			e = deluge::project::loadSong(*song, adapter);
		}
	}
	*loadErr = e;
	if (e != Error::NONE) {
		return nullptr;
	}
	currentSong = song;
	return song;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <song.XML>\n", argv[0]);
		return 2;
	}
	const char* songPath = argv[1];

	FILE* f = std::fopen(songPath, "rb");
	if (!f) {
		std::fprintf(stderr, "cannot open %s\n", songPath);
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

	display = new deluge::hid::display::SevenSegment;

	// Select JSON output. GetSerializer() (used by every deep write site: Output/Sound/MIDI/etc.) and the
	// prologue/file-creation paths all key off this global, so it -- not the serializer argument -- is what
	// makes the whole save emit JSON. Reads go through the Value tree and ignore it.
	writeJsonFlag = true;

	// (1) XML -> Song, (2) Song -> JSON (jsonA)
	Error e;
	Song* song1 = loadFromXml(orig, fileLen, &e);
	if (!song1) {
		std::fprintf(stderr, "LOADFAIL xml -> Error %d\n", (int)e);
		_exit(1);
	}
	long nA = saveJson(song1, jsonA, kCap);
	if (const char* dump = getenv("JSON_DUMP")) { // debug aid: write the intermediate JSON for inspection
		FILE* df = std::fopen(dump, "wb");
		if (df) {
			std::fwrite(jsonA, 1, nA, df);
			std::fclose(df);
		}
	}

	// (3) JSON -> Song, (4) Song -> JSON (jsonB)
	std::memcpy(jsonAcopy, jsonA, nA);
	Song* song2 = loadFromJson(jsonAcopy, nA, &e);
	if (!song2) {
		std::fprintf(stderr, "LOADFAIL json -> Error %d\n", (int)e);
		_exit(1);
	}
	long nB = saveJson(song2, jsonB, kCap);

	// (5) require jsonA == jsonB
	bool settled = (nA == nB) && (std::memcmp(jsonA, jsonB, nA) == 0);
	std::fprintf(stderr, "jsonA=%ld jsonB=%ld %s\n", nA, nB, settled ? "SETTLED" : "MISMATCH");
	if (!settled) {
		_exit(3);
	}
	_exit(0);
}
