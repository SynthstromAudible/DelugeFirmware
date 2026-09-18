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
#include "storage/audio/async_sample_load.h"
#include "storage/audio/audio_load_context.h"
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

	/// @brief Whether a load is actually in flight.
	///
	/// NOT the same question as isLoadingSong(), which is true merely because the load UI is showing.
	/// Five of that predicate's six callers want exactly that, so it is left alone. This is the flag
	/// queueLoadNextSongIfAvailable() already guards on; it is exposed here, not invented.
	[[nodiscard]] bool loadInFlight() const { return performingLoad; }

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
	/// Off the storage owner after boot it never blocks and never reads the card on the calling tier: the
	/// bytes are asked for through the prefix channel and collected by @ref graphicsRoutine on a later
	/// tick, so the render lands later than this call. On the owner, or before it starts, the file is
	/// read inline instead.
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
	/// way a parked direction can outlive the render that was going to consume it: a dropped deferral means the collect
	/// never runs -- while @ref currentFileChanged parks a direction on EVERY move onto a song, so one is parked for
	/// the whole preview window. Left behind it survives into the next visit (there is one LoadSongUI, and @ref opened
	/// cancels), where the first render's @ref finishPreviewRender would start a horizontal scroll on top of the
	/// vertical scroll-in and abandon it half-drawn.
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

	/// @brief The inline arm's render: read @p target's file, blocking, and draw it. Only on the storage
	///        owner, or before it starts.
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
	/// Kept deliberately, and not only as diagnostics: it is the song-preview gate's comparison point. The
	/// channel must agree with the file read on the OUTCOME as well as on the pads, and a gate has no
	/// other way to see that -- "unreadable song", "song with no artwork" and "preview past the read
	/// window" all draw an identical blank screen.
	/// @{
	deluge::storage::SongPreviewOutcome lastPreviewOutcome_{deluge::storage::SongPreviewOutcome::NoPreview};
	Error lastPreviewError_{Error::NONE};
	/// @}

	/// The host song-preview gate, which drives the real drawSongPreview()/graphicsRoutine() pair and holds
	/// the two members above to a reference decode. See src/deluge/harness/song_preview_ab_scenario.cpp.
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

	/// @brief Consecutive collects that found the current preview's read unanswered (UNKNOWN or PENDING), and the
	///        attempt they were counted for.
	///
	/// At @ref kMaxPreviewWaitTicks the preview gives up with the card error: the pads cannot stay scrolled out for
	/// as long as a card takes not to answer. A different attempt, or an answered read, starts the count again.
	/// @{
	int32_t previewWaitTicks_{0};
	uint32_t previewWaitGeneration_{0};
	std::string previewWaitPath_;
	/// @}

	/// @brief How many unanswered collects a preview is given: the bound every channel consumer on this ladder uses.
	static constexpr int32_t kMaxPreviewWaitTicks = 600;

	/// @brief Set up and start the scroll-in from `imageStore`. Call only once it is filled.
	void beginPreviewScrollIn(int32_t movementDirection);

	/// @brief Where an in-flight load is parked between run-loop passes.
	///
	/// Only the two waits can span passes; the storage work itself runs straight through inside whichever
	/// stretch is on the owner. RUNNING covers a stretch that is queued or executing, and keeps
	/// @ref loadRoutine from dispatching a second one on top of it.
	enum class LoadPhase : uint8_t {
		NONE,      ///< No load in flight.
		STAGE,     ///< Reading the song into memory, OFF the owner, before any of it is parsed.
		BEGIN,     ///< Parsing the staged song and claiming samples from RAM, on the interaction tier.
		ESSENTIAL, ///< Loading the samples the swap needs to sound, a tick at a time.
		ARM,       ///< Arming the swap, or taking it outright when nothing is playing.
		FINISH,    ///< The swap is done: freeing the old song and finishing the last sample pass.
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

	/// @brief The samples the swap needs to sound, loading on the interaction tier.
	///
	/// Engaged for the length of the ESSENTIAL phase and empty otherwise. Its root is `preLoadedSong`, which
	/// outlives the phase.
	std::optional<deluge::storage::AsyncSampleLoad> essentialLoad_{};

	/// @brief Whether @ref essentialLoad_ is the crucial-only walk, which the load trace brackets.
	bool essentialIsCrucial_{false};

	/// @brief The samples the swap did NOT need, loading on the interaction tier.
	///
	/// A member rather than a phase because it outlives the one that starts it: engaged as the arm wait begins,
	/// still walking while the user decides, and finished by FINISH rather than restarted there. Its root is
	/// `preLoadedSong`, which the swap promotes to `currentSong` without moving it -- so the swap landing
	/// mid-walk is safe, and is the point. Never the outgoing song, which FINISH frees.
	std::optional<deluge::storage::AsyncSampleLoad> restLoad_{};

	/// @brief The load's sample context: the song's own folder, where its collected samples may be, and what the
	///        load has learnt about that folder. Made once the song is read; every sample pass of the load uses it.
	deluge::storage::AudioLoadContext sampleLoadContext_{};

	/// @brief The song's bytes, held from STAGE until the deserializer is finished in startLoad().
	///
	/// A member and not a local because the fill spans ticks: STAGE runs on the interaction tier and
	/// returns to the run loop between attempts, which is the only reason the Rust driver can make
	/// progress at all. The walk also NUL-terminates inside these bytes, so they must outlive the
	/// PARSE and not merely the open -- see StorageManager::openDelugeBuffer().
	StorageManager::StagingState songStaging_{};

	/// @brief The path STAGE is filling from, pinned once.
	///
	/// The browser's list is rebuilt by other tiers, so re-reading the cursor on a later tick could
	/// name a different song than the one being staged -- or nothing at all. Outlives
	/// releaseSongStaging(), because startLoad() derives the song's filename from it; endLoad() clears it.
	std::string stagingPath_{};

	/// @brief `activeDeserializer` as it was before STAGE pointed it at the staged song.
	///
	/// Restored wherever the load ends, because the memory reader's `fileClusterBuffer` IS a buffer
	/// this class frees, and code downstream writes 32 KiB through that global without opening a
	/// document of its own.
	FileDeserializer* stagingPrevious_{nullptr};

	/// @brief Consecutive ticks STAGE has waited for the browser's listing. Bounded.
	int32_t stagingWaitTicks_{};

	/// @brief Completion handed to @ref beginLoad, cleared as it runs.
	void (*loadOnComplete_)(){nullptr};

	/// @name The three stretches of storage work, split at the two waits. Owner-only.
	/// @{
	/// @brief Parse the staged song and claim its samples from RAM, then park in ESSENTIAL.
	///
	/// Runs where its caller stands: the parse is over `songStaging_`'s bytes and the claim pass never reads the
	/// card, so nothing here needs the storage owner.
	void runBeginSegment();

	/// @brief Advance @ref essentialLoad_ one walk. Ends the phase, and the load's first stretch, when it ends.
	void tickEssential();
	/// @}

	/// @brief Fill @ref songStaging_ off the owner, a tick at a time. See its definition.
	void runStageSegment();

	/// @brief Free @ref songStaging_'s bytes. Safe to call twice.
	void releaseSongStaging();

	/// @name Owner-only, continued
	/// @{
	/// @brief Name the song, capture the outgoing one, and arm the swap -- or take it now, if nothing is playing.
	///
	/// Card-free, so it runs where its caller stands. Engages @ref restLoad_ on its way into the arm wait.
	void runArmSegment();

	/// @brief Free the outgoing song and drop temp samples, then park in FINISH to finish the last pass.
	void runFinishSegment();

	/// @brief Advance @ref restLoad_ one walk, if one is engaged.
	void tickRest();

	/// @brief Finish the last sample pass, then restore the UI and end the load.
	void tickFinish();
	/// @}

	/// @name `Owner::run` trampolines for the three stretches above.
	/// @{

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
