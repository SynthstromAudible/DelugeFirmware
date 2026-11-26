#pragma once

/// @file
/// @brief `Listing` -- the async seam between a tick-driven scene and a file listing run through
///        a dispatcher.
///
/// **The problem.** Legacy runs a listing as an op that calls the UI back from inside itself:
/// `Browser::beginListing` stores a request, sets `listingInProgress_` and shows a loading
/// animation; `Browser::runPendingListing` then switches on a `ListingAction` -- Open, IntoFolder,
/// UpLevel, ByPath, Reload -- and calls the UI back from inside the op. The new shell is
/// immediate-mode and tick-driven, so a scene cannot sit inside that call: it has to render
/// something while the listing is in flight and repaint when it lands.
///
/// **The shape.** Request / poll / collect. A scene calls `request()` once, renders whatever it
/// wants while `in_flight()` is true, and calls `take_result()` on a later tick. The listing runs
/// where the seam's dispatcher puts it -- the shipped one, `deluge::ui::scenes::run_inline`, runs it on
/// the interaction tier's own stack, where legacy runs it too -- and writes into the card's one
/// catalogue through the catalogue port (`bind::Catalogue`), which is where the scene reads its
/// entries from afterwards.
///
/// **The busy indicator is not invented here.** `request()` raises a `notify::Notice::Kind::Working`
/// and completion raises `Kind::ClearWorking`; the shell's `Notices` (`ui/shell/notices.cpp`)
/// turns those into pushing and removing `SceneKind::Working`. A failed listing raises
/// `notice_error_code`, which `Notices` turns into the same popup legacy's
/// `Browser::onListingFailed` shows via `display->displayError`.
/// This file therefore reaches nothing in `ui/shell/` directly -- only `base/notify/`.
///
/// @warning **`ClearWorking` is global and unrefcounted, and that is already live.**
///          `Notices::drain`'s `Kind::ClearWorking` case calls `working_.clear()` unconditionally
///          (`Notices::drain`) -- there is one `WorkingScene` and no count of who armed
///          it. So if a listing overlaps with any other `Kind::Working` raiser, whichever finishes
///          first takes the other's animation down. This is not a hypothetical future collision:
///          `PlaybackHandler::doSongSwap` raises one today through the same queue
///          (via `notice_working`, `util/notify.h`), and a
///          song swap can be in progress while a browser lists. The seam does not fix it -- a fix
///          is a refcount or an owner token on `WorkingScene` -- but nothing
///          here should be read as assuming the animation it raised is the animation it clears.
///
/// @par Delivery contract (read this before using the seam)
///
/// This is **not** the one-slot mailbox with a silent overwrite that `MenuScene::take_action()`
/// (`ui/scenes/menu/menu_scene.h`) is, and the difference is deliberate. That mailbox carries a
/// *request the caller may repeat*; this one guards a *process-global resource*. There is exactly
/// one catalogue -- one list of entries, one folder, one cull window -- so two
/// listings running over it would not merely lose a request, they would interleave two directory
/// reads into one vector. Legacy says the same thing with `listingInProgress_` and the comment at
/// `browser.h` ("One active listing at a time (browser state is already static/shared, e.g.
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
/// ...and the `Idle` row has one more condition on it: **the guard is process-wide, not per
/// instance.** An `Idle` seam is still refused (`Busy`) while any *other* `Listing` owns the
/// catalogue. See "Single flight is PROCESS-WIDE" on the class below for why that has to be so.
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
/// @note A scene should refuse input of its own accord while `in_flight()` is true, so a second
///       press cannot even reach the refusal above -- the refusal is the backstop, not the policy.
///       The scene's own `handle()` is where that belongs: `SceneStack::input_top()` routes every
///       event to the topmost non-input-transparent scene, so swallowing them there is a complete
///       gate.
///
/// @warning **A scene must not hold the shell's input lock (`Action::Kind::LockInput`) as legacy's
///          `waitingForSDRoutineToEnd` gate.** A scene cannot release that lock from `on_exit()`,
///          which returns `void`, so an external `Pop` while a listing was in flight would leave
///          the entire UI deaf. And `LockInput` is not
///          `waitingForSDRoutineToEnd` -- it is the stem-export/song-load bracket, which **drops**
///          events (encoder edges included, `InputLowering::on_encoder_edges`) where the legacy gate
///          defers and retries. The port of the legacy gate is `InputLowering::set_sd_busy`, which
///          deliberately has no `Action` behind it. See the warning on `Action` in
///          `ui/shell/scene.h`.
///
/// @par What this seam does NOT do
///
/// It asks the catalogue port for the **SD half** of legacy's `Browser::arrivedInNewFolder` -- the
/// read, the default-directory retry, creating a missing default directory, the sort and the
/// de-duplication -- and stops there. Two things legacy
/// does around it are deliberately absent:
///
/// 1. **The in-memory instrument merge.** `readFileItemsFromFolderAndMemory` calls
///    `song->addInstrumentsToFileItems(outputType)`. That needs a `Song`, and
///    `src/ui/scenes` may not name `currentSong` (`scripts/check_layer_rules.py`,
///    `scenes-currentSong`). The scene that owns a `MenuContext` has a `Song&` and can do the merge
///    itself between `take_result()` and its next render; the seam takes no `Song` so it cannot be
///    the place that quietly reintroduces the global.
/// 2. **Selection, scroll and text state.** `fileIndexSelected`, `scrollPosVertical`, `enteredText`
///    and the `renderUIsForOled()` calls stay with the scene. `ListingResult` carries the two facts
///    a caller cannot re-derive (`entered_text`, `file_index`) and nothing else.
///
/// @par Honesty about what a host spec can prove
///
/// **The shipped dispatcher is inline.** `deluge::ui::scenes::run_inline` (`ui/scenes/run_inline.h`)
/// runs the op on the caller's stack before returning, so with the default dispatcher `request()`
/// returns with the state already `Ready` and no poll ever observes `InFlight`.
/// `tests/integration/ui/scenes/listing_spec.cpp` therefore installs its own deferring dispatcher (see
/// `Dispatch` below) to exercise the state machine's ordering: dispatch returns, the caller polls,
/// the op runs later. That models the *ordering* a deferred dispatch gives, and nothing else. **No
/// spec exercises concurrency across this seam: not preemption, not memory visibility between two
/// contexts, not a dispatch queue under contention. A green suite is not evidence that the seam is
/// race-free under a dispatcher that runs the op in another context.** The `std::atomic` state
/// below is written for that case regardless; nothing has put it under load.

#include "ui/scenes/bind/ports/catalogue.h"
#include "ui/scenes/run_inline.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace deluge::ui::scenes::browser {

/// @brief One completed listing: what the catalogue port answered, and the folder and count it left.
///
/// The entries themselves are **not** copied: they stay in the catalogue port (`bind::Catalogue`), and `count` is
/// only how many it held when the op finished.
struct ListingResult {
	/// Which step produced this, so a caller draining one slot can tell what landed.
	bind::ListingStep step = bind::ListingStep::OPEN;
	/// The error the listing failed with, as `base::notify::notice_error_code` takes it; empty on success.
	std::optional<int16_t> error;
	/// The catalogue's folder as the listing left it.
	std::string dir;
	/// `UP_LEVEL`: the folder left, which the caller selects. `BY_PATH`: the file's name. Empty otherwise.
	std::string entered_text;
	/// How many entries the catalogue held when the op finished.
	size_t count = 0;
};

/// @brief What `Listing::request()` did with a request.
enum class RequestOutcome : uint8_t {
	/// Dispatched. Exactly one `ListingResult` will follow.
	Accepted,
	/// Refused: a listing is already in flight -- **this seam's or another `Listing`'s**, since the
	/// guard is on the shared catalogue rather than on the object. The running one is
	/// untouched; retry later.
	Busy,
	/// Refused: a completed result has not been collected. Call `take_result()` first.
	ResultPending,
	/// The dispatcher dropped the dispatch (it returned false, which `run_inline` never does).
	/// Nothing will run and nothing was left in flight -- the seam is `Idle` again, so an immediate
	/// retry is legitimate.
	Dropped,
};

/// @brief The request/poll seam itself. One per scene that lists files.
///
/// @par Single flight lives on the RESOURCE, not on this class
///
/// The guard is the catalogue port's `claim`/`release` (`bind::Catalogue`), not a member and not
/// even a static of this class. Two reasons, and the second is the one that made it move:
///
/// 1. The file browser, the sample marker editor and the audio recorder each own one of these,
///    so several `Listing`s exist at once over one catalogue.
/// 2. **Legacy claims through the same call.** `Browser::beginListing`
///    guards with its own static `listingInProgress_`
///    (`browser.h`), which only ever guarded `Browser` against `Browser`. For as long as the
///    legacy UI and the scenes stack coexist, a legacy
///    browser listing and a scenes listing were mutually invisible over one shared `fileItems`.
///    Both now claim the catalogue itself, so the invariant is structural rather than a convention
///    two files have to keep agreeing on.
///
/// So a `request()` is refused with `Busy` while *anything* holds the catalogue: this seam, another
/// seam, or a legacy browser mid-listing.
///
/// The claim is released when the result is **collected**, not when the op finishes. That is deliberate:
/// `ListingResult` does not copy the entries, it counts them -- the entries are in the catalogue port, and the
/// collector still has to read them. Anything allowed to dispatch in that window would replace the vector under a
/// caller that had just been told its listing had landed. Holding it until the collect is the *same* contract as a
/// consumer that reads inside the op: the claim lasts until the consumer has read. `bind::Catalogue::claim` states the
/// shared contract in one place.
///
/// @par Lifetime -- READ THIS BEFORE PUTTING ONE ANYWHERE BUT A SCENE MEMBER
///
/// **A `Listing` must outlive every dispatch it makes.** A deferred op holds `this`; it writes
/// `result_`, raises notices and stores `Ready` when it eventually runs. A `Listing` destroyed
/// while `in_flight()` therefore leaves the dispatcher a dangling pointer, and there is **no
/// cancel**: an op already handed to the dispatcher cannot be withdrawn.
///
/// The rule that makes this satisfiable rather than merely stated: **own a `Listing` as a member of
/// a scene, and nothing else.** Scenes are long-lived singletons owned by the application and are
/// never destroyed while the shell is running (`ui/shell/scene.h`); a scene being *popped* does not
/// destroy it, so a listing outliving its scene's time on the stack is fine. A `Listing` on a
/// stack frame, in a container that reallocates, or owned by anything with a scene's lifetime but
/// not a scene's storage, is the hazard.
///
/// `~Listing` asserts that the state is not `InFlight`, so a Debug build says which object it was
/// rather than corrupting a heap. In a release build the destructor deliberately does **not**
/// release the catalogue claim in that case: a jammed browser is a better outcome than a second
/// listing racing an op that still holds a freed `this`.
///
/// (The alternative -- a heap-owned op context, so the op cannot outlive its target -- was
/// rejected. It buys safety for an ownership pattern this layer forbids anyway, at the cost of an
/// allocation per listing in a layer whose whole discipline is that it does not allocate, plus a
/// new failure mode when that allocation fails mid-navigation. The rule plus the assert is the
/// cheaper answer to the same question.)
class Listing {
public:
	/// @brief Where a listing is dispatched: the shape of `deluge::ui::scenes::run_inline`, the shipped
	///        dispatcher.
	///
	/// A function pointer rather than a hardwired call so a spec can install a dispatcher that
	/// *defers* the op -- the only way, with the shipped dispatcher running the op inline, to
	/// observe the in-flight state at all. See the honesty note in the file docstring for what that
	/// does and does not model.
	///
	/// @warning **The contract is exactly-once-on-true, never-on-false** -- what a dispatcher
	///          promises (`ui/scenes/run_inline.h`): true means the op ran or will run exactly once;
	///          false means the dispatch was dropped and the op will NOT run. A dispatcher that returns true
	///          and then never runs the op strands the seam in `InFlight` **forever**: there is no
	///          cancel, no timeout and no escape, and a scene holding `LockInput` for the duration
	///          would take the input lock down with it.
	using Dispatch = bool (*)(void (*)(void*), void*);

	/// @brief Which of the three states the seam is in; see the table in the file docstring.
	enum class State : uint8_t { Idle, InFlight, Ready };

	Listing() = default;
	/// @brief Asserts the seam is not `InFlight` (see the lifetime rule above) and, if it is not,
	///        gives the catalogue claim back so a later listing is not refused forever.
	~Listing();
	/// @brief Construct with a dispatcher other than `deluge::ui::scenes::run_inline`.
	/// @param dispatch The dispatcher; must run the op exactly once when it returns true and never
	///                 when it returns false -- see the `Dispatch` warning above.
	explicit Listing(Dispatch dispatch) : dispatch_(dispatch) {}

	Listing(const Listing&) = delete;
	Listing& operator=(const Listing&) = delete;

	/// @brief Ask for a listing.
	/// @param req What to list; copied.
	/// @return What happened -- see `RequestOutcome`. Only `Accepted` promises a result.
	RequestOutcome request(bind::ListingSpec req);

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

	/// @brief Swap the dispatcher on a seam that already exists.
	///
	/// The constructor overload above is enough for a `Listing` a spec creates itself; a `Listing`
	/// that is a **member of an application scene singleton** is constructed before any spec can
	/// reach it, and the shipped dispatcher is inline (see the honesty note above), so without
	/// this there is no way to observe that scene's in-flight behaviour at all.
	///
	/// @warning Asserts the seam is `Idle`: swapping the dispatcher under a listing that is in
	///          flight, or over an uncollected result, would leave the seam waiting on an op the
	///          new dispatcher never received.
	/// @param dispatch The new dispatcher; same contract as the constructor's.
	void set_dispatch(Dispatch dispatch);

	/// @brief Point the seam at the catalogue it lists into. With none, every `request()` is `Dropped`.
	/// @warning Changing the catalogue asserts the seam is `Idle`, as `set_dispatch` does: a pending op or an
	/// uncollected
	///          result belongs to the catalogue it was made on. Setting the one already set changes nothing.
	/// @param catalogue The catalogue, or null; it must outlive the seam's use of it.
	void set_catalogue(bind::Catalogue* catalogue);

private:
	/// @brief The op handed to the dispatcher; `self` is the `Listing`.
	static void trampoline(void* self);
	/// @brief Runs where the dispatcher runs the op: perform the request, fill `result_`, raise the
	///        completion notices, and publish `Ready`.
	void perform();

	Dispatch dispatch_ = &deluge::ui::scenes::run_inline;
	bind::Catalogue* catalogue_ = nullptr;
	bind::ListingSpec request_{};
	ListingResult result_{};
	/// The seam's whole synchronisation. Written with release by the op, read with acquire by the
	/// poller, so a caller that sees `Ready` sees `result_` and the catalogue the op left behind.
	/// Nothing exercises that across two contexts -- see the file docstring.
	std::atomic<State> state_{State::Idle};
};

} // namespace deluge::ui::scenes::browser
