#pragma once

/// @file
/// @brief `Listing` -- the async seam between a tick-driven scene and the storage owner's file
///        listing.
///
/// **The problem.** Legacy dispatches a listing onto the storage-owner fiber and completes *on*
/// that fiber: `Browser::beginListing` (`gui/ui/browser/browser.cpp:97 beginListing`) stores a
/// request, sets `listingInProgress_`, shows a loading animation and calls
/// `deluge::storage::Owner::run` (`storage/owner.h:43 run`); `Browser::runPendingListing`
/// (`browser.cpp:114 runPendingListing`) then switches on a `ListingAction` -- Open, IntoFolder,
/// UpLevel, ByPath, Reload -- and calls the UI back from inside the op. The new shell is
/// immediate-mode and tick-driven, so a scene cannot sit inside that call: it has to render
/// something while the listing is in flight and repaint when it lands.
///
/// **The shape.** Request / poll / collect. A scene calls `request()` once, renders whatever it
/// wants while `in_flight()` is true, and calls `take_result()` on a later tick. The listing runs
/// where legacy runs it -- on the owner -- and writes into `deluge::storage::FileCatalogue`
/// (`storage/file_catalogue.h`, extracted in task 1 of this plan), which is where the scene reads
/// its entries from afterwards.
///
/// **The busy indicator is not invented here.** `request()` raises a `notify::Notice::Kind::Working`
/// and completion raises `Kind::ClearWorking`; the shell's `Notices` (`shell/notices.cpp:260-269`)
/// turns those into pushing and removing `SceneKind::Working`. A failed listing raises
/// `notice_error_code`, which `Notices` turns into the same popup legacy's
/// `Browser::onListingFailed` (`browser.cpp:147 onListingFailed`) shows via `display->displayError`.
/// This file therefore reaches nothing in `shell/` directly -- only `notify/`.
///
/// @par Delivery contract (read this before using the seam)
///
/// This is **not** the one-slot mailbox with a silent overwrite that `MenuScene::take_action()`
/// (`scenes/menu/menu_scene.h:510 take_action`) is, and the difference is deliberate. That mailbox carries a
/// *request the caller may repeat*; this one guards a *process-global resource*. There is exactly
/// one `FileCatalogue` -- one `fileItems` vector, one `currentDir`, one cull window -- so two
/// listings running over it would not merely lose a request, they would interleave two directory
/// reads into one vector. Legacy says the same thing with `listingInProgress_` and the comment at
/// `browser.h:158` ("One active listing at a time (browser state is already static/shared, e.g.
/// fileItems)").
///
/// So the seam is a three-state machine with **exactly-once delivery**:
///
/// | state | `request()` | `take_result()` |
/// |---|---|---|
/// | `Idle` | dispatches; -> `InFlight` (or stays `Idle` on a dropped dispatch) | `nullopt` |
/// | `InFlight` | **refused**, `Busy` -- the running listing is untouched | `nullopt` |
/// | `Ready` | **refused**, `ResultPending` | hands the result over; -> `Idle` |
///
/// **The second-request case, stated plainly: it is refused, and nothing is lost.** The caller is
/// told which of the two reasons applies (`Busy` vs. `ResultPending`) and can retry on a later
/// tick. No request is ever silently overwritten and no result is ever silently dropped: every
/// accepted request produces exactly one result, and that result must be collected before another
/// request is accepted.
///
/// @warning **The failure mode this trades for is a jam, not a loss.** A caller that requests and
///          never polls leaves the seam in `Ready` forever, and every later request is refused --
///          the listing stops updating. That is deliberate: a stuck listing is visible
///          (`has_result()` stays true, the `Working` scene has already been cleared) and
///          diagnosable, where a silently overwritten one is not. **Poll every tick** -- the
///          natural home is the scene's `after_render()`, beside where `MenuScene` drains its own
///          mailboxes.
///
/// @note A scene should also hold the shell's input lock (`Action::Kind::LockInput`,
///       `shell/scene.h`, legacy's `waitingForSDRoutineToEnd` gate) while `in_flight()` is true, so
///       a second press cannot even reach the refusal. The refusal is the backstop, not the
///       policy.
///
/// @par What this seam does NOT do
///
/// It performs the **SD half** of legacy's `Browser::arrivedInNewFolder` (`browser.cpp:348`) --
/// `FileCatalogue::readFileItemsForFolder`, the default-directory retry, `sortFileItems`,
/// `deleteFolderAndDuplicateItems` -- and stops there. Three things legacy does around it are
/// deliberately absent:
///
/// 1. **The in-memory instrument merge.** `readFileItemsFromFolderAndMemory` calls
///    `song->addInstrumentsToFileItems(outputType)` (`browser.cpp:305`). That needs a `Song`, and
///    `src/scenes` may not name `currentSong` (`scripts/check_layer_rules.py`,
///    `scenes-currentSong`). The scene that owns a `MenuContext` has a `Song&` and can do the merge
///    itself between `take_result()` and its next render; the seam takes no `Song` so it cannot be
///    the place that quietly reintroduces the global.
/// 2. **Creating the default directory.** Legacy `mkdir`s `defaultDirToAlsoTry` when it does not
///    exist (`browser.cpp:289`). A *listing* that writes to the card is a surprise; the seam only
///    falls back to an existing default directory and reports the card's own error otherwise.
///    (The fallback itself has one deliberate divergence from legacy's condition, because legacy's
///    is dead code today -- see `Listing::list_current_dir` in listing.cpp.)
/// 3. **Selection, scroll and text state.** `fileIndexSelected`, `scrollPosVertical`, `enteredText`
///    and the `renderUIsForOled()` calls stay with the scene. `ListingResult` carries the two facts
///    a caller cannot re-derive (`entered_text`, `file_index`) and nothing else.
///
/// @par Honesty about what a host spec can prove
///
/// **On the host, `storage::Owner::run` is inline.** `Owner::on_owner()` is always false there and
/// `run()` executes the op on the caller's stack before returning (`storage/owner.h:55-59`), so
/// with the default dispatcher `request()` returns with the state already `Ready` and no poll ever
/// observes `InFlight`. `sim/scenes_spec/listing_spec.cpp` therefore installs its own deferring
/// dispatcher (see `Dispatch` below) to exercise the state machine's ordering: dispatch returns,
/// the caller polls, the op runs later. That models the *ordering* an Embassy fiber gives, and
/// nothing else. **No spec in this repository exercises real fiber concurrency across this seam:
/// not preemption, not memory visibility between two contexts, not an owner queue under contention.
/// A green suite is not evidence that this is race-free on Embassy.** The `std::atomic` state below
/// is written for that case regardless; it has never been observed doing its job.

#include "definitions_cxx.hpp"
#include "storage/file_catalogue.h"
#include "storage/owner.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace deluge::scenes::browser {

/// @brief The five listings a scene can ask for -- legacy's `Browser::ListingAction`
///        (`gui/ui/browser/browser.h:160`), one for one.
enum class ListingAction : uint8_t {
	/// Legacy `openListingImpl` (`browser.cpp:330`): list `path` (or the current directory when
	/// `path` is empty), optionally starting at `filename_to_start_at`.
	Open,
	/// Legacy `goIntoFolderImpl` (`browser.cpp:1320`): append `path` as one folder name.
	IntoFolder,
	/// Legacy `goUpOneDirectoryLevelImpl` (`browser.cpp:1360`): drop the last segment.
	UpLevel,
	/// Legacy `setFileByFullPathImpl` (`browser.cpp:216`): split `path`, list its directory, and
	/// find the file in the result.
	ByPath,
	/// Legacy `reloadImpl` (`browser.cpp:716`): re-read the current directory in place.
	Reload,
};

/// @brief "Work the search direction out from the request", the default for
///        `ListingRequest::search_direction`.
///
/// Legacy derives it per listing rather than storing it: `arrivedInNewFolder` uses
/// `CATALOG_SEARCH_BOTH` when there is a name to start at and `CATALOG_SEARCH_RIGHT` when there is
/// not (`browser.cpp:361`). Only `Reload`'s search-from-end arm overrides it, which is exactly what
/// legacy's separate `pendingReloadCatalogSearchDirection_` field is for (`browser.h:178`).
inline constexpr int32_t kDeriveSearchDirection = -1;

/// @brief One listing to run, copied whole because the op reads it later, on the owner.
///
/// The `std::string`s are owning copies for the same reason legacy's `Browser::ListingRequest`
/// makes them owning copies (`browser.h:162-168`): on Embassy the caller's buffers are long gone by
/// the time the fiber runs the op.
struct ListingRequest {
	ListingAction action = ListingAction::Open;

	/// `Open`: the directory to list, or empty to keep `FileCatalogue::currentDir`.
	/// `IntoFolder`: the single folder name to descend into.
	/// `ByPath`: the full path of a file.
	/// `UpLevel`/`Reload`: unused.
	std::string path;

	/// The name a `CATALOG_SEARCH_BOTH` cull centres on -- legacy's `filenameToStartAt`. For
	/// `UpLevel` and `ByPath` the seam fills this in itself (the segment left behind, the file
	/// named by the path), so a caller need not.
	std::string filename_to_start_at;

	/// Legacy's `defaultDirToAlsoTry` (`browser.cpp:275`): where to look when `path` does not
	/// exist. Tried at most once, and never created -- see the file docstring.
	std::string default_dir;

	/// NULL-terminated extension list, as `FileCatalogue::readFileItemsForFolder` takes it.
	char const** allowed_extensions = allowedFileExtensionsXML;

	/// Whether directory entries are listed at all (legacy's `allowFolders` argument).
	bool allow_folders = true;

	/// Legacy's `Browser::allowFoldersSharingNameWithFile` (`browser.cpp:317`): when false, a
	/// folder shadowed by a same-named file is dropped after the sort.
	bool allow_folders_sharing_name_with_file = false;

	/// Legacy's `shouldInterpretNoteNamesForThisBrowser` (`browser.h:250`), threaded explicitly --
	/// see `FileCatalogue`'s own note on why these are parameters now.
	bool interpret_note_names = false;

	/// The cap at which the listing starts culling (legacy passes `FILE_ITEMS_MAX_NUM_ELEMENTS`).
	int32_t max_num_file_items = FILE_ITEMS_MAX_NUM_ELEMENTS;

	/// One of the `CATALOG_SEARCH_*` values, or `kDeriveSearchDirection` to let the seam pick.
	int32_t search_direction = kDeriveSearchDirection;
};

/// @brief What one completed listing produced.
///
/// Everything here is a fact about the listing that the caller cannot cheaply re-derive from
/// `FileCatalogue` afterwards. The entries themselves are **not** copied: they are in
/// `FileCatalogue::fileItems`, exactly where legacy leaves them, and `count` is only how many were
/// there when the op finished.
struct ListingResult {
	/// Which request produced this, so a caller draining one slot can tell what landed.
	ListingAction action = ListingAction::Open;
	/// `Error::NONE`, or what the listing failed with.
	Error error = Error::NONE;
	/// `FileCatalogue::currentDir` as the listing left it.
	std::string dir;
	/// `UpLevel`: the segment that was dropped -- legacy assigns it to `enteredText`
	/// (`browser.cpp:1369`). `ByPath`: the filename part of the path (`browser.cpp:231`). Empty
	/// otherwise.
	std::string entered_text;
	/// `ByPath`: `searchFileItems`' answer for `entered_text`; -1 for every other action.
	int32_t file_index = -1;
	/// `ByPath`: whether that search matched exactly rather than returning an insertion point.
	bool found_exact = false;
	/// How many entries the catalogue held when the op finished.
	size_t count = 0;
};

/// @brief What `Listing::request()` did with a request.
enum class RequestOutcome : uint8_t {
	/// Dispatched onto the owner. Exactly one `ListingResult` will follow.
	Accepted,
	/// Refused: a listing is already in flight. The running one is untouched; retry later.
	Busy,
	/// Refused: a completed result has not been collected. Call `take_result()` first.
	ResultPending,
	/// The owner dropped the dispatch (its queue was full, `storage/owner.h:43`). Nothing will
	/// run and nothing was left in flight -- the seam is `Idle` again, so an immediate retry is
	/// legitimate. Legacy handles the same return the same way (`browser.cpp:101-106`).
	Dropped,
};

/// @brief The request/poll seam itself. One per scene that lists files; they must not overlap in
///        time, because they share the one `FileCatalogue`.
class Listing {
public:
	/// @brief Where a listing is dispatched: `storage::Owner::run`'s exact shape.
	///
	/// A function pointer rather than a hardwired call so a spec can install a dispatcher that
	/// *defers* the op -- the only way, on a host where `Owner::run` is inline, to observe the
	/// in-flight state at all. See the honesty note in the file docstring for what that does and
	/// does not model.
	using Dispatch = bool (*)(void (*)(void*), void*);

	/// @brief Which of the three states the seam is in; see the table in the file docstring.
	enum class State : uint8_t { Idle, InFlight, Ready };

	Listing() = default;
	/// @brief Construct with a dispatcher other than `storage::Owner::run`.
	/// @param dispatch The dispatcher; must eventually call the op exactly once, or never.
	explicit Listing(Dispatch dispatch) : dispatch_(dispatch) {}

	Listing(const Listing&) = delete;
	Listing& operator=(const Listing&) = delete;

	/// @brief Ask for a listing.
	/// @param req What to list; copied.
	/// @return What happened -- see `RequestOutcome`. Only `Accepted` promises a result.
	RequestOutcome request(ListingRequest req);

	/// @brief Is a dispatched listing still running?
	/// @return True between an accepted `request()` and the op finishing.
	bool in_flight() const { return state_.load(std::memory_order_acquire) == State::InFlight; }

	/// @brief Is a completed result waiting to be collected?
	/// @return True once the op has finished and until `take_result()` is called.
	bool has_result() const { return state_.load(std::memory_order_acquire) == State::Ready; }

	/// @brief The raw state, for specs and for a caller that wants to log it.
	/// @return The current state.
	State state() const { return state_.load(std::memory_order_acquire); }

	/// @brief Collect the completed result, exactly once, and return the seam to `Idle`.
	/// @return The result, or `std::nullopt` when none has landed.
	std::optional<ListingResult> take_result();

private:
	/// @brief The `Owner::run` C-ABI entry point; `self` is the `Listing`.
	static void trampoline(void* self);
	/// @brief Runs on the owner: perform the request, fill `result_`, raise the completion
	///        notices, and publish `Ready`.
	void perform();
	/// @brief The action switch -- legacy's `runPendingListing` (`browser.cpp:114`) without the UI
	///        callbacks.
	/// @param out The result being filled in; the per-action fields are written here.
	/// @return The listing's error.
	Error run_action(ListingResult& out);
	/// @brief The SD half of `arrivedInNewFolder` (`browser.cpp:348`): read, retry in
	///        `default_dir`, sort, de-duplicate.
	/// @param filename_to_start_at The name a `CATALOG_SEARCH_BOTH` cull centres on; may be null.
	/// @return The listing's error.
	Error list_current_dir(char const* filename_to_start_at);

	Dispatch dispatch_ = &deluge::storage::Owner::run;
	ListingRequest request_{};
	ListingResult result_{};
	/// The seam's whole synchronisation. Written with release on the owner, read with acquire by
	/// the poller, so a caller that sees `Ready` sees `result_` and the catalogue the op left
	/// behind. Never observed doing that on the host -- see the file docstring.
	std::atomic<State> state_{State::Idle};
};

} // namespace deluge::scenes::browser
