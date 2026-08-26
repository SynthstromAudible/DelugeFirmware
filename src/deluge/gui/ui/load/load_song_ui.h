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
#include "storage/owner.h"

class LoadSongUI final : public LoadUI {
public:
	LoadSongUI();
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult timerCallback() override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	void graphicsRoutine() override {}
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
	void onBrowserOpened() override;

private:
	/// @brief Render the highlighted song's saved pad layout onto the grid.
	///
	/// Reads the song file, so it must run on the storage owner. Called from the scroll path (the
	/// interaction tier), where an off-owner read is refused outright, so this dispatches onto the owner
	/// via @ref previewCoalescer_ and returns; the render happens in @ref drawSongPreviewImpl.
	/// @param toStore true to render into PadLEDs::imageStore (behind a scroll), false for the live image.
	/// @return true if the render happened inline (we were already on the owner); false if it was
	///         dispatched, in which case `imageStore` is NOT yet filled and a scroll that reads it must
	///         wait for @ref previewTrampoline.
	[[nodiscard]] bool drawSongPreview(bool toStore = true);

	/// @brief The actual preview read + render. ONLY valid on the storage owner.
	/// @param toStore As @ref drawSongPreview.
	void drawSongPreviewImpl(bool toStore);

	/// @brief `Owner::run` trampoline for @ref drawSongPreviewImpl, using @ref previewToStore_.
	static void previewTrampoline(void* self);

	void displayArmedPopup();

	/// Coalesces scroll-driven preview requests: fast scrolling would otherwise overrun the owner's
	/// 4-deep queue, and only the newest preview is worth rendering anyway.
	deluge::storage::Coalescer previewCoalescer_{};

	/// `toStore` argument for the dispatched @ref drawSongPreviewImpl (the trampoline takes no args).
	bool previewToStore_{true};

	/// Set on every request, cleared as the render begins. Still set when the render finishes means the
	/// selection moved again mid-read and the coalescer dropped that request, so a catch-up is dispatched.
	bool previewPending_{false};

	/// @brief Scroll direction owed to a preview whose render was dispatched, or 0 for none.
	///
	/// The scroll-in reads `PadLEDs::imageStore`, so it cannot start until the render has actually filled
	/// it. When @ref drawSongPreview dispatches instead of rendering inline, the direction is parked here
	/// and @ref previewTrampoline starts the scroll once the fill is done. Starting it eagerly is what
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
