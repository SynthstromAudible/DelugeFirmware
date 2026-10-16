/// @file
/// @brief Characterisation of `deluge::storage::FileCatalogue`, the scenes' listing backend: the
///        listing, sort, search and cull machinery that was extracted from `Browser`.
///
/// Every number asserted below (entry counts, orders, indices, cull windows) is what legacy's
/// `Browser` listing produces for the same folder and flags, so the catalogue lists, sorts, searches
/// and culls exactly as the browser it serves expects. The examples drive the catalogue through
/// `SpecBrowser` (`support/catalogue_rig.h`), which carries the note-name flag a browser passes it.
///
/// **Known gap.** The listing->cull composite is covered only in its narrowest deterministic form
/// (three files, a cap of two, one cull -- `file_catalogue_trio_spec.cpp`, which needs its own card).
/// What is NOT covered is a large folder culled repeatedly mid-read, where the surviving window
/// depends on the order the host's `readdir` hands entries over and so cannot be asserted as a set
/// at all; closing it needs a listing source with a fixed enumeration order, not another example.
///
/// **Card.** The listing examples run on the fixture card (`tests/fixtures/card`), installed in
/// memory before boot, so nothing here can write to it.

#include "scenes/browser/listing.h"

#include "definitions_cxx.hpp"
#include "processing/engines/audio_engine.h"
#include "storage/file_item.h"
#include "util/name_compare.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the headers this pulls in.
#include "support/catalogue_rig.h"

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
using namespace deluge::scenes::spec::catalogue;
using deluge::scenes::spec::SdRoot;
using deluge::storage::FileCatalogue;

/// @brief The catalogue's contents as one comma-joined string, so an order mismatch prints as an
///        order rather than as a container diff.
std::string catalogue_order() {
	std::string out;
	for (const FileItem& item : FileCatalogue::fileItems) {
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
	FileCatalogue::emptyFileItems();
	for (const std::string& name : names) {
		FileItem item;
		item.filename = name;
		item.isFolder = folders;
		FileCatalogue::fileItems.push_back(std::move(item));
	}
}

/// @brief Sort the catalogue with `strcmpspecial` under the caller's own note-name flags.
///
/// `Browser::sortFileItems()` forces `octaveStartsFromA = false`
/// (`file_catalogue.cpp:398 sortFileItems`), so the
/// `octaveStartsFromA = true` search example -- which needs a list sorted by the comparator it will
/// then binary-search under -- has to sort here instead. Same comparator, same projection as
/// `sortFileItems()`.
void sort_under(bool interpretNoteNames, bool octaveFromA) {
	shouldInterpretNoteNames = interpretNoteNames;
	octaveStartsFromA = octaveFromA;
	std::sort(FileCatalogue::fileItems.begin(), FileCatalogue::fileItems.end(),
	          [](FileItem const& a, FileItem const& b) {
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

/// @}

} // namespace

// clang-format off
describe file_catalogue("the browser's file catalogue", $ {

	// ---- listing: the entries a folder yields, and their order -------------------------------

	it("lists the fixture card's root as its five folders, sorted", _ {
		CleanCatalogue clean;
		SdRoot root(kFixtureCard);
		SpecBrowser browser;

		FileCatalogue::currentDir.clear();
		Error error = browser.readFileItemsForFolder(nullptr, /*allowFolders=*/true, allowedFileExtensionsXML,
		                                             /*filenameToStartAt=*/"", FILE_ITEMS_MAX_NUM_ELEMENTS);
		expect(static_cast<int32_t>(error)).to_equal(static_cast<int32_t>(Error::NONE));
		browser.sortFileItems();

		// Five directories; the `.keep` placeholders each holds are skipped by the dot-entry rule
		// (`file_catalogue.cpp:202 readFileItemsForFolder`, the `entry->name[0] == '.'` continue).
		expect(static_cast<int32_t>(FileCatalogue::fileItems.size())).to_equal(5);
		expect(catalogue_order()).to_equal(std::string("KITS,SAMPLES,SETTINGS,SONGS,SYNTHS"));
		expect(FileCatalogue::fileItems[0].isFolder).to_be_true();
	});

	it("lists only the extension-matching files when folders are disallowed", _ {
		CleanCatalogue clean;
		SdRoot root(kFixtureCard);
		SpecBrowser browser;

		FileCatalogue::currentDir = "SETTINGS";
		Error error = browser.readFileItemsForFolder(nullptr, /*allowFolders=*/false, allowedFileExtensionsXML,
		                                             /*filenameToStartAt=*/"", FILE_ITEMS_MAX_NUM_ELEMENTS);
		expect(static_cast<int32_t>(error)).to_equal(static_cast<int32_t>(Error::NONE));
		browser.sortFileItems();

		expect(static_cast<int32_t>(FileCatalogue::fileItems.size())).to_equal(1);
		expect(catalogue_order()).to_equal(std::string("MIDIFollow.XML"));
		expect(FileCatalogue::fileItems[0].isFolder).to_be_false();
	});

	it("reports a missing folder rather than listing anything", _ {
		CleanCatalogue clean;
		SdRoot root(kFixtureCard);
		SpecBrowser browser;

		FileCatalogue::currentDir = "NO_SUCH_FOLDER";
		Error error = browser.readFileItemsForFolder(nullptr, /*allowFolders=*/true, allowedFileExtensionsXML, "",
		                                             FILE_ITEMS_MAX_NUM_ELEMENTS);
		expect(static_cast<int32_t>(error) != static_cast<int32_t>(Error::NONE)).to_be_true();
		expect(static_cast<int32_t>(FileCatalogue::fileItems.size())).to_equal(0);
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
		expect(FileCatalogue::searchFileItems("C4.WAV", &found)).to_equal(3);
		expect(found).to_be_true();

		expect(FileCatalogue::searchFileItems("E3.WAV", &found)).to_equal(5); // between D3 and F3
		expect(found).to_be_false();

		expect(FileCatalogue::searchFileItems("0.WAV", &found)).to_equal(0); // before everything
		expect(found).to_be_false();
		expect(FileCatalogue::searchFileItems("Z.WAV", &found)).to_equal(7); // past the end
		expect(found).to_be_false();
	});

	it("searches by note number with note names on and the octave starting from C", _ {
		CleanCatalogue clean;
		seed(kNotes);
		sort_under(/*interpretNoteNames=*/true, /*octaveFromA=*/false);
		// C3,D3,F3,G3,A3,B3,C4

		bool found = false;
		expect(FileCatalogue::searchFileItems("A3.WAV", &found)).to_equal(4);
		expect(found).to_be_true();

		expect(FileCatalogue::searchFileItems("E3.WAV", &found)).to_equal(2); // between D3 and F3
		expect(found).to_be_false();
	});

	it("searches by note number with note names on and the octave starting from A", _ {
		CleanCatalogue clean;
		seed(kNotes);
		sort_under(/*interpretNoteNames=*/true, /*octaveFromA=*/true);
		// A3,B3,C3,D3,F3,G3,C4

		bool found = false;
		expect(FileCatalogue::searchFileItems("C3.WAV", &found)).to_equal(2);
		expect(found).to_be_true();

		expect(FileCatalogue::searchFileItems("E3.WAV", &found)).to_equal(4); // between D3 and F3
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

		expect(static_cast<int32_t>(FileCatalogue::fileItems.size())).to_equal(10);
		expect(FileCatalogue::numFileItemsDeletedAtStart).to_equal(10);
		expect(FileCatalogue::numFileItemsDeletedAtEnd).to_equal(0);
		expect(FileCatalogue::firstFileItemRemaining).to_equal(std::string("A10.XML"));
		expect(FileCatalogue::lastFileItemRemaining).to_equal(std::string(""));
		expect(catalogue_order())
		    .to_equal(std::string("A10.XML,A11.XML,A12.XML,A13.XML,A14.XML,A15.XML,A16.XML,A17.XML,A18.XML,A19.XML"));

		// Right cull on what is left: keeps 10 >> 1 == 5, so five go from the end.
		catalogSearchDirection = CATALOG_SEARCH_RIGHT;
		maxNumFileItemsNow = 10;
		browser.cullSomeFileItems();

		expect(static_cast<int32_t>(FileCatalogue::fileItems.size())).to_equal(5);
		expect(FileCatalogue::numFileItemsDeletedAtStart).to_equal(10);
		expect(FileCatalogue::numFileItemsDeletedAtEnd).to_equal(5);
		// The two survivors that bracket the window that is left. These are owning copies on
		// purpose (`file_catalogue.h:109 firstFileItemRemaining`): they name entries the very next erase
		// destroys.
		expect(FileCatalogue::firstFileItemRemaining).to_equal(std::string("A10.XML"));
		expect(FileCatalogue::lastFileItemRemaining).to_equal(std::string("A14.XML"));
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
			return std::pair{catalogue_order(), FileCatalogue::firstFileItemRemaining};
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
			FileCatalogue::filenameToStartSearchAt = "A3.WAV";
			browser.cullSomeFileItems();
			return std::tuple{catalogue_order(), FileCatalogue::numFileItemsDeletedAtStart,
			                  FileCatalogue::numFileItemsDeletedAtEnd};
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
			return std::pair{fresh != nullptr, FileCatalogue::firstFileItemRemaining};
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

	it("culls towards the search term when searching both directions", _ {
		CleanCatalogue clean;
		SpecBrowser browser;
		browser.interpretNoteNames(false);
		seed(twenty());

		catalogSearchDirection = CATALOG_SEARCH_BOTH;
		maxNumFileItemsNow = 20;
		FileCatalogue::filenameToStartSearchAt = "A15.XML";
		browser.cullSomeFileItems();

		// The search term sits at index 15, i.e. in the second half, so the cull takes half of what
		// lies to its left: 15 >> 1 == 7 entries off the start.
		expect(static_cast<int32_t>(FileCatalogue::fileItems.size())).to_equal(13);
		expect(FileCatalogue::numFileItemsDeletedAtStart).to_equal(7);
		expect(FileCatalogue::numFileItemsDeletedAtEnd).to_equal(0);
		expect(FileCatalogue::firstFileItemRemaining).to_equal(std::string("A07.XML"));
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

		const std::string survivor = FileCatalogue::firstFileItemRemaining;
		FileCatalogue::emptyFileItems();        // frees every buffer the names could have aliased
		FileCatalogue::fileItems.shrink_to_fit();
		expect(FileCatalogue::firstFileItemRemaining).to_equal(survivor);
		expect(FileCatalogue::firstFileItemRemaining).to_equal(std::string("A10.XML"));
	});

	// ---- duplicate/folder filtering -----------------------------------------------------------

	it("drops a folder that shares its name with a file beside it", _ {
		CleanCatalogue clean;
		SpecBrowser browser;
		browser.interpretNoteNames(false);

		FileCatalogue::emptyFileItems();
		auto push = [](const char* name, bool isFolder) {
			FileItem item;
			item.filename = name;
			item.isFolder = isFolder;
			FileCatalogue::fileItems.push_back(std::move(item));
		};
		push("KEYS", true);
		push("KEYS.XML", false);
		push("PADS.XML", false);
		browser.sortFileItems();
		expect(catalogue_order()).to_equal(std::string("KEYS,KEYS.XML,PADS.XML"));

		FileCatalogue::deleteFolderAndDuplicateItems();
		expect(static_cast<int32_t>(FileCatalogue::fileItems.size())).to_equal(2);
		expect(catalogue_order()).to_equal(std::string("KEYS.XML,PADS.XML"));
	});

	// ---- the single-flight claim ---------------------------------------------------------------
	//
	// The catalogue is process-global and `deluge::scenes::browser::Listing` reads it after the
	// listing completes, so one claimant holds it from request to collect.

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

	it("refuses the scenes seam while another claimant holds the catalogue", _ {
		CleanCatalogue clean;
		SdRoot root(kFixtureCard);

		// The seam only ever asks whether the catalogue is free, so any other claimant stands in
		// for one still holding its listing.
		const int other = 0;
		expect(deluge::storage::FileCatalogue::claim(&other)).to_be_true();

		deluge::scenes::browser::Listing seam;
		expect(static_cast<int>(seam.request({.action = deluge::scenes::browser::ListingAction::Open})))
		    .to_equal(static_cast<int>(deluge::scenes::browser::RequestOutcome::Busy));
		expect(seam.has_result()).to_be_false();

		deluge::storage::FileCatalogue::release(&other);
		expect(static_cast<int>(seam.request({.action = deluge::scenes::browser::ListingAction::Open})))
		    .to_equal(static_cast<int>(deluge::scenes::browser::RequestOutcome::Accepted));
		expect(seam.take_result().has_value()).to_be_true();
	});

	it("also trims one entry from each end that a cull had already made unsafe", _ {
		CleanCatalogue clean;
		SpecBrowser browser;
		browser.interpretNoteNames(false);
		seed({"A.XML", "B.XML", "C.XML", "D.XML"});
		FileCatalogue::firstFileItemRemaining = "A.XML";
		FileCatalogue::lastFileItemRemaining = "D.XML";

		FileCatalogue::deleteFolderAndDuplicateItems();
		// Nothing here is a duplicate, so the only losses are the two edge entries the cull
		// boundaries make untrustworthy (`file_catalogue.cpp:285 deleteFolderAndDuplicateItems`, tail).
		expect(catalogue_order()).to_equal(std::string("B.XML,C.XML"));
	});
});
// clang-format on

DELUGE_SPEC(file_catalogue_spec, file_catalogue)
