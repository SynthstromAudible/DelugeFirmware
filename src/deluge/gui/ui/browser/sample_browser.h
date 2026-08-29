/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "gui/ui/browser/browser.h"
#include "hid/button.h"
#include "storage/deferred_load.h"
#include <string>

class SoundDrum;
class Source;
class Sample;
class MenuItem;

namespace deluge::gui::menu_item {
class HorizontalMenu;
}

class SampleBrowser final : public Browser {
public:
	SampleBrowser();
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) override;
	bool opened() override;
	void focusRegained() override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	bool canSeeViewUnderneath() override;
	Error claimAudioFileForInstrument(bool makeWaveTableWorkAtAllCosts = false);
	Error claimAudioFileForAudioClip();
	void scrollFinished() override;
	bool importFolderAsKit();
	bool importFolderAsMultisamples();
	ActionResult timerCallback() override;
	bool claimCurrentFile(int32_t mayDoPitchDetection = 1, int32_t mayDoSingleCycle = 1, int32_t mayDoWaveTable = 1,
	                      bool loadWithoutExiting = false); // 0 means no. 1 means auto. 2 means yes definitely
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	void exitAndNeverDeleteDrum();
	/// @brief Per-tick surface (every ~15 ms while this UI is up). Re-drives a preview whose load
	///        deferred, then chains to the base.
	///
	/// This is the ONLY thing that finishes a deferred preview. previewIfPossible() is reached from
	/// exactly two places -- entering the browser and moving the cursor -- so with no re-drive an
	/// async load that answers Error::LOAD_IN_PROGRESS is never asked again, and the user gets no
	/// sound, no waveform and no error until they happen to scroll back onto that file.
	void browserGraphicsRoutine() override;

	/// @brief Preview the file at @p path: snapshot it and dispatch the load+render onto the storage
	///        owner, coalesced latest-wins.
	///
	/// The path-based half of previewIfPossible(), which resolves the current selection to a path and
	/// calls this. Split out so a caller that already knows the path (the load A/B gate, which has no
	/// browser file list) drives the same production code the browser does, re-drive included.
	///
	/// @param path              The file to preview.
	/// @param movementDirection Scroll direction driving the teardown/entry animation; 0 for none.
	void previewFile(std::string path, int32_t movementDirection);

	/// @brief Abandon any preview in progress: nothing that is loading is wanted any more.
	///
	/// For the moves that leave nothing to preview at all: onto a folder or an empty selection
	/// (previewIfPossible), up a directory or out of the browser (backButtonAction, exitAction,
	/// exitAndNeverDeleteDrum), and into a fresh visit (opened). None of those dispatch a
	/// replacement, so the ordinary latest-wins supersession never fires; without this, a load
	/// dispatched for the file the cursor has LEFT finishes, gets re-driven by graphicsRoutine(),
	/// and then sounds and draws that file over whatever the user actually moved to -- or, on the
	/// exit paths, after the browser is gone.
	///
	/// Cancelling strands nothing. It abandons the RESULT, never the work: the load carries on in
	/// the loader and is either collected by a later request for the same path or evicted. And it
	/// keeps the single-flight guard rather than releasing it, which is safe on every path here
	/// because each one reaches this only after a dispatch the storage owner ACCEPTED, so the op
	/// runs and releases the guard itself (a dispatch the owner refused goes to release_dispatch(),
	/// not here).
	///
	/// Public because the A/B gate has to be able to drive it: that harness calls previewFile()
	/// directly and never populates a file list, so it can never reach the folder branch that calls
	/// this in the real UI.
	void cancelPreview() { previewLoad_.cancel(); }

	/// @brief Whether a preview load deferred and is waiting for graphicsRoutine() to ask again.
	///
	/// Exposed so a gate can tell "the preview settled" apart from "the preview was dropped and
	/// nothing will ever finish it" -- from outside, the two look identical.
	[[nodiscard]] bool previewLoadPending() const { return previewLoad_.pending(); }

	std::string lastFilePathLoaded{};

	// menus to open when a sample file is selected
	deluge::gui::menu_item::HorizontalMenu* parentMenuHeadingTo{nullptr};
	MenuItem* menuItemHeadingTo{nullptr};

	// ui
	UIType getUIType() override { return UIType::SAMPLE_BROWSER; }
	bool canDisplayFavourites() override { return true; }

protected:
	void enterKeyPress() override;
	void exitAction() override;
	ActionResult backButtonAction() override;
	void folderContentsReady(int32_t entryDirection) override;
	void currentFileChanged(int32_t movementDirection) override;
	void onBrowserOpened() override;

private:
	void displayCurrentFilename();

	/// @brief Snapshot of the file to preview, captured at trigger time.
	///
	/// Lets the dispatched op load the file the user pointed at even if the selection moves on
	/// before it runs (fast cursor-scroll under async dispatch).
	struct PreviewTarget {
		std::string path;
		int32_t movementDirection;
	};

	/// @brief Snapshot the current file and dispatch its preview load+render onto the storage
	///        owner (coalesced latest-wins).
	///
	/// A folder or empty selection just clears the preview.
	/// @param movementDirection Scroll direction driving the preview's teardown/entry animation.
	void previewIfPossible(int32_t movementDirection = 1);
	/// @brief Dispatch @p target onto the storage owner through the coalescer.
	///
	/// Shared by the user-driven entry (previewFile) and the deferred-load re-drive
	/// (graphicsRoutine), so both take the same single-flight/latest-wins path.
	/// @param target The file to preview.
	/// @return true if the target is now the coalescer's to run (dispatched, or queued behind an op
	///         that will pick it up); false if the storage-owner queue was full so nothing will run
	///         it -- the caller decides whether to drop it or ask again.
	[[nodiscard]] bool dispatchPreview(const PreviewTarget& target);
	/// @brief The dispatched op: load and render its attempt's target, then re-dispatch if a newer
	///        target arrived while it ran.
	///
	/// Renders nothing if the attempt was cancelled while it sat queued -- the cursor has moved
	/// somewhere with no preview at all, and drawing the file it has left is the damage.
	///
	/// Runs on the storage owner (inline on legacy/host).
	/// @param self The SampleBrowser instance.
	static void runPreviewOp(void* self);
	/// @brief Load @p target's sample and render its waveform.
	///
	/// Clears the preview display if nothing loaded -- but NOT if the load merely deferred, in which
	/// case whatever is onscreen stays put until the re-drive lands. That matches what the
	/// synchronous path does at the same moment: it blocks inside the load, and the old content
	/// stays up.
	/// @param target The file to preview, snapshotted by previewIfPossible().
	/// @return true if the load deferred (Error::LOAD_IN_PROGRESS) and has to be asked for again.
	[[nodiscard]] bool renderPreviewForTarget(const PreviewTarget& target);
	/// @brief Tear down any on-screen sample preview.
	///
	/// Shared by the no-file path and a failed load.
	/// @param movementDirection Scroll direction driving the teardown animation.
	void clearPreviewDisplay(int32_t movementDirection);

	/// @brief The one piece of preview-load state: which file is being previewed, whether an op is
	///        out working on it, whether it deferred, and whether it is still wanted.
	///
	/// A deferred re-ask holds the ORIGINAL target, movementDirection included, so when the re-drive
	/// lands the entry animation is the one the cursor move asked for, a few ms late.
	deluge::storage::DeferredLoad<PreviewTarget> previewLoad_{};

	/// @brief The dispatched op for the direct pad-tap load (enterKeyPress).
	///
	/// Runs claimCurrentFile() with its default args on the storage owner. The result is
	/// discarded (as it always was inline) — claimCurrentFile() already handles its own
	/// loading-animation/close-on-success internally. Its unnamed `void*` parameter is the
	/// storage-owner op signature; unused here.
	static void runClaimCurrentFileOp(void*);

	void audioFileIsNowSet();
	bool canImportWholeKit();
	bool loadAllSamplesInFolder(bool detectPitch, int32_t* getNumSamples, Sample*** getSortArea,
	                            bool* getDoingSingleCycle = nullptr, int32_t* getNumCharsInPrefix = nullptr);
	std::string getCurrentFilePath() override;
	void drawKeysOverWaveform();
	void autoDetectSideChainSending(SoundDrum* drum, Source* source, char const* fileName);
	void possiblySetUpBlinking();

	bool autoLoadEnabled{};

	bool currentlyShowingSamplePreview{};

	bool qwertyCurrentlyDrawnOnscreen; // This will linger as true even when qwertyVisible has been set to false
};

extern SampleBrowser sampleBrowser;
