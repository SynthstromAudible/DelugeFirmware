/// @file
/// @brief The listing seam's missing-folder arms on a card that has neither `DX7` nor `SAMPLES`: a missing default
///        folder is created and listed, empty, as main's `readFileItemsFromFolderAndMemory` does, and a missing folder
///        with no default is the card's error.
///
/// The card is empty (`none` in `tests/integration/checks/scenes_spec_cards.txt`) and in memory, so the folders these
/// examples create reach no committed fixture. Each example names its own folder, so their order does not matter.

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the
// project headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`).
#include "support/listing_rig.h"

#include "cppspec.hpp"

#include <string>

#include "spec/support/spec_main.h"

using deluge::scenes::spec::NoSdRoot;
using namespace deluge::scenes::spec::listing;

namespace {

/// @brief `DxSyxBrowser`'s extension list.
char const* kSyxExtensions[] = {"SYX", nullptr};

/// @brief Whether @p path is a folder on the installed card.
bool folder_on_card(const char* path) {
	return deluge::io::Directory::open(path).has_value();
}

} // namespace

// clang-format off
describe listing_missing_folder("the async listing seam on a card without the default folder", $ {
	it("creates the default folder when it is the one asked for, and lists it empty", _ {
		CleanListing clean;
		NoSdRoot card;
		expect(folder_on_card("DX7")).to_be_false();
		// The previous folder's cull count, which the new folder's own listing replaces.
		FileCatalogue::numFileItemsDeletedAtStart = 3;
		Listing listing(&Deferred::dispatch);

		// `DxSyxBrowser::opened`: `currentDir` "DX7", default "DX7".
		ListingRequest req = open_request("DX7");
		req.default_dir = "DX7";
		req.allowed_extensions = kSyxExtensions;
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(static_cast<int>(result->error)).to_equal(static_cast<int>(Error::NONE));
		expect(result->dir).to_equal(std::string("DX7"));
		expect(static_cast<int>(result->count)).to_equal(0);
		expect(static_cast<int>(FileCatalogue::numFileItemsDeletedAtStart)).to_equal(0);
		expect(folder_on_card("DX7")).to_be_true();
		expect(drain_notices().error).to_equal(0);
	});

	it("falls back from a missing folder to the default one, and creates that when it is missing too", _ {
		CleanListing clean;
		NoSdRoot card;
		expect(folder_on_card("SAMPLES")).to_be_false();
		Listing listing(&Deferred::dispatch);

		// `SampleBrowser::opened` beside a remembered sample whose folder has gone, on a card with no SAMPLES either.
		ListingRequest req = open_request("SAMPLES/KICKS");
		req.filename_to_start_at = "KICK1.WAV";
		req.default_dir = "SAMPLES";
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(static_cast<int>(result->error)).to_equal(static_cast<int>(Error::NONE));
		expect(result->dir).to_equal(std::string("SAMPLES"));
		expect(static_cast<int>(result->count)).to_equal(0);
		expect(folder_on_card("SAMPLES")).to_be_true();
		expect(folder_on_card("SAMPLES/KICKS")).to_be_false();
		expect(drain_notices().error).to_equal(0);
	});

	it("reports a missing folder with no default as FOLDER_DOESNT_EXIST, and creates nothing", _ {
		CleanListing clean;
		NoSdRoot card;
		Listing listing(&Deferred::dispatch);

		expect(outcome_of(listing.request(open_request("NOPE")))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(static_cast<int>(result->error)).to_equal(static_cast<int>(Error::FOLDER_DOESNT_EXIST));
		expect(folder_on_card("NOPE")).to_be_false();
		NoticeTally tally = drain_notices();
		expect(tally.error).to_equal(1);
		expect(static_cast<int>(tally.last_error_code)).to_equal(static_cast<int>(Error::FOLDER_DOESNT_EXIST));
	});
});
// clang-format on

DELUGE_SPEC(listing_missing_folder_spec, listing_missing_folder)
