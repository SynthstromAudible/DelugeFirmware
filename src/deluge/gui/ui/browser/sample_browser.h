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
#include "storage/audio/latest_sample_load.h"
#include "storage/latest_wins.h"
#include <cstdint>
#include <optional>
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
	void scrollFinished() override;
	bool importFolderAsKit();
	bool importFolderAsMultisamples();
	ActionResult timerCallback() override;
	/// @brief Ask the sample claim for the file the browser is on, as Enter and the synth context menu do.
	/// @param mayDoPitchDetection For each of these three: 0 means no, 1 means auto, 2 means definitely.
	/// @param mayDoSingleCycle    As above.
	/// @param mayDoWaveTable      As above.
	/// @param completion          Told how the claim ended; a cancelled claim tells it nothing.
	void requestClaim(int32_t mayDoPitchDetection, int32_t mayDoSingleCycle, int32_t mayDoWaveTable,
	                  void (*completion)(bool success) = nullptr);
	/// @brief Reset the current source's oscillator volume, if it is not automated and was at 0: after a file is set.
	void audioFileIsNowSet();
	/// @brief Send @p drum to side-chain when its file, named @p fileName, looks like a kick and plays once.
	void autoDetectSideChainSending(SoundDrum* drum, Source* source, char const* fileName);
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	void exitAndNeverDeleteDrum();
	/// @brief Per-tick surface (every ~15 ms while this UI is up): ticks the preview, then chains to the base.
	///
	/// This is the ONLY thing that loads a preview. previewIfPossible() is reached from exactly two places -- entering
	/// the browser and moving the cursor -- and only records the file; its load starts, is ticked and ends here. See
	/// tickPreview().
	void browserGraphicsRoutine() override;

	/// @brief Preview the file at @p path: record it, for the next tick to start its load.
	///
	/// The path-based half of previewIfPossible(), which resolves the current selection to a path and
	/// calls this. Split out so a caller that already knows the path (the load A/B gate, which has no
	/// browser file list) drives the same production code the browser does, tick included.
	///
	/// @param path              The file to preview.
	/// @param movementDirection Scroll direction driving the teardown/entry animation; 0 for none.
	void previewFile(std::string path, int32_t movementDirection);

	/// @brief Abandon any preview in progress: nothing wanted, loading or waiting to be shown is wanted any more.
	///
	/// For the moves that leave nothing to preview at all: onto a folder or an empty selection
	/// (previewIfPossible), up a directory or out of the browser (backButtonAction, exitAction,
	/// exitAndNeverDeleteDrum), and into a fresh visit (opened). None of those asks for a replacement, so nothing
	/// supersedes the preview; without this, a load started for the file the cursor has LEFT ends, and then sounds and
	/// draws that file over whatever the user actually moved to -- or, on the exit paths, after the browser is gone.
	///
	/// Clears the wanted file, drops the load, drops a show not yet dispatched, and moves the generation so that a show
	/// already dispatched does nothing. Cancelling strands nothing: a dropped load's request stays with the loader,
	/// where a later lookup of the same path collects it, and a result nobody collects is evicted.
	///
	/// Public because the A/B gate has to be able to drive it: that harness calls previewFile()
	/// directly and never populates a file list, so it can never reach the folder branch that calls
	/// this in the real UI.
	void cancelPreview();

	/// @brief Whether a preview is wanted and not yet started, or its load has not ended.
	///
	/// Exposed so a gate can tell "the preview settled" apart from "the preview was dropped and
	/// nothing will ever finish it" -- from outside, the two look identical.
	[[nodiscard]] bool previewLoadPending() const { return previewWanted_.has_value() || previewLoad_.pending(); }

	/// @brief How many previews gave up on a load that stopped moving.
	///
	/// A preview whose load ends stalled (AsyncSampleLoad::kMaxStalledTicks walks in a row with nothing settled) gives
	/// up with the card error; see finishPreviewLoad(). Exposed for the A/B gate, which holds a load far past the
	/// bound.
	[[nodiscard]] int32_t previewGiveUps() const { return previewGiveUps_; }

	/// @brief How many preview load steps ran on the storage owner.
	///
	/// A preview's load belongs on the interaction tier, where the browser's per-tick surface ticks it; a step on the
	/// owner means the load was sent there. Exposed for the A/B gate's AB-PREVIEW-TIER check, which requires none.
	[[nodiscard]] int32_t previewLoadsOnOwner() const { return previewLoadsOnOwner_; }

	/// @name Where a folder import's card work began, for the sample-claim gate's IM-TIER check.
	/// @{
	/// Folder listings opened on the storage owner.
	[[nodiscard]] int32_t importListingsOnOwner() const { return importListingsOnOwner_; }
	/// File loads begun on the storage owner; a folder import's loads belong on the interaction tier.
	[[nodiscard]] int32_t importLoadsOnOwner() const { return importLoadsOnOwner_; }
	/// Pitch detections run on the storage owner, where they read the card synchronously.
	[[nodiscard]] int32_t importPitchOnOwner() const { return importPitchOnOwner_; }
	/// @}

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
	/// Lets the preview's tick load the file the user pointed at, and its show animate in the direction the cursor
	/// moved, even if the selection moves on before either runs.
	struct PreviewTarget {
		std::string path;
		int32_t movementDirection;
	};

	/// @brief Ask for the current file's preview. A folder or empty selection cancels any preview and clears it.
	/// @param movementDirection Scroll direction driving the preview's teardown/entry animation.
	void previewIfPossible(int32_t movementDirection = 1);

	/// @brief What the show operation is for: the direction to animate in, and the generation its load ended in.
	struct PreviewShow {
		int32_t movementDirection = 0;
		uint32_t generation = 0;
	};

	/// @brief One tick of the preview: start the wanted file's load, tick the load in hand, and act on its end.
	///
	/// Runs on the interaction tier, from browserGraphicsRoutine(). No sample is read from the card here: the load
	/// finds its file through the async loader.
	void tickPreview();
	/// @brief Start @p target's load, replacing any load in hand.
	///
	/// Records the path as the last one loaded, stops the preview sounding (which also unbinds its holder), and asks
	/// for the file in the preview drum's first range. Moves the generation, so a show dispatched for an earlier file
	/// does nothing and the display stays as it is until this one lands.
	/// @param target The file to preview.
	void startPreviewLoad(PreviewTarget target);
	/// @brief Act on the end of a preview's load.
	///
	/// A load that found its file, or settled without one, shows any lookup error, sounds the preview if the browser
	/// should, and dispatches the show operation. A load that stalled, or found the card unusable, gives up with the
	/// card error and leaves the display as it is; nothing is asked for again until the next move onto the file.
	/// @param outcome How the load ended, with the target it was started for.
	void finishPreviewLoad(deluge::storage::LatestSampleLoad<PreviewTarget>::Outcome outcome);
	/// @brief The show operation: show the latest show asked for, unless a newer preview has started or the preview
	///        was cancelled since.
	///
	/// Runs on the storage owner (inline on legacy/host), latest-wins through previewShow_.
	/// @param self The SampleBrowser instance.
	static void runPreviewShowOp(void* self);
	/// @brief Draw the preview drum's sample, or clear the preview display
	///        when the drum holds none.
	/// @param show The show to draw.
	void showPreview(const PreviewShow& show);
	/// @brief Tear down any on-screen sample preview.
	///
	/// Shared by the no-file path and a show with no sample to draw.
	/// @param movementDirection Scroll direction driving the teardown animation.
	void clearPreviewDisplay(int32_t movementDirection);

	/// @brief The file a cursor move asked to preview, until the next tick starts its load.
	std::optional<PreviewTarget> previewWanted_{};
	/// @brief The preview's load, ticked by tickPreview(). Holds the target it was started for.
	deluge::storage::LatestSampleLoad<PreviewTarget> previewLoad_{};
	/// @brief The show operation's single flight, latest-wins.
	deluge::storage::LatestWins<PreviewShow> previewShow_{};
	/// @brief Moves each time a preview's load starts or the preview is cancelled. A show from an earlier generation
	///        does nothing.
	uint32_t previewGeneration_{0};

	/// @brief Previews that gave up on a stalled load. See previewGiveUps().
	int32_t previewGiveUps_{0};

	/// @brief Preview load steps that ran on the storage owner. See previewLoadsOnOwner().
	int32_t previewLoadsOnOwner_{0};

	/// @brief Folder listings opened on the storage owner. See importListingsOnOwner().
	int32_t importListingsOnOwner_{0};
	/// @brief Folder-import file loads begun on the storage owner. See importLoadsOnOwner().
	int32_t importLoadsOnOwner_{0};
	/// @brief Folder-import pitch detections run on the storage owner. See importPitchOnOwner().
	int32_t importPitchOnOwner_{0};

	bool canImportWholeKit();
	bool loadAllSamplesInFolder(bool detectPitch, int32_t* getNumSamples, Sample*** getSortArea,
	                            bool* getDoingSingleCycle = nullptr, int32_t* getNumCharsInPrefix = nullptr);
	std::string getCurrentFilePath() override;
	void drawKeysOverWaveform();
	void possiblySetUpBlinking();

	bool autoLoadEnabled{};

	bool currentlyShowingSamplePreview{};

	bool qwertyCurrentlyDrawnOnscreen; // This will linger as true even when qwertyVisible has been set to false
};

extern SampleBrowser sampleBrowser;
