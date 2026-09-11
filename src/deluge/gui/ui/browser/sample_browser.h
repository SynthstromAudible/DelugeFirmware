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
#include "storage/latest_wins.h"
#include <string>
#include <string_view>

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

	/// @brief Point the browser at @p dir / @p name, so `claimCurrentFile()` can run against a file
	///        the user chose somewhere this browser was never open.
	///
	/// `claimCurrentFile()` takes no path: it reads `Browser::currentDir` and
	/// `Browser::getCurrentFileItem()` for itself, which is what `getCurrentFilePath()` (`:538-549`)
	/// composes. Browsing normally leaves both set, so `enterKeyPress` never has to. The ported
	/// `BrowserScene` hands its dispatcher a *path* and has already closed — and `BrowserScene::on_exit`
	/// empties the shared catalogue (`scenes/browser/browser_scene.cpp:402`, via
	/// `FileCatalogue::emptyFileItems`) exactly as `Browser::close()` does — so the state legacy would
	/// have left behind has to be rebuilt from the path before the claim can read it.
	///
	/// This is the smallest thing that does that, and it is deliberately not
	/// `Browser::setFileByFullPath` (`browser.h:64`): that one dispatches a whole listing of the
	/// folder, then `setEnteredTextFromCurrentFilename`, `renderUIsForOled` and `currentFileChanged`
	/// (`browser.cpp:257-274`) — a re-read and a sample preview that `enterKeyPress`'s file arm does
	/// not do, i.e. behaviour the port would be adding rather than carrying over.
	///
	/// It lives here rather than in the dispatcher because `fileIndexSelected` and
	/// `setEnteredTextFromCurrentFilename()` are `Browser`'s protected state, and because "the
	/// selected index indexes `fileItems`" is `Browser`'s invariant to keep.
	///
	/// @note **What this leaves behind, on both paths.** The success path leaves a **one-entry**
	///       catalogue and `fileIndexSelected == 0`; the failure path leaves `currentDir` already
	///       reassigned to @p dir with the catalogue empty and nothing selected. Neither is cleaned
	///       up, deliberately: every reader re-lists first (`FileCatalogue::readFileItemsForFolder`
	///       opens with `emptyFileItems()`, `storage/file_catalogue.cpp:209`), and clearing
	///       `fileIndexSelected` back to a value that indexed the *old* catalogue would be the bug
	///       this function exists to avoid. It is exactly the state a real browse leaves, one entry
	///       narrower.
	///
	/// @warning **The caller must claim with `loadWithoutExiting = true`.** `claimCurrentFile`'s
	///          success tail closes the browser (`exitAndNeverDeleteDrum()` → `Browser::close()` →
	///          `closeUI(this)`), and under the ported browser this object was never opened, so it is
	///          not in `uiNavigationHierarchy` and `closeUI` would read index `-1`
	///          (`gui/ui/ui.cpp:183-195`). `deluge::gui::ui::runShellSampleClaimOp` is the op that
	///          gets this right; `runClaimCurrentFileOp` is `enterKeyPress`'s and must not be reused
	///          from outside an open browser.
	///
	/// @param dir  The chosen file's directory, as `Browser::currentDir` spells it (empty at the card
	///             root).
	/// @param name The chosen file's name, extension included, as `FileItem::filename` spells it.
	/// @return False when the one-entry catalogue could not be allocated, in which case nothing is
	///         selected and the caller must not claim.
	bool selectFileForClaim(std::string_view dir, std::string_view name);

	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	void exitAndNeverDeleteDrum();

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
	/// @brief The dispatched op: load and render the coalescer's current target, then re-dispatch
	///        if a newer target arrived while it ran.
	///
	/// Runs on the storage owner (inline on legacy/host).
	/// @param self The SampleBrowser instance.
	static void runPreviewOp(void* self);
	/// @brief Load @p target's sample and render its waveform.
	///
	/// Clears the preview display if nothing loaded.
	/// @param target The file to preview, snapshotted by previewIfPossible().
	void renderPreviewForTarget(const PreviewTarget& target);
	/// @brief Tear down any on-screen sample preview.
	///
	/// Shared by the no-file path and a failed load.
	/// @param movementDirection Scroll direction driving the teardown animation.
	void clearPreviewDisplay(int32_t movementDirection);

	deluge::storage::LatestWins<PreviewTarget> previewCoalescer_{};

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
