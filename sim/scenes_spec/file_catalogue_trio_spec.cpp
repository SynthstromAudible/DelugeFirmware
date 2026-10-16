/// @file
/// @brief The file catalogue's mid-listing cull under the browser's note-name flag, on a card holding
///        `kReversingTrio`.
///
/// A real listing that overflows its cap is the only route by which the note-name flag reaches the
/// cull through `readFileItemsForFolder`, so this example needs a folder of exactly these three
/// files: `tests/fixtures/scenes_spec/file_catalogue_trio`. The rest of the catalogue's examples stay
/// in `file_catalogue_spec.cpp` on the fixture card.

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the headers this pulls in.
#include "support/catalogue_rig.h"

#include "cppspec.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <tuple>

#include "spec/support/spec_main.h"

namespace {

using namespace deluge::scenes::spec::catalogue;
using deluge::scenes::spec::SdRoot;

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

/// @brief The card holding `kReversingTrio`, installed for this file before boot
///        (`tests/integration/checks/scenes_spec_cards.txt`). Constructing one checks it is the installed card.
struct TrioCard {
	std::filesystem::path dir =
	    std::filesystem::path(DELUGE_REPO_ROOT) / "tests/fixtures/scenes_spec/file_catalogue_trio";
	SdRoot root{dir.string()};
};

} // namespace

// clang-format off
describe file_catalogue_trio("the browser's file catalogue, listing past its cap", $ {
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
			FileCatalogue::currentDir.clear();
			Error error = browser.readFileItemsForFolder(nullptr, /*allowFolders=*/false, allowedFileExtensionsXML,
			                                             /*filenameToStartAt=*/"", /*newMaxNumFileItems=*/2,
			                                             CATALOG_SEARCH_LEFT);
			return std::tuple{error, FileCatalogue::firstFileItemRemaining, static_cast<int32_t>(FileCatalogue::fileItems.size()),
			                  FileCatalogue::numFileItemsDeletedAtStart};
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
});
// clang-format on

DELUGE_SPEC(file_catalogue_trio_spec, file_catalogue_trio)
