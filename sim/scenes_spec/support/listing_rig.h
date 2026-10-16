#pragma once

/// @file
/// @brief What the listing seam's specs share: a deferring dispatcher, the notice tally, the clean-catalogue guard
///        and the small readers the examples assert through. `listing_spec.cpp` and `listing_nested_spec.cpp` run on
///        different cards, so they are two files and this is their common part.
///
/// @warning **Include this BEFORE `<cppspec.hpp>`** (see `card_access.h`).

#include "scenes/browser/listing.h"

#include "definitions_cxx.hpp"
#include "notify/notice_queue.h"
#include "storage/file_catalogue.h"
#include "storage/file_item.h"
#include "util/name_compare.h"

#include "support/card_access.h"

#include <cstdint>
#include <string>
#include <utility>

namespace deluge::scenes::spec::listing {

using deluge::scenes::browser::Listing;
using deluge::scenes::browser::ListingAction;
using deluge::scenes::browser::ListingRequest;
using deluge::scenes::browser::ListingResult;
using deluge::scenes::browser::RequestOutcome;
using deluge::scenes::spec::QuietAudio;
using deluge::storage::FileCatalogue;

inline const std::string kFixtureCard = std::string(DELUGE_REPO_ROOT) + "/tests/fixtures/card";

/// @brief A dispatcher that stores the op instead of running it, so the in-flight state exists at
///        all when the shipped dispatcher is inline.
///
/// `Listing::Dispatch` is a bare function pointer, so the pending op lives in statics rather than
/// in a lambda capture.
/// One `Listing` is driven at a time, so one slot is enough.
struct Deferred {
	static inline void (*fn)(void*) = nullptr;
	static inline void* ctx = nullptr;
	/// Set false to model a dispatcher refusing the op -- `RequestOutcome::Dropped`.
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
	/// @brief Run the stored op, as a deferred dispatcher eventually would.
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

inline NoticeTally drain_notices() {
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

/// @brief The catalogue's contents as one comma-joined string, so an order mismatch prints as an
///        order rather than as a container diff.
inline std::string catalogue_order() {
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
inline int state_of(const Listing& listing) {
	return static_cast<int>(listing.state());
}
inline constexpr int kIdle = static_cast<int>(Listing::State::Idle);
inline constexpr int kInFlight = static_cast<int>(Listing::State::InFlight);
inline constexpr int kReady = static_cast<int>(Listing::State::Ready);

inline int outcome_of(RequestOutcome o) {
	return static_cast<int>(o);
}
inline constexpr int kAccepted = static_cast<int>(RequestOutcome::Accepted);
inline constexpr int kBusy = static_cast<int>(RequestOutcome::Busy);
inline constexpr int kResultPending = static_cast<int>(RequestOutcome::ResultPending);
inline constexpr int kDropped = static_cast<int>(RequestOutcome::Dropped);

/// @brief An `Open` request for the current directory, with the defaults every example shares.
inline ListingRequest open_request(std::string path = {}) {
	ListingRequest req;
	req.action = ListingAction::Open;
	req.path = std::move(path);
	return req;
}

} // namespace deluge::scenes::spec::listing
