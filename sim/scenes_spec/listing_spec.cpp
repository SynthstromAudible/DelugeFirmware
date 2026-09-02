/// @file
/// @brief The async listing seam (`src/scenes/browser/listing.h`): its state machine, its
///        exactly-once delivery contract, and the five actions it performs over a real card.
///
/// @par What this spec cannot prove, stated before anything green appears below
///
/// **On the host, `storage::Owner::run` is inline.** `Owner::on_owner()` is always false there and
/// `run()` calls the op on the caller's stack before returning (`storage/owner.h:55-59`). So with
/// the shipped dispatcher a listing is already finished by the time `request()` returns, and *no
/// spec in this repository -- this one included -- exercises real fiber concurrency across this
/// seam.* Not preemption, not memory visibility between two contexts, not an owner queue under
/// contention, not a listing that yields mid-read while the UI keeps ticking.
///
/// What the examples below do instead is install a **deferring dispatcher** (`Deferred`), which
/// stores the op and runs it when the example says so. That reproduces the one property of the
/// Embassy path the seam's contract actually depends on -- *dispatch returns before the op runs* --
/// and reproduces nothing else. Every "in flight" assertion here is an assertion about that
/// ordering, not about a second execution context. The `std::atomic` in `Listing` is written for
/// the real case and is never put under any load here.
///
/// This warning is written out because a comparable gap has already cost this plan a task: E2
/// shipped an auto-repeat whose unit spec was structurally blind to the thing it was meant to
/// cover, and it took a level-2 acceptance script to find it. A green run of this file means the
/// state machine and the five actions behave; it does not mean the seam is race-free on device.
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
/// Nothing here writes to `tests/fixtures/card`: the fixture examples only `opendir`/`readdir` it,
/// and the two examples that need a directory the fixture does not have build a scratch card under
/// the host temp directory and remove it again (`NestedCard`). `git status --porcelain
/// tests/fixtures` stays clean.

#include "scenes/browser/listing.h"

#include "definitions_cxx.hpp"
#include "notify/notice_queue.h"
#include "storage/file_catalogue.h"
#include "storage/file_item.h"
#include "storage/owner.h"
#include "util/name_compare.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the
// project headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`).
#include "support/card_access.h"

#include "cppspec.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

using deluge::scenes::browser::Listing;
using deluge::scenes::browser::ListingAction;
using deluge::scenes::browser::ListingRequest;
using deluge::scenes::browser::ListingResult;
using deluge::scenes::browser::RequestOutcome;
using deluge::scenes::spec::QuietAudio;
using deluge::scenes::spec::SdRoot;
using deluge::storage::FileCatalogue;

namespace {

const std::string kFixtureCard = std::string(DELUGE_REPO_ROOT) + "/tests/fixtures/card";

/// @brief A dispatcher that stores the op instead of running it, so the in-flight state exists at
///        all on a host where `Owner::run` is inline.
///
/// `Listing::Dispatch` is a bare function pointer (it has to be -- it is `Owner::run`'s own
/// C-ABI-shaped signature), so the pending op lives in statics rather than in a lambda capture.
/// One `Listing` is driven at a time, so one slot is enough.
struct Deferred {
	static inline void (*fn)(void*) = nullptr;
	static inline void* ctx = nullptr;
	/// Set false to model `Owner::run` returning false -- the queue-full drop (`owner.h:43`).
	static inline bool accept = true;

	static bool dispatch(void (*f)(void*), void* c) {
		if (!accept) {
			return false;
		}
		fn = f;
		ctx = c;
		return true;
	}
	static bool pending() { return fn != nullptr; }
	/// @brief Run the stored op, as the owner fiber eventually would.
	static void run() {
		void (*f)(void*) = fn;
		void* c = ctx;
		fn = nullptr;
		ctx = nullptr;
		if (f != nullptr) {
			f(c);
		}
	}
	static void reset() {
		fn = nullptr;
		ctx = nullptr;
		accept = true;
	}
};

/// @brief How many notices of each kind are sitting in the global queue; drains it.
struct NoticeTally {
	int working = 0;
	int clear_working = 0;
	int error = 0;
	int16_t last_error_code = 0;
};

NoticeTally drain_notices() {
	NoticeTally tally;
	deluge::notify::Notice n;
	while (deluge::notify::notices().pop(n)) {
		switch (n.kind) {
		case deluge::notify::Notice::Kind::Working:
			tally.working++;
			break;
		case deluge::notify::Notice::Kind::ClearWorking:
			tally.clear_working++;
			break;
		case deluge::notify::Notice::Kind::Error:
			tally.error++;
			tally.last_error_code = n.error_code;
			break;
		default:
			break;
		}
	}
	return tally;
}

/// @brief Per-example reset of everything process-global this spec touches.
///
/// The catalogue's statics are the same shared state `file_catalogue_spec.cpp`'s `CleanCatalogue`
/// guards, for the same reason: one spec binary runs many examples in one process, so an example
/// that left entries (or a stale `currentDir`, or a stale comparator flag) behind would decide the
/// next one's result by link order. `QuietAudio` is declared first so the latch is held before this
/// guard's own body erases anything -- see `support/card_access.h` for why erasing without it
/// segfaults.
struct CleanListing {
	QuietAudio quiet;

	CleanListing() { reset(); }
	~CleanListing() { reset(); }
	static void reset() {
		FileCatalogue::emptyFileItems();
		FileCatalogue::numFileItemsDeletedAtStart = 0;
		FileCatalogue::numFileItemsDeletedAtEnd = 0;
		FileCatalogue::firstFileItemRemaining.clear();
		FileCatalogue::lastFileItemRemaining.clear();
		FileCatalogue::filenameToStartSearchAt = nullptr;
		FileCatalogue::currentDir.clear();
		shouldInterpretNoteNames = false;
		octaveStartsFromA = false;
		Deferred::reset();
		(void)drain_notices();
	}
	CleanListing(const CleanListing&) = delete;
	CleanListing& operator=(const CleanListing&) = delete;
};

/// @brief A scratch card with a two-level directory, for the `UpLevel` examples.
///
/// The committed fixture is flat (`KITS`, `SAMPLES`, `SETTINGS`, `SONGS`, `SYNTHS`, one file), and
/// `UpLevel` needs a directory with a parent that is not the root -- legacy refuses to ascend when
/// `currentDir` has no `/` in it at all (`browser.cpp:1382`). Under the host temp directory,
/// removed on the way out, so nothing is left behind and the committed fixture is untouched.
///
/// Layout: `OUTER/A.XML`, `OUTER/INNER/B.XML`.
struct NestedCard {
	std::filesystem::path dir;
	SdRoot root;

	static std::filesystem::path make() {
		std::filesystem::path d = std::filesystem::temp_directory_path() / "deluge_listing_spec";
		std::filesystem::remove_all(d);
		std::filesystem::create_directories(d / "OUTER" / "INNER");
		std::ofstream(d / "OUTER" / "A.XML") << "<x/>";
		std::ofstream(d / "OUTER" / "INNER" / "B.XML") << "<x/>";
		return d;
	}

	NestedCard() : dir(make()), root(dir.string()) {}
	~NestedCard() { std::filesystem::remove_all(dir); }
	NestedCard(const NestedCard&) = delete;
	NestedCard& operator=(const NestedCard&) = delete;
};

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

/// @brief `Listing::State` as an int, so a mismatch prints as a number rather than failing to
///        compile against CppSpec's formatter.
int state_of(const Listing& listing) {
	return static_cast<int>(listing.state());
}
constexpr int kIdle = static_cast<int>(Listing::State::Idle);
constexpr int kInFlight = static_cast<int>(Listing::State::InFlight);
constexpr int kReady = static_cast<int>(Listing::State::Ready);

int outcome_of(RequestOutcome o) {
	return static_cast<int>(o);
}
constexpr int kAccepted = static_cast<int>(RequestOutcome::Accepted);
constexpr int kBusy = static_cast<int>(RequestOutcome::Busy);
constexpr int kResultPending = static_cast<int>(RequestOutcome::ResultPending);
constexpr int kDropped = static_cast<int>(RequestOutcome::Dropped);

/// @brief An `Open` request for the current directory, with the defaults every example shares.
ListingRequest open_request(std::string path = {}) {
	ListingRequest req;
	req.action = ListingAction::Open;
	req.path = std::move(path);
	return req;
}

} // namespace

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
		expect(static_cast<int>(result->error)).to_equal(static_cast<int>(Error::NONE));
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
		// Tasks 3-5 of this plan give one of these to the browser, the marker editor and the
		// recorder, so several exist in one process and they share the one `FileCatalogue`.
		Listing browser_seam(&Deferred::dispatch);
		Listing marker_seam(&Deferred::dispatch);

		expect(outcome_of(browser_seam.request(open_request()))).to_equal(kAccepted);

		// A freshly constructed second seam is `Idle` by its own reckoning and is refused anyway:
		// the guard is on the resource (legacy's *static* `listingInProgress_`, `browser.h:170`),
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
		// Nothing is in flight and nothing is owed: legacy clears its own flag on this return
		// (`browser.cpp:113-117`), so the next event may simply retry.
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
		// Legacy shows its loading animation here (`browser.cpp:111
		// displayLoadingAnimationText("Working")`); the port raises the notice `Notices::drain`
		// turns into a pushed `SceneKind::Working` (`shell/notices.cpp:260-265`).
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
		req.action = ListingAction::IntoFolder;
		req.path = "NO_SUCH_FOLDER";
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		// FILE_NOT_FOUND, not FOLDER_DOESNT_EXIST -- pinned exactly because a whole legacy branch
		// turns on the difference. `Directory::open` on a missing folder yields
		// `DELUGE_ERR_NOT_FOUND`, which `delugeStatusToError` maps to `Error::FILE_NOT_FOUND`
		// (`util/functions.cpp:1879-1880`); nothing on a listing path can produce
		// `Error::FOLDER_DOESNT_EXIST` any more, which is why legacy's default-directory fallback
		// (`browser.cpp:293`) is dead and this port's fallback accepts both codes.
		expect(static_cast<int>(result->error)).to_equal(static_cast<int>(Error::FILE_NOT_FOUND));
		// The seam did not hang: it is idle, the animation is down, and the failure was announced
		// the way `Browser::onListingFailed` announces it (`browser.cpp:166-167`, displayError).
		expect(state_of(listing)).to_equal(kIdle);
		NoticeTally tally = drain_notices();
		expect(tally.clear_working).to_equal(1);
		expect(tally.error).to_equal(1);
		expect(static_cast<int>(tally.last_error_code)).to_equal(static_cast<int>(result->error));
	});

	it("keeps a failed reload silent, exactly as legacy's reloadImpl does", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		// `reloadImpl` swallows its read failure and returns Error::NONE so the browser neither
		// pops up nor closes (`browser.cpp:747-753`). The port keeps the silence but still reports
		// the error in the result, so a caller that wants the popup has the fact.
		FileCatalogue::currentDir = "NO_SUCH_FOLDER";
		ListingRequest req;
		req.action = ListingAction::Reload;
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(static_cast<int>(result->error) != static_cast<int>(Error::NONE)).to_be_true();
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
		req.action = ListingAction::IntoFolder;
		req.path = "SETTINGS";
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(static_cast<int>(result->error)).to_equal(static_cast<int>(Error::NONE));
		expect(result->dir).to_equal(std::string("SETTINGS"));
		expect(catalogue_order()).to_equal(std::string("MIDIFollow.XML"));
	});

	it("refuses to ascend out of a single-segment directory, as legacy does", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		// `goUpOneDirectoryLevelImpl` tests for a `/` in `currentDir`, not for emptiness
		// (`browser.cpp:1382`), so "SETTINGS" is already the top as far as it is concerned.
		FileCatalogue::currentDir = "SETTINGS";
		ListingRequest req;
		req.action = ListingAction::UpLevel;
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(static_cast<int>(result->error))
		    .to_equal(static_cast<int>(Error::NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP));
		expect(FileCatalogue::currentDir).to_equal(std::string("SETTINGS"));
	});

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

	it("finds a file by its full path", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		ListingRequest req;
		req.action = ListingAction::ByPath;
		req.path = "SETTINGS/MIDIFollow.XML";
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(static_cast<int>(result->error)).to_equal(static_cast<int>(Error::NONE));
		expect(result->dir).to_equal(std::string("SETTINGS"));
		expect(result->entered_text).to_equal(std::string("MIDIFollow.XML"));
		expect(result->file_index).to_equal(0);
		expect(result->found_exact).to_be_true();
	});

	it("reports a path that is not on the card as FILE_NOT_FOUND", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		ListingRequest req;
		req.action = ListingAction::ByPath;
		req.path = "SETTINGS/NOPE.XML";
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(static_cast<int>(result->error)).to_equal(static_cast<int>(Error::FILE_NOT_FOUND));
	});

	it("reloads the current directory in place", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		FileCatalogue::currentDir = "SETTINGS";
		ListingRequest req;
		req.action = ListingAction::Reload;
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(static_cast<int>(result->error)).to_equal(static_cast<int>(Error::NONE));
		expect(result->dir).to_equal(std::string("SETTINGS"));
		expect(catalogue_order()).to_equal(std::string("MIDIFollow.XML"));
	});

	it("falls back to the default directory when the requested one does not exist", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		Listing listing(&Deferred::dispatch);

		// Legacy's `defaultDirToAlsoTry` retry (`browser.cpp:293-300`), once. The third arm --
		// mkdir the default directory -- is deliberately not ported (see listing.h), so a default
		// directory that does not exist either simply fails.
		//
		// This example is the one that caught legacy's condition being dead: written against
		// legacy's `error == FOLDER_DOESNT_EXIST` gate it failed with error 18
		// (`FILE_NOT_FOUND`), which is what a missing directory actually returns today. See the
		// divergence note in `Listing::list_current_dir`.
		ListingRequest req = open_request("NO_SUCH_FOLDER");
		req.default_dir = "SETTINGS";
		expect(outcome_of(listing.request(req))).to_equal(kAccepted);
		Deferred::run();

		auto result = listing.take_result();
		expect(result.has_value()).to_be_true();
		expect(static_cast<int>(result->error)).to_equal(static_cast<int>(Error::NONE));
		expect(result->dir).to_equal(std::string("SETTINGS"));
		expect(catalogue_order()).to_equal(std::string("MIDIFollow.XML"));
	});

	// ---- the harness's own limit, pinned rather than described --------------------------------

	it("completes INLINE under the shipped dispatcher, which is why every example above defers", _ {
		CleanListing clean;
		SdRoot root(kFixtureCard);
		// The default-constructed seam dispatches through `storage::Owner::run`, which on the host
		// runs the op on the caller's stack (`owner.h:55-59`, `on_owner()` always false). So the
		// result is ALREADY waiting when `request()` returns and `in_flight()` is never observably
		// true from here.
		//
		// This example exists to pin that fact rather than leave it in a comment: if `Owner::run`
		// ever becomes deferred on the host, this fails and whoever changed it learns that every
		// other example in this file is modelling an ordering the host now provides for real.
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
