// Phase-0 round-trip driver: load a Deluge song XML headless, re-serialize it,
// and check the canonical fixed point. Safety net for the project-model
// extraction (docs/dev/project_model_extraction.md, Phase 0).
//
//   song_roundtrip <song.XML> [gen1_out_path]
//
//   gen1 = save(load(original))
//   gen2 = save(load(gen1))
//   require gen1 == gen2            (the entry is "settled")
//   if gen1_out_path given, write gen1 there (the runner hashes it -> fixture)
//
// Exit: 0 = settled; 1 = load error; 2 = usage/IO; 3 = fixed-point mismatch.
//
// The load/save path is deliberately exercised with no audio engine, display
// hardware, USB, or SD — only the allocator (host BSP) + a stub display. The
// firmware's load<->execution couplings are gated/neutralised here, not faked:
//   - AudioEngine yields gated via FileReader::callRoutines (in-memory read).
//   - View selection cleared before save (writeToFile serialises UI state).
// See the design doc for the full list.

#include "deluge-project/serialization.h"
#include "hid/display/display.h"
#include "hid/display/seven_segment.h"
#include "memory/general_memory_allocator.h"
#include "model/global_effectable/global_effectable.h"
#include "model/song/song.h"
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

// load(xml) -> save into `out`. Returns serialized length, or -1 on load error
// (code in *loadErr). `xml` must be writable (the reader null-terminates tokens
// in place).
static long load_then_save(char* xml, size_t xmlLen, char* out, uint32_t outCap, Error* loadErr) {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);

	// Do NOT set currentSong before readFromFile. On firmware the song under load is
	// `preLoadedSong`; `currentSong` still points at the *old* song, so the action logger
	// (which setBPM triggers mid-load) inspects the old song's clips. Pointing currentSong
	// at the half-loaded song instead makes getNewAction walk clips whose outputs aren't
	// wired yet -> null deref. Set it only once the load is complete (writeToFile needs it).
	// Load via the Value tree (record-first deserializer, step 2b): parse the whole XML to a Value,
	// then replay it to the existing readFromFile through ValueTreeDeserializer instead of streaming
	// the bytes. Must be bit-identical to the streaming load (the golden baselines are stream-made).
	std::optional<deluge::storage::Value> tree =
	    deluge::storage::parseXml(std::string_view(reinterpret_cast<const char*>(xml), static_cast<size_t>(xmlLen)));
	Error e = Error::FILE_CORRUPTED;
	if (tree) {
		deluge::storage::ValueTreeDeserializer adapter(*tree, deluge::storage::SourceFormat::Xml);
		// Point the active-reader gate at the adapter: its shouldCallRoutines()==false gates the
		// AudioEngine render off during load (the in-memory tree never yields), exactly as the streaming
		// XMLDeserializer's callRoutines==false did. (Interior read sites like MIDIInstrument modKnobs now
		// take the reader param, so activeDeserializer itself need not point here.)
		activeReader = &adapter;
		e = openTreeFile(adapter, "song");
		if (e == Error::NONE) {
			e = deluge::project::loadSong(*song, adapter);
		}
	}
	*loadErr = e;
	if (e != Error::NONE) {
		return -1;
	}
	currentSong = song; // load complete; some deep write paths consult the global

	// Clear the transient view selection before saving: writeToFile() otherwise serializes
	// "selected"/"beingEdited" for the current clip and consults the (null, headless) root UI.
	// That is view state, not project data — dropping it yields a view-independent canonical
	// form that still round-trips to a fixed point.
	song->setCurrentClip(nullptr);

	smSerializer.setMemoryBuffer(out, outCap);
	deluge::project::saveSong(*song, smSerializer);
	return smSerializer.bytesWritten();
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <song.XML> [gen1_out_path]\n", argv[0]);
		return 2;
	}
	const char* songPath = argv[1];
	const char* outPath = (argc > 2) ? argv[2] : nullptr;

	FILE* f = std::fopen(songPath, "rb");
	if (!f) {
		std::fprintf(stderr, "cannot open %s\n", songPath);
		return 2;
	}
	std::fseek(f, 0, SEEK_END);
	long fileLen = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);

	const uint32_t kCap = 16u * 1024 * 1024; // largest songs are well under this
	char* orig = (char*)std::malloc(fileLen + 1);
	char* gen1 = (char*)std::malloc(kCap);
	char* gen1copy = (char*)std::malloc(kCap); // writable input for the 2nd load
	char* gen2 = (char*)std::malloc(kCap);
	fileLen = (long)std::fread(orig, 1, fileLen, f);
	orig[fileLen] = 0;
	std::fclose(f);

	// Give defensive display->popup paths a real target so they don't deref null.
	display = new deluge::hid::display::SevenSegment;

	Error e1, e2;
	long n1 = load_then_save(orig, fileLen, gen1, kCap, &e1);
	if (n1 < 0) {
		std::fprintf(stderr, "LOADFAIL original -> Error %d\n", (int)e1);
		_exit(1);
	}

	std::memcpy(gen1copy, gen1, n1);
	long n2 = load_then_save(gen1copy, n1, gen2, kCap, &e2);
	if (n2 < 0) {
		std::fprintf(stderr, "LOADFAIL gen1 -> Error %d\n", (int)e2);
		_exit(1);
	}

	bool settled = (n1 == n2) && (std::memcmp(gen1, gen2, n1) == 0);
	std::fprintf(stderr, "in=%ld gen1=%ld gen2=%ld %s\n", fileLen, n1, n2, settled ? "SETTLED" : "MISMATCH");
	if (!settled) {
		_exit(3);
	}

	if (outPath) {
		FILE* of = std::fopen(outPath, "wb");
		if (!of || std::fwrite(gen1, 1, n1, of) != (size_t)n1) {
			std::fprintf(stderr, "cannot write %s\n", outPath);
			_exit(2);
		}
		std::fclose(of);
	}
	_exit(0);
}
