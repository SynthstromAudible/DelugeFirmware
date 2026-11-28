/// @file
/// @brief The async listing seam (`src/ui/scenes/browser/listing.h`): its state machine, its
///        exactly-once delivery contract, and the five actions it performs over a real card.
///
/// @par What this spec cannot prove, stated before anything green appears below
///
/// **The shipped dispatcher is inline.** `deluge::ui::scenes::run_inline` calls the op on the caller's
/// stack before returning, so a listing is already finished by the time `request()` returns, and
/// *no spec in this repository -- this one included -- exercises concurrency across this seam.* Not
/// preemption, not memory visibility between two contexts, not a listing that yields mid-read while
/// the UI keeps ticking.
///
/// What the examples below do instead is install a **deferring dispatcher** (`Deferred`), which
/// stores the op and runs it when the example says so. That reproduces the one property a deferred
/// dispatch would have that the seam's contract actually depends on -- *dispatch returns before the op runs* --
/// and reproduces nothing else. Every "in flight" assertion here is an assertion about that
/// ordering, not about a second execution context. The `std::atomic` in `Listing` is written for
/// the real case and is never put under any load here.
///
/// This warning is written out because a unit spec can be structurally blind to the thing it is
/// meant to cover (an auto-repeat whose spec could not see the race took a level-2 acceptance
/// script to find). A green run of this file means the state machine and the five actions behave;
/// it does not mean the seam is race-free on device.
///
/// One further gap, of a different kind: **the destroy-while-in-flight rule is asserted, not
/// tested.** `~Listing` `assert`s that the seam is not `InFlight` (the dispatched op holds `this`),
/// and an assert firing aborts the process -- there is no death-test facility in this driver, so no
/// example provokes it. The examples below cover the *other* half of that rule, which is
/// observable: a seam destroyed while holding an uncollected result hands the static catalogue
/// guard back rather than jamming every later seam.
///
/// @par Card hygiene
///
/// The card is installed in memory before boot, so nothing here can write to `tests/fixtures/card`.
/// The example that needs a nested directory the fixture does not have runs on its own card, in
/// `listing_nested_spec.cpp`.

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the
// project headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`).
#include "support/listing_rig.h"

#include "fakes.h"

#include "cppspec.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "support/spec_main.h"

using deluge::ui::scenes::spec::SdRoot;
using namespace deluge::ui::scenes::spec::listing;

// clang-format off
describe listing("the async listing seam", $ {

	// ---- the state machine: request / poll / collect ------------------------------------------

	it("reports in flight from the dispatch until the op runs, then hands the result over once", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		expect(state_of(listing)).to_equal(kIdle);
		expect(outcome_of(listing.request(open_request()))).to_equal(kAccepted);

		// The whole point of the deferring dispatcher: the op has NOT run, and the seam says so.
		expect(listing.in_flight()).to_be_true();
		expect(listing.has_result()).to_be_false();
		expect(listing.take_result().has_value()).to_be_false();
		expect(Deferred::pending()).to_be_true();

		Deferred::run();

		expect(listing.in_flight()).to_be_false();
		expect(state_of(listing)).to_equal(kReady);

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(error_of(*result)).to_equal(static_cast<int>(Error::NONE));
		expect(static_cast<int>(result->count)).to_equal(5);
		expect(catalogue_order()).to_equal(std::string("KITS,SAMPLES,SETTINGS,SONGS,SYNTHS"));

		// Exactly once: the slot is empty afterwards and the seam is idle again.
		expect(listing.take_result().has_value()).to_be_false();
		expect(state_of(listing)).to_equal(kIdle);
	});

	it("refuses a second request while one is in flight, and completes the first one untouched", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		expect(outcome_of(listing.request(open_request()))).to_equal(kAccepted);

		// THE SECOND-REQUEST CASE. Not a silent overwrite (`MenuScene::take_action`'s contract):
		// refused, with the reason, and the running listing keeps its own request.
		ListingRequest second = open_request("SETTINGS");
		expect(outcome_of(listing.request(second))).to_equal(kBusy);
		expect(outcome_of(listing.request(second))).to_equal(kBusy);

		Deferred::run();
		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		// The FIRST request's listing (the card root, five folders), not the refused one's.
		expect(result->dir).to_equal(std::string(""));
		expect(static_cast<int>(result->count)).to_equal(5);
	});

	it("refuses a new request until the completed one has been collected", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		expect(outcome_of(listing.request(open_request()))).to_equal(kAccepted);
		Deferred::run();

		// Completed but uncollected: a request now would silently destroy a result nobody read, so
		// it is refused with a different reason than Busy -- the caller can tell "wait" from
		// "collect first".
		expect(state_of(listing)).to_equal(kReady);
		expect(outcome_of(listing.request(open_request("SETTINGS")))).to_equal(kResultPending);
		expect(Deferred::pending()).to_be_false();

		(void)listing.take_result();
		expect(outcome_of(listing.request(open_request("SETTINGS")))).to_equal(kAccepted);
		Deferred::run();
		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(result->dir).to_equal(std::string("SETTINGS"));
	});

	it("guards the catalogue across INSTANCES, not just within one", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		// The browser, the marker editor and the
		// recorder each own one of these, so several exist in one process and they share the one `FileCatalogue`.
		Listing browser_seam(&Deferred::dispatch);
		Listing marker_seam(&Deferred::dispatch);

		expect(outcome_of(browser_seam.request(open_request()))).to_equal(kAccepted);

		// A freshly constructed second seam is `Idle` by its own reckoning and is refused anyway:
		// the guard is on the resource (legacy's *static* `listingInProgress_`, `gui/ui/browser/browser.h`),
		// not on the object.
		expect(state_of(marker_seam)).to_equal(kIdle);
		expect(outcome_of(marker_seam.request(open_request("SETTINGS")))).to_equal(kBusy);

		Deferred::run();

		// Still refused after the op finishes: `ListingResult` counts the entries rather than
		// copying them, so the first seam's caller has not read `fileItems` yet and a second
		// listing here would replace the vector underneath it.
		expect(outcome_of(marker_seam.request(open_request("SETTINGS")))).to_equal(kBusy);

		auto first = browser_seam.take_result();
		expect(first.has_value()).to_be_true();
		expect(static_cast<int>(first->count)).to_equal(5);

		// Collected -- and only now is the catalogue handed on.
		expect(outcome_of(marker_seam.request(open_request("SETTINGS")))).to_equal(kAccepted);
		Deferred::run();
		auto second = marker_seam.take_result();
		expect(second.has_value()).to_be_true();
		expect(second->dir).to_equal(std::string("SETTINGS"));
	});

	it("hands the catalogue back when a seam is destroyed holding an uncollected result", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		{
			// A scene torn down between the landing and the poll. Not a use-after-free (nothing is
			// in flight), but without the destructor's release the static guard would stay claimed
			// by a dead object and every later seam would be refused for the life of the process.
			Listing abandoned(&Deferred::dispatch);
			expect(outcome_of(abandoned.request(open_request()))).to_equal(kAccepted);
			Deferred::run();
			expect(abandoned.has_result()).to_be_true();
		}

		Listing next(&Deferred::dispatch);
		expect(outcome_of(next.request(open_request("SETTINGS")))).to_equal(kAccepted);
		Deferred::run();
		expect(next.take_result().has_value()).to_be_true();
	});

	it("leaves the seam idle when the owner drops the dispatch", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);
		Deferred::accept = false;

		expect(outcome_of(listing.request(open_request()))).to_equal(kDropped);
		// Nothing is in flight and nothing is owed: `Listing::request`'s dropped-dispatch branch
		// (`ui/scenes/browser/listing.cpp`) clears the in-flight state, so the next event may simply retry.
		expect(state_of(listing)).to_equal(kIdle);
		expect(listing.has_result()).to_be_false();

		// And the busy indicator it raised on the way in is taken back down again.
		NoticeTally tally = drain_notices();
		expect(tally.working).to_equal(1);
		expect(tally.clear_working).to_equal(1);

		Deferred::accept = true;
		expect(outcome_of(listing.request(open_request()))).to_equal(kAccepted);
		Deferred::run();
		expect(listing.take_result().has_value()).to_be_true();
	});

	// ---- what the shell sees: the Working scene and the error popup ---------------------------

	it("raises the shell's Working notice on dispatch and clears it on completion", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		expect(outcome_of(listing.request(open_request()))).to_equal(kAccepted);
		// Legacy shows its loading animation here (`Browser::beginListing`'s
		// `displayLoadingAnimationText("Working")` in `gui/ui/browser/browser.cpp`); the port raises the notice `Notices::drain`
		// turns into a pushed `SceneKind::Working` (`Notices::drain`).
		NoticeTally after_request = drain_notices();
		expect(after_request.working).to_equal(1);
		expect(after_request.clear_working).to_equal(0);

		Deferred::run();
		NoticeTally after_run = drain_notices();
		expect(after_run.clear_working).to_equal(1);
		expect(after_run.error).to_equal(0);
	});

	it("surfaces a failed listing as an error result and an error notice, not a hang", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		ListingRequest req;
		req.step = ListingAction::INTO_FOLDER;
		req.path = "NO_SUCH_FOLDER";
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		// FOLDER_DOESNT_EXIST, as main's FatFS directory open reports a missing folder: the default-folder arms turn on
		// it, and its popup says "Folder not found".
		expect(error_of(*result)).to_equal(static_cast<int>(Error::FOLDER_DOESNT_EXIST));
		// The seam did not hang: it is idle, the animation is down, and the failure was announced
		// the way `Browser::onListingFailed` announces it (`gui/ui/browser/browser.cpp`, displayError).
		expect(state_of(listing)).to_equal(kIdle);
		NoticeTally tally = drain_notices();
		expect(tally.clear_working).to_equal(1);
		expect(tally.error).to_equal(1);
		expect(static_cast<int>(tally.last_error_code)).to_equal(error_of(*result));
	});

	it("keeps a failed reload silent, exactly as main's Browser::selectEncoderAction does", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		// Main's `Browser::selectEncoderAction` (`origin/main:src/deluge/gui/ui/browser/browser.cpp`) empties the file
		// items on a failed reload and returns, so the browser neither pops up nor closes. The port keeps the silence
		// but still reports the error in the result, so a caller that wants the popup has the fact.
		FileCatalogue::currentDir = "NO_SUCH_FOLDER";
		ListingRequest req;
		req.step = ListingAction::RELOAD;
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(error_of(*result)).not_().to_equal(static_cast<int>(Error::NONE));
		NoticeTally tally = drain_notices();
		expect(tally.error).to_equal(0);
		expect(tally.clear_working).to_equal(1);
	});

	// ---- the five actions, over a real card ---------------------------------------------------

	it("walks into a folder", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		ListingRequest req;
		req.step = ListingAction::INTO_FOLDER;
		req.path = "SETTINGS";
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(error_of(*result)).to_equal(static_cast<int>(Error::NONE));
		expect(result->dir).to_equal(std::string("SETTINGS"));
		expect(catalogue_order()).to_equal(std::string("FAVOURITES,MIDIFollow.XML"));
	});

	it("refuses to ascend out of a single-segment directory, as legacy does", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		// `goUpOneDirectoryLevelImpl` tests for a `/` in `currentDir`, not for emptiness
		// (`gui/ui/browser/browser.cpp`), so "SETTINGS" is already the top as far as it is concerned.
		FileCatalogue::currentDir = "SETTINGS";
		ListingRequest req;
		req.step = ListingAction::UP_LEVEL;
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(error_of(*result))
		    .to_equal(static_cast<int>(Error::NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP));
		expect(FileCatalogue::currentDir).to_equal(std::string("SETTINGS"));
	});


	it("finds a file by its full path", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		ListingRequest req;
		req.step = ListingAction::BY_PATH;
		req.path = "SETTINGS/MIDIFollow.XML";
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(error_of(*result)).to_equal(static_cast<int>(Error::NONE));
		expect(result->dir).to_equal(std::string("SETTINGS"));
		expect(result->entered_text).to_equal(std::string("MIDIFollow.XML"));
		// Index 1, not 0: `SETTINGS/` holds the `FAVOURITES` folder as well, and folders sort in
		// with files rather than ahead of them as a group.
		const deluge::ui::scenes::bind::SearchHit hit = deluge::gui::shell_host::catalogue().search(result->entered_text);
		expect(hit.index).to_equal(std::size_t{1});
		expect(hit.exact).to_be_true();
	});

	it("reports a path that is not on the card as FILE_NOT_FOUND", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		ListingRequest req;
		req.step = ListingAction::BY_PATH;
		req.path = "SETTINGS/NOPE.XML";
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(error_of(*result)).to_equal(static_cast<int>(Error::FILE_NOT_FOUND));
	});

	it("reloads the current directory in place", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		FileCatalogue::currentDir = "SETTINGS";
		ListingRequest req;
		req.step = ListingAction::RELOAD;
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(error_of(*result)).to_equal(static_cast<int>(Error::NONE));
		expect(result->dir).to_equal(std::string("SETTINGS"));
		expect(catalogue_order()).to_equal(std::string("FAVOURITES,MIDIFollow.XML"));
	});

	it("falls back to the default directory when the requested one is missing, as main does", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		// `readFileItemsFromFolderAndMemory`: a folder that is not there gives way to `defaultDirToAlsoTry`, read from
		// its start.
		ListingRequest req = open_request("NO_SUCH_FOLDER");
		req.default_dir = "SETTINGS";
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(error_of(*result)).to_equal(static_cast<int>(Error::NONE));
		expect(result->dir).to_equal(std::string("SETTINGS"));
		expect(catalogue_order()).to_equal(std::string("FAVOURITES,MIDIFollow.XML"));
	});

	// ---- the catalogue port ---------------------------------------------------------------------

	it("lists through the catalogue it is pointed at, and claims and releases that catalogue", _ {
		CleanListing clean;
		deluge::ui::scenes::bind::test::FakeCatalogue fake;
		fake.entries = {{"a.wav", false}, {"B", true}, {"c.wav", false}};
		fake.folder = "SAMPLES";
		fake.next_outcome.error = static_cast<int16_t>(Error::SD_CARD);
		deluge::ui::scenes::browser::Listing seam(&Deferred::dispatch);
		seam.set_catalogue(&fake);

		ListingRequest req = open_request("SAMPLES");
		req.kinds = deluge::ui::scenes::bind::FileKinds::AUDIO;
		expect(outcome_of(seam.request(req))).to_equal(kAccepted);
		expect(fake.holder).to_equal(static_cast<const void*>(&seam));
		Deferred::run();

		auto result = seam.take_result();
		expect(result.has_value()).to_be_true();
		expect(error_of(*result)).to_equal(static_cast<int>(Error::SD_CARD));
		expect(result->count).to_equal(std::size_t{3});
		expect(result->dir).to_equal(std::string("SAMPLES"));
		expect(fake.listed.size()).to_equal(std::size_t{1});
		expect(fake.listed[0].path).to_equal(std::string("SAMPLES"));
		expect(fake.holder).to_equal(static_cast<const void*>(nullptr));
		expect(drain_notices().error).to_equal(1);
	});

	it("drops a request when it has no catalogue, and announces nothing", _ {
		CleanListing clean;
		deluge::ui::scenes::browser::Listing seam(&Deferred::dispatch);
		expect(outcome_of(seam.request(open_request()))).to_equal(kDropped);
		expect(Deferred::pending()).to_be_false();
		const NoticeTally tally = drain_notices();
		expect(tally.working).to_equal(0);
	});

	// ---- the harness's own limit, pinned rather than described --------------------------------

	it("completes INLINE under the shipped dispatcher, which is why every example above defers", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		// The default-constructed seam dispatches through `deluge::ui::scenes::run_inline`, which runs
		// the op on the caller's stack. So the result is ALREADY waiting when `request()` returns
		// and `in_flight()` is never observably true from here.
		//
		// This example exists to pin that fact rather than leave it in a comment: if the default
		// dispatch ever becomes deferred, this fails and whoever changed it learns that every other
		// example in this file is modelling an ordering the seam now provides for real.
		Listing listing;

		expect(outcome_of(listing.request(open_request()))).to_equal(kAccepted);
		expect(listing.in_flight()).to_be_false();
		expect(state_of(listing)).to_equal(kReady);

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(static_cast<int>(result->count)).to_equal(5);
	});
});
// clang-format on

DELUGE_SPEC(listing_spec, listing)
