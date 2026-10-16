/// @file
/// @brief The listing seam's `UpLevel` out of a nested directory, on a card with one.
///
/// The fixture card is two levels deep only at `SETTINGS/FAVOURITES/`, and `UpLevel` needs a
/// directory whose parent is not the root -- legacy refuses to ascend when `currentDir` has no `/`
/// in it at all (`browser.cpp:1382`). So this example runs on its own card,
/// `tests/fixtures/scenes_spec/listing_nested` (`OUTER/A.XML`, `OUTER/INNER/B.XML`), and the rest of
/// the seam's examples stay in `listing_spec.cpp` on the fixture card.

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the
// project headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`).
#include "support/listing_rig.h"

#include "cppspec.hpp"

#include <filesystem>
#include <string>

#include "spec/support/spec_main.h"

using deluge::scenes::spec::SdRoot;
using namespace deluge::scenes::spec::listing;

namespace {

/// @brief The nested card, installed for this file before boot
///        (`tests/integration/checks/scenes_spec_cards.txt`). Constructing one checks it is the installed card.
struct NestedCard {
	std::filesystem::path dir = std::filesystem::path(DELUGE_REPO_ROOT) / "tests/fixtures/scenes_spec/listing_nested";
	SdRoot root{dir.string()};
};

} // namespace

// clang-format off
describe listing_nested("the async listing seam over a nested directory", $ {
	it("ascends out of a nested directory, naming the folder it left", _ {
		CleanListing clean;
		NestedCard card;
		Listing listing(&Deferred::dispatch);

		expect(outcome_of(listing.request(open_request("OUTER/INNER")))).to_equal(kAccepted);
		Deferred::run();
		auto inner = listing.take_result();
		expect(inner.has_value()).to_be_true();
		expect(catalogue_order()).to_equal(std::string("B.XML"));

		ListingRequest up;
		up.action = ListingAction::UpLevel;
		expect(outcome_of(listing.request(up))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(static_cast<int>(result->error)).to_equal(static_cast<int>(Error::NONE));
		expect(result->dir).to_equal(std::string("OUTER"));
		// Legacy assigns the dropped segment to `enteredText` and lists starting at it
		// (`browser.cpp:1388`), so the folder just left is what the browser lands on.
		expect(result->entered_text).to_equal(std::string("INNER"));
		expect(catalogue_order()).to_equal(std::string("A.XML,INNER"));
	});
});
// clang-format on

DELUGE_SPEC(listing_nested_spec, listing_nested)
