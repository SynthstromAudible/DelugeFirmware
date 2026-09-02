/// @file
/// @brief Characterisation of the browser's file catalogue -- the listing/sort/search/cull
///        machinery that task 1 of this plan moves out of `Browser` and into
///        `deluge::storage::FileCatalogue`.
///
/// **This spec was written and made to pass against the LEGACY statics first**, before a line of
/// the extraction existed, and is re-run unchanged afterwards. That order is the whole point: a
/// spec written after a move can only witness that the new code agrees with itself. Every number
/// asserted below (entry counts, orders, indices, cull windows) was observed on
/// `Browser::readFileItemsForFolder` / `sortFileItems` / `searchFileItems` / `cullSomeFileItems`
/// as they stood at `a6b915f92`, and is the invariant the move must preserve.
///
/// **Why this file lives in `sim/scenes_spec/` and not `tests/spec/storage/`** (the location the
/// task brief names): the host suite (`tests/spec/CMakeLists.txt`) deliberately builds a
/// dependency-free island -- it lists individual dependency-free translation units by hand and
/// never builds `deluge_app`'s closure at all. `Browser` is a `QwertyUI` that reaches `display`,
/// `AudioEngine`, `StorageManager`, `currentSong` and the host BSP's SD passthrough, so it cannot
/// be linked there. `sim/scenes_spec` is this repo's only model-backed CppSpec driver (it links
/// `deluge_app` and the host BSP, `sim/scenes_spec/CMakeLists.txt`), so a spec that drives the real
/// browser statics over a real filesystem has exactly one home. Its `*_spec.cpp` glob picks this
/// file up with no CMake change.
///
/// **Card hygiene.** Nothing here writes to the card. The two listing examples point
/// `$DELUGE_SD_ROOT` at the committed fixture (`tests/fixtures/card`) explicitly rather than
/// relying on the aggregate `scenes_spec` ctest row's env -- the per-spec row
/// `create_specs_driver` registers does *not* set it -- and only ever `opendir`/`readdir` it, so
/// `git status --porcelain tests/fixtures` stays clean. `SdRoot` is the same
/// save/point/restore shape as `NoSdRoot` (`context_menu_scene_spec.cpp`) and `ErasedFlash`
/// (`support/host_flash.h`).

#include "gui/ui/browser/browser.h"

#include "definitions_cxx.hpp"
#include "processing/engines/audio_engine.h"
#include "storage/file_item.h"
#include "util/name_compare.h"

#include "cppspec.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

// The two file-scope globals `readFileItemsForFolder` sets and `getNewFileItem`/`cullSomeFileItems`
// read (`storage/file_catalogue.cpp:53 maxNumFileItemsNow`, `:54 catalogSearchDirection`; they were
// `browser.cpp`'s before the extraction). They
// have external linkage but no header declares them -- the same situation `menu_ranges_spec.cpp`
// records for the file-scope menu globals -- so this spec declares them to drive a cull directly,
// deterministically, instead of hoping a mid-listing cull lands where readdir order happens to put
// it.
extern int32_t maxNumFileItemsNow;
extern int32_t catalogSearchDirection;

namespace {

/// @brief Point `$DELUGE_SD_ROOT` at a directory for the scope, and restore whatever was there.
///
/// The host BSP resolves every filesystem call against `$DELUGE_SD_ROOT` on each call
/// (`src/bsp/host/host_efatfs_passthrough.cpp:164 resolve_root_relative`,
/// `:547 deluge_efatfs_dir_open`), so switching it mid-process is enough to aim a listing.
struct SdRoot {
	std::string saved;
	bool had;
	explicit SdRoot(const std::string& root) {
		const char* prev = std::getenv("DELUGE_SD_ROOT");
		had = prev != nullptr;
		if (had) {
			saved = prev;
		}
		(void)::setenv("DELUGE_SD_ROOT", root.c_str(), 1);
	}
	~SdRoot() {
		if (had) {
			(void)::setenv("DELUGE_SD_ROOT", saved.c_str(), 1);
		}
		else {
			(void)::unsetenv("DELUGE_SD_ROOT");
		}
	}
	SdRoot(const SdRoot&) = delete;
	SdRoot& operator=(const SdRoot&) = delete;
};

/// @brief Hold `AudioEngine`'s own re-entrancy latch for the scope, so the catalogue's yield points
///        are no-ops.
///
/// `emptyFileItems()` and `deleteSomeFileItems()` pump `AudioEngine::routineWithClusterLoading()`
/// between chunks (`file_catalogue.cpp:58 emptyFileItems`, `:74 deleteSomeFileItems`) -- they can be
/// erasing thousands of entries, and the
/// audio must keep running. This spec process never boots the audio engine: `AudioEngine::init()`
/// (`processing/engines/audio_engine.cpp:226`) builds a preview `SoundDrum` off `currentSong` and
/// only then sets `inputRingPos` (`:246`), so an un-booted process walks into `fillInputRing`
/// (`:179`) with a cursor of 0 and segfaults on the first non-empty erase.
///
/// `audioRoutineLocked` is the engine's *own* answer to "do not run the routine right now"
/// (`:1122` -- `routine()` returns immediately while it is set, and `runRoutine()` reaches
/// `routine()` directly here because no scheduler task is registered). Holding it makes every pump
/// call a documented no-op. It changes nothing about the catalogue: the pump is a yield point, not
/// part of the listing, sort, search or cull contract, and none of the state asserted below is
/// touched by it.
struct QuietAudio {
	bool saved = AudioEngine::audioRoutineLocked;
	QuietAudio() { AudioEngine::audioRoutineLocked = true; }
	~QuietAudio() { AudioEngine::audioRoutineLocked = saved; }
	QuietAudio(const QuietAudio&) = delete;
	QuietAudio& operator=(const QuietAudio&) = delete;
};

/// @brief Leaves the catalogue empty and its window counters zeroed on both sides of an example.
///
/// The catalogue is process-global static state (that is exactly the shape this plan is moving),
/// and one spec binary runs many examples in one process, so an example that left entries behind
/// would decide the next one's result by link order.
struct CleanCatalogue {
	/// Declared first, so it is latched before this guard's own body erases anything and released
	/// only after the body has run on the way out.
	QuietAudio quiet;

	CleanCatalogue() { reset(); }
	~CleanCatalogue() { reset(); }
	static void reset() {
		Browser::emptyFileItems();
		Browser::numFileItemsDeletedAtStart = 0;
		Browser::numFileItemsDeletedAtEnd = 0;
		Browser::firstFileItemRemaining.clear();
		Browser::lastFileItemRemaining.clear();
		Browser::filenameToStartSearchAt = nullptr;
		Browser::currentDir.clear();
	}
	CleanCatalogue(const CleanCatalogue&) = delete;
	CleanCatalogue& operator=(const CleanCatalogue&) = delete;
};

/// @brief The smallest concrete `Browser`: `getCurrentFilePath()` is the class's only pure virtual.
///
/// `readFileItemsForFolder`, `sortFileItems`, `getNewFileItem` and `cullSomeFileItems` are
/// *instance* members of `Browser` -- not because they touch any instance state beyond
/// `shouldInterpretNoteNamesForThisBrowser` (`browser.h:250`, protected), but because that one flag
/// is per-browser. This subclass exists to supply it, and nothing else.
class SpecBrowser : public Browser {
public:
	std::string getCurrentFilePath() override { return {}; }
	// The two other pure virtuals `Browser` inherits and does not implement: `UI::getUIType()`
	// (`gui/ui/ui.h:164`) and `QwertyUI::enterKeyPress()` (`gui/ui/qwerty_ui.h:45`). Nothing below
	// drives either -- the catalogue does not draw and does not take keys.
	UIType getUIType() override { return UIType::LOAD_SONG; }
	void enterKeyPress() override {}
	void interpretNoteNames(bool on) { shouldInterpretNoteNamesForThisBrowser = on; }
};

/// @brief The catalogue's contents as one comma-joined string, so an order mismatch prints as an
///        order rather than as a container diff.
std::string catalogue_order() {
	std::string out;
	for (const FileItem& item : Browser::fileItems) {
		if (!out.empty()) {
			out += ",";
		}
		out += item.displayName();
	}
	return out;
}

/// @brief Fill the catalogue with @p names, in the order given, without going through
///        `getNewFileItem()` (which would cull as it went).
void seed(const std::vector<std::string>& names, bool folders = false) {
	Browser::emptyFileItems();
	for (const std::string& name : names) {
		FileItem item;
		item.filename = name;
		item.isFolder = folders;
		Browser::fileItems.push_back(std::move(item));
	}
}

/// @brief Sort the catalogue with `strcmpspecial` under the caller's own note-name flags.
///
/// `Browser::sortFileItems()` forces `octaveStartsFromA = false`
/// (`file_catalogue.cpp:340 sortFileItems`), so the
/// `octaveStartsFromA = true` search example -- which needs a list sorted by the comparator it will
/// then binary-search under -- has to sort here instead. Same comparator, same projection as
/// `sortFileItems()`.
void sort_under(bool interpretNoteNames, bool octaveFromA) {
	shouldInterpretNoteNames = interpretNoteNames;
	octaveStartsFromA = octaveFromA;
	std::sort(Browser::fileItems.begin(), Browser::fileItems.end(), [](FileItem const& a, FileItem const& b) {
		return strcmpspecial(a.displayName().c_str(), b.displayName().c_str()) < 0;
	});
}

/// The seven note-named entries the sort/search examples run over. Deliberately not in sorted order
/// here: every example sorts before it asserts.
const std::vector<std::string> kNotes{"G3.WAV", "C4.WAV", "A3.WAV", "F3.WAV", "C3.WAV", "B3.WAV", "D3.WAV"};

/// Twenty entries whose `strcmpspecial` order is their lexical order, for the cull examples.
std::vector<std::string> twenty() {
	std::vector<std::string> out;
	for (int32_t i = 0; i < 20; i++) {
		out.push_back("A" + std::string(1, static_cast<char>('0' + i / 10))
		              + std::string(1, static_cast<char>('0' + i % 10)) + ".XML");
	}
	return out;
}

const std::string kFixtureCard = std::string(DELUGE_REPO_ROOT) + "/tests/fixtures/card";

} // namespace

// clang-format off
describe file_catalogue("the browser's file catalogue", $ {

	// ---- listing: the entries a folder yields, and their order -------------------------------

	it("lists the fixture card's root as its five folders, sorted", _ {
		CleanCatalogue clean;
		SdRoot root(kFixtureCard);
		SpecBrowser browser;

		Browser::currentDir.clear();
		Error error = browser.readFileItemsForFolder(nullptr, /*allowFolders=*/true, allowedFileExtensionsXML,
		                                             /*filenameToStartAt=*/"", FILE_ITEMS_MAX_NUM_ELEMENTS);
		expect(static_cast<int32_t>(error)).to_equal(static_cast<int32_t>(Error::NONE));
		browser.sortFileItems();

		// Five directories; the `.keep` placeholders each holds are skipped by the dot-entry rule
		// (`file_catalogue.cpp:175 readFileItemsForFolder`, the `entry->name[0] == '.'` continue).
		expect(static_cast<int32_t>(Browser::fileItems.size())).to_equal(5);
		expect(catalogue_order()).to_equal(std::string("KITS,SAMPLES,SETTINGS,SONGS,SYNTHS"));
		expect(Browser::fileItems[0].isFolder).to_be_true();
	});

	it("lists only the extension-matching files when folders are disallowed", _ {
		CleanCatalogue clean;
		SdRoot root(kFixtureCard);
		SpecBrowser browser;

		Browser::currentDir = "SETTINGS";
		Error error = browser.readFileItemsForFolder(nullptr, /*allowFolders=*/false, allowedFileExtensionsXML,
		                                             /*filenameToStartAt=*/"", FILE_ITEMS_MAX_NUM_ELEMENTS);
		expect(static_cast<int32_t>(error)).to_equal(static_cast<int32_t>(Error::NONE));
		browser.sortFileItems();

		expect(static_cast<int32_t>(Browser::fileItems.size())).to_equal(1);
		expect(catalogue_order()).to_equal(std::string("MIDIFollow.XML"));
		expect(Browser::fileItems[0].isFolder).to_be_false();
	});

	it("reports a missing folder rather than listing anything", _ {
		CleanCatalogue clean;
		SdRoot root(kFixtureCard);
		SpecBrowser browser;

		Browser::currentDir = "NO_SUCH_FOLDER";
		Error error = browser.readFileItemsForFolder(nullptr, /*allowFolders=*/true, allowedFileExtensionsXML, "",
		                                             FILE_ITEMS_MAX_NUM_ELEMENTS);
		expect(static_cast<int32_t>(error) != static_cast<int32_t>(Error::NONE)).to_be_true();
		expect(static_cast<int32_t>(Browser::fileItems.size())).to_equal(0);
	});

	// ---- sort order ---------------------------------------------------------------------------

	it("sorts plainly when the browser does not interpret note names", _ {
		CleanCatalogue clean;
		SpecBrowser browser;
		browser.interpretNoteNames(false);
		seed(kNotes);
		browser.sortFileItems();
		expect(catalogue_order())
		    .to_equal(std::string("A3.WAV,B3.WAV,C3.WAV,C4.WAV,D3.WAV,F3.WAV,G3.WAV"));
	});

	it("sorts by note number when the browser does interpret note names", _ {
		CleanCatalogue clean;
		SpecBrowser browser;
		browser.interpretNoteNames(true);
		seed(kNotes);
		browser.sortFileItems();
		// sortFileItems() pins octaveStartsFromA = false, so the octave runs C..B.
		expect(catalogue_order())
		    .to_equal(std::string("C3.WAV,D3.WAV,F3.WAV,G3.WAV,A3.WAV,B3.WAV,C4.WAV"));
		expect(octaveStartsFromA).to_be_false();
	});

	// ---- the search contract ------------------------------------------------------------------

	it("finds an exact match, and reports the insertion point for a miss, with note names off", _ {
		CleanCatalogue clean;
		seed(kNotes);
		sort_under(/*interpretNoteNames=*/false, /*octaveFromA=*/false);
		// A3,B3,C3,C4,D3,F3,G3

		bool found = false;
		expect(Browser::searchFileItems("C4.WAV", &found)).to_equal(3);
		expect(found).to_be_true();

		expect(Browser::searchFileItems("E3.WAV", &found)).to_equal(5); // between D3 and F3
		expect(found).to_be_false();

		expect(Browser::searchFileItems("0.WAV", &found)).to_equal(0); // before everything
		expect(found).to_be_false();
		expect(Browser::searchFileItems("Z.WAV", &found)).to_equal(7); // past the end
		expect(found).to_be_false();
	});

	it("searches by note number with note names on and the octave starting from C", _ {
		CleanCatalogue clean;
		seed(kNotes);
		sort_under(/*interpretNoteNames=*/true, /*octaveFromA=*/false);
		// C3,D3,F3,G3,A3,B3,C4

		bool found = false;
		expect(Browser::searchFileItems("A3.WAV", &found)).to_equal(4);
		expect(found).to_be_true();

		expect(Browser::searchFileItems("E3.WAV", &found)).to_equal(2); // between D3 and F3
		expect(found).to_be_false();
	});

	it("searches by note number with note names on and the octave starting from A", _ {
		CleanCatalogue clean;
		seed(kNotes);
		sort_under(/*interpretNoteNames=*/true, /*octaveFromA=*/true);
		// A3,B3,C3,D3,F3,G3,C4

		bool found = false;
		expect(Browser::searchFileItems("C3.WAV", &found)).to_equal(2);
		expect(found).to_be_true();

		expect(Browser::searchFileItems("E3.WAV", &found)).to_equal(4); // between D3 and F3
		expect(found).to_be_false();

		// Restore the process-wide comparator flags this example moved.
		shouldInterpretNoteNames = false;
		octaveStartsFromA = false;
	});

	// ---- culling ------------------------------------------------------------------------------

	it("culls from each end in turn, counting the losses and naming the survivors", _ {
		CleanCatalogue clean;
		SpecBrowser browser;
		browser.interpretNoteNames(false);
		seed(twenty()); // A00.XML .. A19.XML

		// Left cull: keeps maxNumFileItemsNow >> 1 == 10, so ten go from the start.
		catalogSearchDirection = CATALOG_SEARCH_LEFT;
		maxNumFileItemsNow = 20;
		browser.cullSomeFileItems();

		expect(static_cast<int32_t>(Browser::fileItems.size())).to_equal(10);
		expect(Browser::numFileItemsDeletedAtStart).to_equal(10);
		expect(Browser::numFileItemsDeletedAtEnd).to_equal(0);
		expect(Browser::firstFileItemRemaining).to_equal(std::string("A10.XML"));
		expect(Browser::lastFileItemRemaining).to_equal(std::string(""));
		expect(catalogue_order())
		    .to_equal(std::string("A10.XML,A11.XML,A12.XML,A13.XML,A14.XML,A15.XML,A16.XML,A17.XML,A18.XML,A19.XML"));

		// Right cull on what is left: keeps 10 >> 1 == 5, so five go from the end.
		catalogSearchDirection = CATALOG_SEARCH_RIGHT;
		maxNumFileItemsNow = 10;
		browser.cullSomeFileItems();

		expect(static_cast<int32_t>(Browser::fileItems.size())).to_equal(5);
		expect(Browser::numFileItemsDeletedAtStart).to_equal(10);
		expect(Browser::numFileItemsDeletedAtEnd).to_equal(5);
		// The two survivors that bracket the window that is left. These are owning copies on
		// purpose (`file_catalogue.h:109 firstFileItemRemaining`): they name entries the very next erase
		// destroys.
		expect(Browser::firstFileItemRemaining).to_equal(std::string("A10.XML"));
		expect(Browser::lastFileItemRemaining).to_equal(std::string("A14.XML"));
		expect(catalogue_order()).to_equal(std::string("A10.XML,A11.XML,A12.XML,A13.XML,A14.XML"));
	});

	it("culls towards the search term when searching both directions", _ {
		CleanCatalogue clean;
		SpecBrowser browser;
		browser.interpretNoteNames(false);
		seed(twenty());

		catalogSearchDirection = CATALOG_SEARCH_BOTH;
		maxNumFileItemsNow = 20;
		Browser::filenameToStartSearchAt = "A15.XML";
		browser.cullSomeFileItems();

		// The search term sits at index 15, i.e. in the second half, so the cull takes half of what
		// lies to its left: 15 >> 1 == 7 entries off the start.
		expect(static_cast<int32_t>(Browser::fileItems.size())).to_equal(13);
		expect(Browser::numFileItemsDeletedAtStart).to_equal(7);
		expect(Browser::numFileItemsDeletedAtEnd).to_equal(0);
		expect(Browser::firstFileItemRemaining).to_equal(std::string("A07.XML"));
		expect(catalogue_order().substr(0, 7)).to_equal(std::string("A07.XML"));
	});

	it("survives a cull whose deletions would reallocate the vector out from under a name", _ {
		// The regression this whole family of owning copies exists for: firstFileItemRemaining used
		// to be a `char const*` into a FileItem inside `fileItems`, which the cull then erased. If
		// that ever comes back, this reads freed memory -- under the sim's sanitizers, loudly.
		CleanCatalogue clean;
		SpecBrowser browser;
		browser.interpretNoteNames(false);
		seed(twenty());
		catalogSearchDirection = CATALOG_SEARCH_LEFT;
		maxNumFileItemsNow = 20;
		browser.cullSomeFileItems();

		const std::string survivor = Browser::firstFileItemRemaining;
		Browser::emptyFileItems();        // frees every buffer the names could have aliased
		Browser::fileItems.shrink_to_fit();
		expect(Browser::firstFileItemRemaining).to_equal(survivor);
		expect(Browser::firstFileItemRemaining).to_equal(std::string("A10.XML"));
	});

	// ---- duplicate/folder filtering -----------------------------------------------------------

	it("drops a folder that shares its name with a file beside it", _ {
		CleanCatalogue clean;
		SpecBrowser browser;
		browser.interpretNoteNames(false);

		Browser::emptyFileItems();
		auto push = [](const char* name, bool isFolder) {
			FileItem item;
			item.filename = name;
			item.isFolder = isFolder;
			Browser::fileItems.push_back(std::move(item));
		};
		push("KEYS", true);
		push("KEYS.XML", false);
		push("PADS.XML", false);
		browser.sortFileItems();
		expect(catalogue_order()).to_equal(std::string("KEYS,KEYS.XML,PADS.XML"));

		Browser::deleteFolderAndDuplicateItems();
		expect(static_cast<int32_t>(Browser::fileItems.size())).to_equal(2);
		expect(catalogue_order()).to_equal(std::string("KEYS.XML,PADS.XML"));
	});

	it("also trims one entry from each end that a cull had already made unsafe", _ {
		CleanCatalogue clean;
		SpecBrowser browser;
		browser.interpretNoteNames(false);
		seed({"A.XML", "B.XML", "C.XML", "D.XML"});
		Browser::firstFileItemRemaining = "A.XML";
		Browser::lastFileItemRemaining = "D.XML";

		Browser::deleteFolderAndDuplicateItems();
		// Nothing here is a duplicate, so the only losses are the two edge entries the cull
		// boundaries make untrustworthy (`file_catalogue.cpp:258 deleteFolderAndDuplicateItems`, tail).
		expect(catalogue_order()).to_equal(std::string("B.XML,C.XML"));
	});
});
// clang-format on

DELUGE_SPEC(file_catalogue_spec, file_catalogue)
