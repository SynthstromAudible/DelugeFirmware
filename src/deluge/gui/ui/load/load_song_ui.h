/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#pragma once

#include "gui/ui/load/load_ui.h"
#include "hid/button.h"
#include "storage/deferred_load.h"
#include "storage/song_preview.h" // SongPreviewOutcome, held as the last render's result

#include <cstdint>
#include <string>

class LoadSongUI final : public LoadUI {
public:
	LoadSongUI();
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult timerCallback() override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;

	/// @brief Per-tick surface for the song preview: re-asks for one that has not landed yet.
	///
	/// This is the ONLY thing that finishes a deferred preview. @ref drawSongPreview is reached from
	/// exactly two places -- arriving in a folder and moving the cursor -- so with no re-drive a prefix
	/// that is still being read is never asked for again and the pads keep the previous song's artwork
	/// until the user happens to scroll back onto that entry.
	void browserGraphicsRoutine() override;
	void scrollFinished() override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	bool opened() override;
	void selectEncoderAction(int8_t offset) override;
	void queueLoadNextSongIfAvailable(int8_t offset);

	/// @brief Starts loading the highlighted song and returns without waiting for it to finish.
	///
	/// The load runs as three stretches of storage work on the storage owner, separated by the two points
	/// it has to wait at: the sample loader settling, and the user arming the song swap. Rather than
	/// blocking across those, each stretch ends by parking the load in a phase; @ref loadRoutine observes
	/// the phase from the run loop and dispatches the next stretch once the wait is over.
	///
	/// @param onComplete Run once the load has finished, whether it succeeded, failed, or never started.
	///                   Runs on the storage owner. Null for callers with no follow-up work. Must outlive
	///                   the load, so pass a free function or a captureless lambda.
	void beginLoad(void (*onComplete)() = nullptr);

	/// @brief Advances an in-flight load past a wait it has finished. Cheap no-op when none is in flight.
	///
	/// Registered as a repeating task by registerTasks(), so every entry point - both browser gestures,
	/// the startup song, and the host harness drivers - is driven from one place.
	void loadRoutine();

	/// @brief True from @ref beginLoad until the load's completion has run.
	[[nodiscard]] bool isLoadInFlight() const { return performingLoad; }

	void displayLoopsRemainingPopup();
	bool isLoadingSong();

	bool deletedPartsOfOldSong{};

	// ui
	UIType getUIType() override { return UIType::LOAD_SONG; }
	bool canDisplayFavourites() override { return true; }

protected:
	void displayText(bool blinkImmediately = false) override;
	void enterKeyPress() override;
	void folderContentsReady(int32_t entryDirection) override;
	void currentFileChanged(int32_t movementDirection) override;
	void exitAction() override;
	ActionResult backButtonAction() override;
	void onBrowserOpened() override;

private:
	/// @brief Snapshot of the song to preview, captured at request time.
	///
	/// Both fields have to travel with the request. The path, because the selection can move on before
	/// the bytes arrive; `toStore`, because the tick surface that re-asks knows nothing about the
	/// scroll-in the cursor move set up, and painting the live image instead of the store would leave
	/// that scroll reading an unfilled buffer.
	struct PreviewTarget {
		std::string path;
		bool toStore;
	};

	/// @brief Ask for the highlighted song's saved pad layout, and draw it if it is already to hand.
	///
	/// Never blocks and never reads the card on the calling tier. The bytes are asked for through the
	/// prefix channel and collected by @ref graphicsRoutine on a later tick; where that channel is not
	/// backed (@ref deluge_file_prefix_async_active false) the read is dispatched onto the storage
	/// owner instead. Either way the render lands later than this call.
	///
	/// @param toStore true to render into PadLEDs::imageStore (behind a scroll), false for the live image.
	/// @return true if `imageStore` is filled and a scroll may start from it now. False means the fill
	///         is still owed, and the caller must park its scroll direction in
	///         @ref pendingScrollDirection_ rather than scrolling out of an unfilled store.
	[[nodiscard]] bool drawSongPreview(bool toStore = true);

	/// @brief Abandon any preview in progress: nothing that is being read is wanted any more.
	///
	/// For the moves that leave nothing to preview at all -- onto a folder or an empty selection, out
	/// of the browser, or up a directory. None of them request a replacement, so ordinary latest-wins
	/// supersession never fires; without this, the prefix already asked for arrives and paints the
	/// song the cursor has LEFT over whatever replaced it, or after the browser is gone.
	///
	/// Cancelling strands nothing. It abandons the RESULT, never the read: the prefix channel is
	/// single-flight and the next request supersedes whatever is outstanding.
	///
	/// It also drops @ref pendingScrollDirection_, and that half is not tidiness. A cancel is the one
	/// way a parked direction can outlive the render that was going to consume it: the owner op skips
	/// @ref finishPreviewRender for a stale attempt, and a dropped deferral means the collect never
	/// runs -- while @ref currentFileChanged parks a direction on EVERY move onto a song, so one is
	/// parked for the whole preview window. Left behind it survives into the next visit (there is one
	/// LoadSongUI, and @ref opened cancels), where the first render's @ref finishPreviewRender would
	/// start a horizontal scroll on top of the vertical scroll-in and abandon it half-drawn.
	void cancelPreview() {
		previewLoad_.cancel();
		pendingScrollDirection_ = 0;
	}

	/// @brief Start work on @p target, or queue it behind work already running (latest-wins).
	void dispatchPreview(const PreviewTarget& target);

	/// @brief Drive the current attempt one step, and hand off to a newer target if one queued.
	///
	/// Shared by the user-driven entry (@ref drawSongPreview) and the re-ask (@ref graphicsRoutine),
	/// so both take the same single-flight path.
	void drivePreview();

	/// @brief Collect @p attempt's prefix and, if it has landed, decode and draw it.
	/// @return true if the bytes are not here yet and the target must be asked for again.
	[[nodiscard]] bool collectPreview(const deluge::storage::DeferredLoad<PreviewTarget>::Attempt& attempt);

	/// @brief The storage-owner op behind the synchronous backing: read the file and render inline.
	/// @param self The LoadSongUI instance.
	static void runPreviewOp(void* self);

	/// @brief The synchronous backing's render: read @p target's file and draw it. Owner-only.
	void renderPreviewFromFile(const PreviewTarget& target);

	/// @brief Turn a finished decode into what the user sees, and record it. Both backings end here.
	///
	/// One function rather than a branch in each arm, because the two arms disagreeing about this is
	/// exactly the defect that shipped: the file path called `displayError()` for a song it could not
	/// open, and the prefix path could not, because a failed read reached it as an empty prefix
	/// indistinguishable from a song with no artwork. Flipping the selector would have dropped those
	/// popups with nothing to say so -- and no comparison of rendered PIXELS can see it, since both
	/// arms draw the same blank pads either way.
	///
	/// Only ::deluge::storage::SongPreviewOutcome::Error shows anything. NoPreview and Truncated both
	/// draw blank and say nothing: blank is the right picture for a song with no artwork, and a
	/// preview past the read window is a limit of this build rather than something the user did.
	///
	/// @param outcome What the decode found.
	/// @param error   The open failure behind an Error outcome; ignored otherwise.
	void reportPreviewOutcome(deluge::storage::SongPreviewOutcome outcome, Error error);

	/// @brief What the last finished preview render produced, and the error it reported.
	///
	/// Kept deliberately, and not only as diagnostics: it is the A/B gate's comparison point. The two
	/// backings must agree on the OUTCOME as well as on the pads, and an out-of-process gate has no
	/// other way to see that -- "unreadable song", "song with no artwork" and "preview past the read
	/// window" all draw an identical blank screen.
	/// @{
	deluge::storage::SongPreviewOutcome lastPreviewOutcome_{deluge::storage::SongPreviewOutcome::NoPreview};
	Error lastPreviewError_{Error::NONE};
	/// @}

	/// The host A/B gate, which drives the real drawSongPreview()/graphicsRoutine() pair against both
	/// backings and compares the two members above. See src/deluge/harness/song_preview_ab_scenario.cpp.
	friend class SongPreviewAbProbe;

	/// A row-major pad image of the shape both preview decoders write into.
	using PreviewImage = RGB (*)[kDisplayWidth + kSideBarWidth];

	/// @brief Clear the destination image and hand it back, or null if the preview must not draw.
	/// @param toStore As @ref drawSongPreview.
	[[nodiscard]] PreviewImage beginPreviewRender(bool toStore);

	/// @brief Everything that has to happen once a render has actually filled the destination image.
	///
	/// Starts the scroll-in owed to @ref pendingScrollDirection_ and asks for a repaint. Runs even
	/// when nothing was drawn (a blank song, a folder, or the QWERTY overlay suppressing the render):
	/// the scroll direction is owed by the cursor move, not by the artwork, and leaving it parked
	/// wedges the browser mid-scroll.
	void finishPreviewRender();

	void displayArmedPopup();

	/// @brief The one piece of preview state: which song is being read, whether work is out on it,
	///        whether that work deferred, and whether it is still wanted.
	///
	/// Replaces a `Coalescer` plus a hand-rolled `previewPending_` catch-up flag. `Coalescer::request`
	/// DROPS a request made while one is in flight, so scrolling faster than one preview round-trip
	/// left the pads showing an older song; the flag existed to notice that and re-dispatch by hand.
	/// `DeferredLoad` is latest-wins, so the drop cannot happen in the first place.
	deluge::storage::DeferredLoad<PreviewTarget> previewLoad_{};

	/// @brief Scroll direction owed to a preview whose render has not happened yet, or 0 for none.
	///
	/// The scroll-in reads `PadLEDs::imageStore`, so it cannot start until the render has actually filled
	/// it. @ref drawSongPreview never renders inline, so the direction is parked here and
	/// @ref finishPreviewRender starts the scroll once the fill is done. Starting it eagerly is what
	/// left the first two columns showing the PREVIOUS entry: `setupScroll` ticks once itself and
	/// `currentFileChanged` ticked again, so two columns were copied out of an unfilled store.
	int32_t pendingScrollDirection_{0};

	/// @brief Set up and start the scroll-in from `imageStore`. Call only once it is filled.
	void beginPreviewScrollIn(int32_t movementDirection);

	/// @brief Where an in-flight load is parked between run-loop passes.
	///
	/// Only the two waits can span passes; the storage work itself runs straight through inside whichever
	/// stretch is on the owner. RUNNING covers a stretch that is queued or executing, and keeps
	/// @ref loadRoutine from dispatching a second one on top of it.
	enum class LoadPhase : uint8_t {
		NONE,      ///< No load in flight.
		START,     ///< Requested, but the owner queue was full - waiting to be dispatched.
		RUNNING,   ///< A stretch is queued on, or running on, the storage owner.
		SETTLE,    ///< Waiting for the sample loader to settle, or for its deadline to pass.
		AWAIT_ARM, ///< Waiting for the user to arm the swap, or for the arming to launch it.
	};
	LoadPhase loadPhase_{LoadPhase::NONE};

	/// @brief The outgoing song, captured before the swap and freed in @ref finishLoad.
	///
	/// The one piece of the load's state that has to survive a wait, so the only local promoted to a
	/// member when the function was split.
	Song* loadToDelete_{nullptr};

	/// @brief getSystemTime() value past which SETTLE stops waiting and swaps anyway.
	double settleDeadline_{0.};

	/// @brief Completion handed to @ref beginLoad, cleared as it runs.
	void (*loadOnComplete_)(){nullptr};

	/// @name The three stretches of storage work, split at the two waits. Owner-only.
	/// @{
	void startLoad();  ///< Open, read and claim samples, then park in SETTLE.
	void armOrSwap();  ///< Arm the swap and park in AWAIT_ARM, or swap outright and carry straight on.
	void finishLoad(); ///< Free the old song, finish loading, restore the UI, run the completion.
	/// @}

	/// @name `Owner::run` trampolines for the three stretches above.
	/// @{
	static void startLoadTrampoline(void*);
	static void armOrSwapTrampoline(void*);
	static void finishLoadTrampoline(void*);
	/// @}

	/// @brief Queues the next stretch on the storage owner, leaving the phase alone if the queue is full.
	/// @param segment One of the trampolines above.
	void dispatchLoadSegment(void (*segment)(void*));

	/// @brief Ends the load: clears the phase and runs the caller's completion exactly once.
	void endLoad();

	bool performingLoad;
	bool scrollingIntoSlot{};
	bool qwertyCurrentlyDrawnOnscreen;
	void doQueueLoadNextSongIfAvailable(int8_t offset);
	// int32_t findNextFile(int32_t offset);
	void exitThisUI();
	void exitActionWithError();
};
extern LoadSongUI loadSongUI;

extern char loopsRemainingText[];
