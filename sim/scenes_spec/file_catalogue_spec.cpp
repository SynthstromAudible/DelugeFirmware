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
/// **Known gap.** The listing->cull composite is covered only in its narrowest deterministic form
/// (three files, a cap of two, one cull, and an assertion that survives any `readdir` order -- see
/// "lists under the browser's note-name flag when the folder overflows mid-read"). What is NOT
/// covered is a large folder culled repeatedly mid-read, where the surviving window depends on the
/// order the host's `readdir` hands entries over and so cannot be asserted as a set at all. Tasks 2
/// and 3 drive real folders through the file browser and are where that shows up; closing it needs
/// a listing source with a fixed enumeration order, not another example here.
///
/// **Card hygiene.** Nothing here writes to the card. The two listing examples point
/// `$DELUGE_SD_ROOT` at the committed fixture (`tests/fixtures/card`) explicitly rather than
/// relying on the aggregate `scenes_spec` ctest row's env -- the per-spec row
/// `create_specs_driver` registers does *not* set it -- and only ever `opendir`/`readdir` it, so
/// `git status --porcelain tests/fixtures` stays clean. `SdRoot` is the same
/// save/point/restore shape as `NoSdRoot` (`context_menu_scene_spec.cpp`) and `ErasedFlash`
/// (`support/host_flash.h`).

#include "gui/ui/browser/browser.h"
#include "scenes/browser/listing.h"

#include "definitions_cxx.hpp"
#include "processing/engines/audio_engine.h"
#include "storage/file_item.h"
#include "util/name_compare.h"

#include "cppspec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "spec/support/spec_main.h"
#include "support/card_access.h"

// The two file-scope globals `readFileItemsForFolder` sets and `getNewFileItem`/`cullSomeFileItems`
// read (`storage/file_catalogue.cpp:80 maxNumFileItemsNow`, `:81 catalogSearchDirection`; they were
// `browser.cpp`'s before the extraction). They
// have external linkage but no header declares them -- the same situation `menu_ranges_spec.cpp`
// records for the file-scope menu globals -- so this spec declares them to drive a cull directly,
// deterministically, instead of hoping a mid-listing cull lands where readdir order happens to put
// it.
extern int32_t maxNumFileItemsNow;
extern int32_t catalogSearchDirection;

namespace {

/// `SdRoot` and `QuietAudio` moved to `support/card_access.h` in task 2 of this plan: `QuietAudio`
/// in particular is the disarm for a landmine that *segfaults* an un-booted spec process rather
/// than failing it, and every later spec in this plan drives a listing too. Their documentation --
/// including why the shipped seam must NOT hold that latch -- moved with them, unchanged.
using deluge::scenes::spec::QuietAudio;
using deluge::scenes::spec::SdRoot;

/// @brief Leaves the catalogue empty, its window counters zeroed and the name comparator's flags
///        back at their defaults, on both sides of an example.
///
/// The catalogue is process-global static state (that is exactly the shape this plan is moving),
/// and one spec binary runs many examples in one process, so an example that left entries behind
/// would decide the next one's result by link order.
///
/// `shouldInterpretNoteNames` and `octaveStartsFromA` (`util/name_compare.cpp:85`, `:87`) are
/// process-global for the same reason and leak the same way -- and worse, silently: they change
/// what `strcmpspecial` *means*, so a stale `true` turns a later example's expected sort order into
/// a different one rather than into an obvious crash. Nearly every example here moves them (every
/// `sortFileItems` and every `CATALOG_SEARCH_BOTH` cull writes them), so they belong in the
/// fixture, not in an ad-hoc restore at the bottom of whichever example happened to notice.
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
		// The catalogue's single-flight claim is process-global too, and a leaked one would refuse
		// every listing in every later example rather than failing this one.
		deluge::storage::FileCatalogue::release(deluge::storage::FileCatalogue::claimant());
		shouldInterpretNoteNames = false;
		octaveStartsFromA = false;
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

	/// @brief `Browser::beginListing` (protected), for the shared-claim examples.
	///
	/// Only ever called below on a path that is refused *at the claim*, which `beginListing` tests
	/// before it touches anything else -- `display` is null in this process
	/// (`hid/display/display.cpp:4`), so the accepted path's `displayLoadingAnimationText` would
	/// dereference null. That ordering is not incidental to the test; it is the property that lets
	/// a refused listing leave the UI untouched.
	/// @return What `beginListing` returned.
	bool tryBeginOpenListing() { return beginListing({.action = ListingAction::Open, .direction = 0}); }
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
/// (`file_catalogue.cpp:367 sortFileItems`), so the
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

/// @name Pinning the one parameter the extraction invented.
///
/// `interpretNoteNames` is new: in `Browser` the listing read the per-browser
/// `shouldInterpretNoteNamesForThisBrowser` directly, and on `FileCatalogue` each of the four
/// operations that needs it takes it as an argument. Four `Browser` members now have to *thread* it
/// (`browser.cpp`: `readFileItemsForFolder`, `sortFileItems`, `getNewFileItem`,
/// `cullSomeFileItems`), and a delegate that passed a hardcoded `false` -- or the wrong browser's
/// flag -- would be a silent behaviour change on a real card full of note-named samples. So every
/// one of the four is driven with the flag set BOTH ways, over inputs whose two answers differ.
/// @{

/// Eight note names whose plain order and whose note-number order disagree from the fifth entry on,
/// so a cull that keeps the back half keeps a different half under each flag value.
///
///  - plain:       `A3, A4, B3, C3, C4, D3, F3, G3`
///  - note number: `C3, D3, F3, G3, A3, B3, C4, A4`
const std::vector<std::string> kEightNotes{"G3.WAV", "C4.WAV", "A3.WAV", "F3.WAV",
                                           "C3.WAV", "B3.WAV", "D3.WAV", "A4.WAV"};

/// @brief Three note names whose two orders are exact reverses of one another.
///
///  - plain:       `A4.XML < B3.XML < C3.XML`
///  - note number: `C3.XML (108) < B3.XML (126) < A4.XML (159)`
///
/// `.XML` rather than `.WAV` because these are read through a real listing, and
/// `allowedFileExtensionsXML` is what that listing filters on.
///
/// Exact reversal is what makes the mid-listing cull example below assert something the host's
/// `readdir` order cannot perturb: *whichever* two of the three are in hand when the cull fires,
/// the entry it keeps under one flag value is the one it drops under the other.
const std::array<const char*, 3> kReversingTrio{"A4.XML", "B3.XML", "C3.XML"};

/// @brief A scratch card root holding `kReversingTrio`, created and removed with the guard.
///
/// Not `tests/fixtures/card`: that fixture is version-controlled and this needs to *write* files.
/// Under the host's temp directory, removed on the way out, so `git status --porcelain
/// tests/fixtures` stays clean and nothing is left behind for the next example.
struct TrioCard {
	std::filesystem::path dir;
	SdRoot root;

	static std::filesystem::path make() {
		std::filesystem::path d = std::filesystem::temp_directory_path() / "deluge_file_catalogue_spec";
		std::filesystem::remove_all(d);
		std::filesystem::create_directories(d);
		for (const char* name : kReversingTrio) {
			std::ofstream(d / name) << "<x/>";
		}
		return d;
	}

	TrioCard() : dir(make()), root(dir.string()) {}
	~TrioCard() { std::filesystem::remove_all(dir); }
	TrioCard(const TrioCard&) = delete;
	TrioCard& operator=(const TrioCard&) = delete;
};
/// @}

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
		// (`file_catalogue.cpp:202 readFileItemsForFolder`, the `entry->name[0] == '.'` continue).
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
		// The comparator flags this example moved are restored by CleanCatalogue, not here.
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

	// ---- the interpretNoteNames parameter, on each of the four paths that thread it ------------

	it("culls under the browser's note-name flag, not a hardcoded one", _ {
		// Browser::cullSomeFileItems -> FileCatalogue::cullSomeFileItems(interpretNoteNames).
		// A left cull keeps the back half of the SORTED list, and the two flag values sort these
		// eight differently from the fifth entry on, so they keep different halves.
		auto cull_left = [](bool interpretNoteNames) {
			SpecBrowser browser;
			browser.interpretNoteNames(interpretNoteNames);
			seed(kEightNotes);
			catalogSearchDirection = CATALOG_SEARCH_LEFT;
			maxNumFileItemsNow = 8;
			browser.cullSomeFileItems();
			return std::pair{catalogue_order(), Browser::firstFileItemRemaining};
		};

		CleanCatalogue clean;
		auto plain = cull_left(false);
		CleanCatalogue::reset();
		auto notes = cull_left(true);

		expect(plain.first).to_equal(std::string("C4.WAV,D3.WAV,F3.WAV,G3.WAV"));
		expect(plain.second).to_equal(std::string("C4.WAV"));
		expect(notes.first).to_equal(std::string("A3.WAV,B3.WAV,C4.WAV,A4.WAV"));
		expect(notes.second).to_equal(std::string("A3.WAV"));
	});

	it("picks the cull's side from the search term under the browser's note-name flag", _ {
		// The CATALOG_SEARCH_BOTH arm reads the flag a second time, for the searchFileItems() call
		// that decides WHICH END to cull. Same eight entries, same search term: with note names off
		// "A3.WAV" sorts first, so the cull takes the right-hand half; with them on it sorts into
		// the middle, so the cull takes a slice off the left instead. A hardcoded flag cannot
		// produce both.
		auto cull_both = [](bool interpretNoteNames) {
			SpecBrowser browser;
			browser.interpretNoteNames(interpretNoteNames);
			seed(kEightNotes);
			catalogSearchDirection = CATALOG_SEARCH_BOTH;
			maxNumFileItemsNow = 8;
			Browser::filenameToStartSearchAt = "A3.WAV";
			browser.cullSomeFileItems();
			return std::tuple{catalogue_order(), Browser::numFileItemsDeletedAtStart,
			                  Browser::numFileItemsDeletedAtEnd};
		};

		CleanCatalogue clean;
		auto plain = cull_both(false);
		CleanCatalogue::reset();
		auto notes = cull_both(true);

		// Plain: "A3.WAV" is at index 0, so the cull works from the right.
		expect(std::get<0>(plain)).to_equal(std::string("A3.WAV,A4.WAV,B3.WAV,C3.WAV"));
		expect(std::get<1>(plain)).to_equal(0);
		expect(std::get<2>(plain)).to_equal(4);

		// Note names: "A3.WAV" is at index 4, so the cull works from the left, and takes half of
		// what lies left of it (4 >> 1 == 2) rather than the full four.
		expect(std::get<0>(notes)).to_equal(std::string("F3.WAV,G3.WAV,A3.WAV,B3.WAV,C4.WAV,A4.WAV"));
		expect(std::get<1>(notes)).to_equal(2);
		expect(std::get<2>(notes)).to_equal(0);
	});

	it("makes room for a new entry under the browser's note-name flag", _ {
		// Browser::getNewFileItem -> FileCatalogue::getNewFileItem(interpretNoteNames), which
		// forwards it to the cull it runs when the catalogue is already at its cap.
		auto grow_at_cap = [](bool interpretNoteNames) {
			SpecBrowser browser;
			browser.interpretNoteNames(interpretNoteNames);
			seed(kEightNotes);
			catalogSearchDirection = CATALOG_SEARCH_LEFT;
			maxNumFileItemsNow = 8;
			FileItem* fresh = browser.getNewFileItem();
			return std::pair{fresh != nullptr, Browser::firstFileItemRemaining};
		};

		CleanCatalogue clean;
		auto plain = grow_at_cap(false);
		CleanCatalogue::reset();
		auto notes = grow_at_cap(true);

		expect(plain.first).to_be_true();
		expect(notes.first).to_be_true();
		// Four survivors plus the empty entry getNewFileItem appended.
		expect(plain.second).to_equal(std::string("C4.WAV"));
		expect(notes.second).to_equal(std::string("A3.WAV"));
	});

	it("lists under the browser's note-name flag when the folder overflows mid-read", _ {
		// Browser::readFileItemsForFolder -> FileCatalogue::readFileItemsForFolder(...,
		// interpretNoteNames), whose only route to an observable difference is the cull
		// getNewFileItem runs on the entry that first exceeds the cap. Three files, a cap of two:
		// the third read triggers exactly one left cull, which drops one of the two entries in
		// hand.
		//
		// The host's readdir decides WHICH two are in hand, so nothing here asserts the final set.
		// It does not have to: kReversingTrio's two orders are exact reverses, so for any pair the
		// cull keeps the alphabetically LATER one with note names off and the EARLIER one with them
		// on. That relation holds for all six readdir permutations, and a hardcoded flag collapses
		// it -- both runs would then name the same survivor.
		auto list_over_cap = [](bool interpretNoteNames) {
			SpecBrowser browser;
			browser.interpretNoteNames(interpretNoteNames);
			Browser::currentDir.clear();
			Error error = browser.readFileItemsForFolder(nullptr, /*allowFolders=*/false, allowedFileExtensionsXML,
			                                             /*filenameToStartAt=*/"", /*newMaxNumFileItems=*/2,
			                                             CATALOG_SEARCH_LEFT);
			return std::tuple{error, Browser::firstFileItemRemaining, static_cast<int32_t>(Browser::fileItems.size()),
			                  Browser::numFileItemsDeletedAtStart};
		};

		CleanCatalogue clean;
		TrioCard card;
		auto plain = list_over_cap(false);
		CleanCatalogue::reset();
		auto notes = list_over_cap(true);

		expect(static_cast<int32_t>(std::get<0>(plain))).to_equal(static_cast<int32_t>(Error::NONE));
		expect(static_cast<int32_t>(std::get<0>(notes))).to_equal(static_cast<int32_t>(Error::NONE));
		// Exactly one cull, of exactly one entry, on each run: 2 - (2 >> 1) == 1.
		expect(std::get<3>(plain)).to_equal(1);
		expect(std::get<3>(notes)).to_equal(1);
		expect(std::get<2>(plain)).to_equal(2);
		expect(std::get<2>(notes)).to_equal(2);

		// The survivor the cull named. Plain keeps the alphabetically later of the pair it held,
		// note names the earlier -- so the two runs never agree, whatever readdir handed over.
		expect(std::get<1>(plain).empty()).to_be_false();
		expect(std::get<1>(notes).empty()).to_be_false();
		expect(std::get<1>(notes) < std::get<1>(plain)).to_be_true();
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

	// ---- the single-flight claim: legacy and the scenes seam over one catalogue ---------------
	//
	// Added in task 2 of this plan. `Browser::listingInProgress_` only ever guarded Browser against
	// Browser; `deluge::scenes::browser::Listing` drives these same statics from outside the class,
	// so for as long as the legacy UI and the scenes stack coexist the two were mutually invisible
	// over one `fileItems`. The guard now lives on the catalogue and both dispatch paths claim it.

	it("admits exactly one claimant, and only the claimant can release", _ {
		CleanCatalogue clean;
		const int a = 0;
		const int b = 0;

		expect(deluge::storage::FileCatalogue::claim(&a)).to_be_true();
		expect(deluge::storage::FileCatalogue::claimant() == &a).to_be_true();
		// A second claim from anyone -- including `a` itself -- fails, with no partial claim.
		expect(deluge::storage::FileCatalogue::claim(&b)).to_be_false();
		expect(deluge::storage::FileCatalogue::claim(&a)).to_be_false();
		expect(deluge::storage::FileCatalogue::claimant() == &a).to_be_true();

		// A non-claimant's release is a no-op, which is what lets a caller release
		// unconditionally without first working out whether it was holding anything.
		deluge::storage::FileCatalogue::release(&b);
		expect(deluge::storage::FileCatalogue::claimant() == &a).to_be_true();

		deluge::storage::FileCatalogue::release(&a);
		expect(deluge::storage::FileCatalogue::claimant() == nullptr).to_be_true();
		expect(deluge::storage::FileCatalogue::claim(&b)).to_be_true();
		deluge::storage::FileCatalogue::release(&b);
	});

	it("refuses a legacy browser listing while the scenes seam still holds the catalogue", _ {
		CleanCatalogue clean;
		SdRoot root(kFixtureCard);
		SpecBrowser browser;

		// The shipped dispatcher, deliberately: on the host `Owner::run` is inline
		// (`storage/owner.h:55-59`), so this listing has already RUN by the time `request()`
		// returns -- and the claim is still held, because the seam releases on collect, not on
		// completion. That window is exactly the one legacy's own completion-release would not
		// have covered, and it is where the entries the seam's caller has not read yet live.
		deluge::scenes::browser::Listing seam;
		expect(static_cast<int>(seam.request({.action = deluge::scenes::browser::ListingAction::Open})))
		    .to_equal(static_cast<int>(deluge::scenes::browser::RequestOutcome::Accepted));
		expect(seam.has_result()).to_be_true();
		expect(deluge::storage::FileCatalogue::claimant() == &seam).to_be_true();

		// THE CROSS-SIDE CASE. Before the shared claim this returned true and the legacy listing
		// went on to re-read the folder into the same vector the seam's caller was about to read.
		expect(browser.tryBeginOpenListing()).to_be_false();
		expect(Browser::isListingInProgress()).to_be_false();
		// Refused before it touched anything: the catalogue still holds the seam's listing.
		expect(static_cast<int32_t>(Browser::fileItems.size())).to_equal(5);

		auto result = seam.take_result();
		expect(result.has_value()).to_be_true();
		expect(deluge::storage::FileCatalogue::claimant() == nullptr).to_be_true();
	});

	it("refuses the scenes seam while a legacy listing holds the catalogue", _ {
		CleanCatalogue clean;
		SdRoot root(kFixtureCard);

		// The other direction. A legacy listing's claim token is the `Browser*` itself
		// (`browser.cpp:106`), and on this host `Owner::run` is inline, so a real legacy listing
		// never survives across a statement boundary here to be observed mid-flight -- the claim
		// below stands in for it, and is indistinguishable to the seam, which only ever asks
		// whether the catalogue is free.
		SpecBrowser browser;
		expect(deluge::storage::FileCatalogue::claim(&browser)).to_be_true();

		deluge::scenes::browser::Listing seam;
		expect(static_cast<int>(seam.request({.action = deluge::scenes::browser::ListingAction::Open})))
		    .to_equal(static_cast<int>(deluge::scenes::browser::RequestOutcome::Busy));
		expect(seam.has_result()).to_be_false();

		deluge::storage::FileCatalogue::release(&browser);
		expect(static_cast<int>(seam.request({.action = deluge::scenes::browser::ListingAction::Open})))
		    .to_equal(static_cast<int>(deluge::scenes::browser::RequestOutcome::Accepted));
		expect(seam.take_result().has_value()).to_be_true();
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
		// boundaries make untrustworthy (`file_catalogue.cpp:285 deleteFolderAndDuplicateItems`, tail).
		expect(catalogue_order()).to_equal(std::string("B.XML,C.XML"));
	});
});
// clang-format on

DELUGE_SPEC(file_catalogue_spec, file_catalogue)
