/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "scenes/browser/listing.h"

#include "notify/notice_queue.h"
#include "storage/storage_manager.h"

#include <cassert>
#include <cctype>
#include <utility>

namespace deluge::scenes::browser {

using deluge::storage::FileCatalogue;

namespace {

/// @brief Legacy's `deluge::string::caselessEquals` (`util/string.h`), re-spelled locally.
///
/// `util/**` is not on the list of directories `src/scenes` may include
/// (`src/scenes/CMakeLists.txt`), and this is the only thing from it the seam wanted, so it is
/// four lines here rather than a hole in the layer rule.
/// @param a First string.
/// @param b Second string.
/// @return True if they match ignoring ASCII case.
bool caseless_equals(std::string_view a, std::string_view b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); i++) {
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
			return false;
		}
	}
	return true;
}

/// @brief Should a failure of \p action be announced as a popup?
///
/// Every action but `Reload`: legacy's `runPendingListing` routes a failure to `onListingFailed`
/// (`browser.cpp:154`), which calls `display->displayError` (`:148`) -- except `reloadImpl`, which swallows
/// its read failure entirely and returns `Error::NONE` so the browser neither pops up nor closes
/// (`browser.cpp:747-753`, "Reload failure stays silent"). The seam keeps that distinction, but
/// unlike legacy it still reports the error in `ListingResult::error`, so a caller that *wants* to
/// announce a reload failure has the fact and can raise its own notice.
/// @param action The action that failed.
/// @return True to raise `notice_error_code`.
bool announces_errors(ListingAction action) {
	return action != ListingAction::Reload;
}

} // namespace

Listing::~Listing() {
	const State state = state_.load(std::memory_order_acquire);

	// The lifetime rule, enforced where it can be seen. A dispatched op holds `this` and cannot be
	// withdrawn, so destroying an in-flight seam hands the storage fiber a dangling pointer to
	// write `result_` and `state_` through. See the class docstring for why the answer is a rule
	// plus this assert rather than a heap-owned op context.
	assert(state != State::InFlight
	       && "a Listing was destroyed with a listing in flight -- the dispatched owner op still "
	          "holds `this`; own a Listing as a scene member, never on a stack frame");

	if (state != State::InFlight) {
		// Not in flight: if this instance was holding the catalogue (an uncollected `Ready`),
		// hand it back, or every later listing -- this layer's or legacy's -- is refused forever.
		// `release` is a no-op for a non-claimant, so this needs no state test of its own.
		FileCatalogue::release(this);
	}
	// In flight and past the assert (a release build): the owner is deliberately NOT released. A
	// jammed browser is a better outcome than letting a second listing race a fiber that is still
	// writing through a freed `this`.
}

RequestOutcome Listing::request(ListingRequest req) {
	// This instance's own state first: an uncollected result is a more specific answer than "the
	// catalogue is busy", and it is the one the caller can actually act on.
	if (state_.load(std::memory_order_acquire) == State::Ready) {
		return RequestOutcome::ResultPending;
	}

	// Then the guard, which lives on the resource rather than here: `FileCatalogue::claim`. That
	// is what makes single flight a property of the shared catalogue rather than of this object --
	// tasks 3-5 of this plan put three of these seams in the same process, AND legacy's
	// `Browser::beginListing` (`gui/ui/browser/browser.cpp:97`) claims through the very same call,
	// so a legacy browser listing and a scenes listing can no longer both believe they are alone
	// over one `fileItems`. A claim held by anyone -- this instance already in flight, another
	// seam, or a legacy browser -- is `Busy`.
	if (!FileCatalogue::claim(this)) {
		return RequestOutcome::Busy;
	}

	request_ = std::move(req);
	result_ = {};
	state_.store(State::InFlight, std::memory_order_release);

	// The busy indicator, raised the way E1 built it: `Notices::drain` arms `WorkingScene` and
	// pushes `SceneKind::Working` on this notice (`shell/notices.cpp:260-265`). "Working" is
	// legacy's own word here -- `beginListing` calls
	// `display->displayLoadingAnimationText("Working", /*delayed=*/true)` (`browser.cpp:112`).
	notify::notice_text("Working", notify::Notice::Kind::Working);

	if (!dispatch_(&Listing::trampoline, this)) {
		// The owner dropped it, so the op will never run. Undo everything the dispatch attempt
		// did -- exactly what legacy does on the same return (`browser.cpp:113-117`: clear the
		// flag, remove the loading animation, return false).
		state_.store(State::Idle, std::memory_order_release);
		FileCatalogue::release(this);
		notify::notice_kind(notify::Notice::Kind::ClearWorking);
		return RequestOutcome::Dropped;
	}
	return RequestOutcome::Accepted;
}

std::optional<ListingResult> Listing::take_result() {
	if (state_.load(std::memory_order_acquire) != State::Ready) {
		return std::nullopt;
	}
	ListingResult out = std::move(result_);
	result_ = {};
	state_.store(State::Idle, std::memory_order_release);
	// The catalogue is released HERE, not when the op finished. `ListingResult` counts the entries
	// rather than copying them -- they are still in `FileCatalogue::fileItems`, and this caller has
	// not read them yet. Anything allowed to dispatch in that window -- another seam, or a legacy
	// browser -- would replace the vector underneath a caller that had just been told its listing
	// had landed. Legacy releases at the end of its op instead, and that is the same contract, not
	// a looser one: its consumer reads inside the op. See `FileCatalogue::claim`.
	FileCatalogue::release(this);
	return out;
}

void Listing::set_dispatch(Dispatch dispatch) {
	// Idle only: an `InFlight` seam is waiting for an op the old dispatcher holds, and a `Ready`
	// one owes its caller a collect. Neither survives having the dispatcher changed underneath it.
	assert(state_.load(std::memory_order_acquire) == State::Idle
	       && "Listing::set_dispatch on a seam that is not Idle -- the pending op belongs to the old "
	          "dispatcher");
	dispatch_ = dispatch;
}

void Listing::trampoline(void* self) {
	static_cast<Listing*>(self)->perform();
}

void Listing::perform() {
	ListingResult out{};
	out.action = request_.action;
	out.error = run_action(out);
	out.dir = FileCatalogue::currentDir;
	out.count = FileCatalogue::fileItems.size();
	result_ = std::move(out);

	// Raised from the op, i.e. on the owner fiber on Embassy -- the same context legacy calls
	// `display->removeLoadingAnimation()` and `display->displayError()` from
	// (`browser.cpp:140`, `:148`). `NoticeQueue` documents itself as single-logical-producer, serialised
	// by cooperative scheduling (`notify/notice_queue.h`, COOP-WRITE), which is what the storage
	// fiber is.
	notify::notice_kind(notify::Notice::Kind::ClearWorking);
	if (result_.error != Error::NONE && announces_errors(result_.action)) {
		notify::notice_error_code(static_cast<int16_t>(result_.error));
	}

	// Last, and with release: a poller that sees `Ready` must see `result_` and the catalogue the
	// op left behind. See the honesty note in listing.h -- nothing has ever exercised this.
	state_.store(State::Ready, std::memory_order_release);
}

Error Listing::run_action(ListingResult& out) {
	switch (request_.action) {

	case ListingAction::Open: {
		// `openListingImpl` (`browser.cpp:349`) lists whatever `currentDir` already holds; the seam
		// lets a caller set it in the same request rather than reaching into the catalogue first.
		if (!request_.path.empty()) {
			FileCatalogue::currentDir = request_.path;
		}
		return list_current_dir(request_.filename_to_start_at.empty() ? nullptr
		                                                              : request_.filename_to_start_at.c_str());
	}

	case ListingAction::IntoFolder: {
		// `goIntoFolderImpl` (`browser.cpp:1339`): append the name, separator only when there is
		// something to separate from.
		// A malformed request, not a card condition: appending nothing would silently re-list the
		// directory the caller thought it was leaving.
		if (request_.path.empty()) {
			return Error::BUG;
		}
		if (!FileCatalogue::currentDir.empty()) {
			FileCatalogue::currentDir.append("/");
		}
		FileCatalogue::currentDir.append(request_.path);
		return list_current_dir(nullptr);
	}

	case ListingAction::UpLevel: {
		// `goUpOneDirectoryLevelImpl` (`browser.cpp:1379`). The refusal at a single-segment
		// directory is legacy's, and it is a *level* test, not an emptiness test: legacy takes
		// `strrchr(currentDir, '/')` and refuses both when there is no slash and when the slash is
		// the first character, so "SETTINGS" cannot be ascended out of but "SAMPLES/KICKS" can.
		const size_t slash = FileCatalogue::currentDir.rfind('/');
		if (slash == std::string::npos || slash == 0) {
			return Error::NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP;
		}
		// Legacy assigns the dropped segment to `enteredText` and then lists starting at it, so the
		// folder just left is the one selected on arrival.
		out.entered_text = FileCatalogue::currentDir.substr(slash + 1);
		FileCatalogue::currentDir.resize(slash);
		return list_current_dir(out.entered_text.c_str());
	}

	case ListingAction::ByPath: {
		// `setFileByFullPathImpl` (`browser.cpp:235`).
		// TODO(conflation-batch-4): legacy treats an unknown `fileExists` as absent here and carries
		// the same marker; the load/browse/favourites batch gives that site its ruling, and this
		// port inherits whatever it decides.
		if (!StorageManager::fileExists(request_.path.c_str()).value_or(false)) {
			return Error::FILE_NOT_FOUND;
		}
		const size_t slash = request_.path.rfind('/');
		if (slash == std::string::npos) {
			out.entered_text = request_.path;
			FileCatalogue::currentDir.clear();
		}
		else {
			out.entered_text = request_.path.substr(slash + 1);
			// Assigned from a substring of the request's own string, not from a `char const*` into
			// a temporary -- legacy has a comment here about exactly that dangling pointer
			// (`browser.cpp:246-250`).
			FileCatalogue::currentDir = request_.path.substr(0, slash);
		}

		const Error error = list_current_dir(out.entered_text.c_str());
		if (error != Error::NONE) {
			return error;
		}

		// `searchFileItems` reads the name comparator's globals, which `sortFileItems` has just
		// pinned for this listing (`file_catalogue.cpp:398 sortFileItems`). When the catalogue came
		// back empty the sort did not run, but the search then has nothing to compare against and
		// returns 0 regardless -- which is why this file never touches `util/name_compare`'s flags.
		out.file_index = FileCatalogue::searchFileItems(out.entered_text.c_str(), &out.found_exact);
		// `>` and not `>=`, copied from legacy (`browser.cpp:264`) rather than corrected. The
		// consequence is real and is NOT a divergence hidden by omission: `searchFileItems` returns
		// the match index *or the insertion point*, and an insertion point may be exactly
		// `fileItems.size()`, one past the end -- which this check lets through with `Error::NONE`.
		// Legacy survives it because `clampFileSelectionAndScroll` (`browser.cpp:1009`) reins
		// `fileIndexSelected` in before anything dereferences it. This port does not silently
		// re-decide legacy's comparisons, so the check stays and the hazard is pushed to the
		// caller, spelled out on `ListingResult::file_index`: index the catalogue with it only
		// after checking `found_exact`.
		if (out.file_index > static_cast<int32_t>(FileCatalogue::fileItems.size())) {
			return Error::FILE_NOT_FOUND;
		}
		return Error::NONE;
	}

	case ListingAction::Reload: {
		// `reloadImpl` (`browser.cpp:735`), both arms: with a name to centre on it re-reads around
		// that name (legacy's `CATALOG_SEARCH_BOTH` on `enteredText`), and without one it re-reads
		// from whichever end `search_direction` names -- legacy's `pendingReloadSearchFromEnd_`
		// pair (`browser.h:178`). Choosing the new selection index from the reloaded list is the
		// caller's (`finishSelectEncoderAction`, `browser.cpp:702`), not the seam's.
		return list_current_dir(request_.filename_to_start_at.empty() ? nullptr
		                                                              : request_.filename_to_start_at.c_str());
	}
	}
	return Error::BUG;
}

Error Listing::list_current_dir(char const* filename_to_start_at) {
	// `arrivedInNewFolder`'s own rule (`browser.cpp:379`): a name to start at means a two-sided
	// cull around it, no name means keep the start of the folder. `search_direction` overrides it
	// only where legacy has its own override -- Reload's search-from-end arm.
	const int32_t direction = request_.search_direction != kDeriveSearchDirection ? request_.search_direction
	                          : (filename_to_start_at != nullptr && *filename_to_start_at != '\0')
	                              ? CATALOG_SEARCH_BOTH
	                              : CATALOG_SEARCH_RIGHT;

	Error error = FileCatalogue::readFileItemsForFolder(nullptr, request_.allow_folders, request_.allowed_extensions,
	                                                    filename_to_start_at, request_.max_num_file_items, direction,
	                                                    request_.interpret_note_names);

	// The default-directory retry (`browser.cpp:293-300`), once. Legacy's third arm -- `mkdir` the
	// default directory and try a third time (`:289`) -- is deliberately not ported: see listing.h.
	//
	// **One deliberate divergence.** Legacy gates this on `error == Error::FOLDER_DOESNT_EXIST`
	// alone (`browser.cpp:293`), and that condition is dead: since the FatFS-to-`deluge::io`
	// refactor, a missing directory comes back from `Directory::open` as `DELUGE_ERR_NOT_FOUND`,
	// which `delugeStatusToError` maps to `Error::FILE_NOT_FOUND` (`util/functions.cpp:1879-1880`).
	// `Error::FOLDER_DOESNT_EXIST` is produced in exactly one place in the tree
	// (`storage/storage_manager.cpp:116`), which no listing reaches. So legacy's fallback -- and the
	// mkdir arm behind it -- can no longer fire at all. Porting the condition verbatim would have
	// shipped a fallback that is green in review and dead in the field; this accepts both codes, and
	// `listing_spec.cpp` exercises it.
	if ((error == Error::FOLDER_DOESNT_EXIST || error == Error::FILE_NOT_FOUND) && !request_.default_dir.empty()
	    && !caseless_equals(FileCatalogue::currentDir, request_.default_dir)) {
		FileCatalogue::currentDir = request_.default_dir;
		error = FileCatalogue::readFileItemsForFolder(nullptr, request_.allow_folders, request_.allowed_extensions,
		                                              nullptr, request_.max_num_file_items, CATALOG_SEARCH_RIGHT,
		                                              request_.interpret_note_names);
	}

	if (error != Error::NONE) {
		return error;
	}

	// The tail of `readFileItemsFromFolderAndMemory` (`browser.cpp:330-340`), minus the in-memory
	// instrument merge that needs a `Song` -- see listing.h.
	if (!FileCatalogue::fileItems.empty()) {
		FileCatalogue::sortFileItems(request_.interpret_note_names);
		if (!request_.allow_folders_sharing_name_with_file) {
			FileCatalogue::deleteFolderAndDuplicateItems(Availability::ANY);
		}
	}
	return Error::NONE;
}

} // namespace deluge::scenes::browser
