/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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
#include "gui/ui/load/load_ui.h"
#include "hid/button.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "processing/sound/sound_drum.h"
#include "storage/latest_wins.h"
#include "storage/owner.h"

class Instrument;
class InstrumentClip;
class Output;

class LoadInstrumentPresetUI final : public LoadUI {
private:
	/// @brief Snapshot of the file identity to load, recorded at dispatch time by
	///        currentFileChanged() (mirrors SampleBrowser::PreviewTarget).
	///
	/// @note performLoad()/performLoadSynthToKit() take this BY VALUE COPY (never a pointer into
	///       Browser::fileItems or a live Browser member): once the op starts running, a scroll on
	///       the UI task during the op's own SD-yield points (loadInstrumentFromFile() calls
	///       block_on_fiber internally) could otherwise rebuild Browser::fileItems and free a
	///       FileItem the op still held a raw pointer to. See runScrollLoadOp()'s doc for how the
	///       snapshot is used.
	struct LoadTarget {
		bool loadingSynthToKitRow = false;
		int32_t movementDirection = 1; // Scroll-animation hint; currently unused by performLoad().

		/// @brief Whether there was a current file selection at dispatch time
		///        (getCurrentFileItem() != nullptr).
		bool hasFile = false;
		bool isFolder = false;
		bool maybeExistsOnCard = true;

		/// @brief FileItem::instrument at dispatch time: an already-loaded (possibly hibernating)
		///        Instrument for this file, if any.
		///
		/// This is a Song-owned pointer, NOT a pointer into Browser::fileItems, so - unlike a
		/// FileItem* - it stays valid even if the file listing is rebuilt mid-op.
		Instrument* existingInstrument = nullptr;

		std::string path;    // getCurrentFilePath() at dispatch.
		std::string name;    // enteredText at dispatch - candidate name for the loaded Instrument/Drum.
		std::string dirPath; // currentDir at dispatch.
	};

public:
	LoadInstrumentPresetUI() = default;
	bool opened() override;
	// void selectEncoderAction(int8_t offset);
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	/// @brief Load the selected instrument preset, replacing instrumentToReplace (or cloning it).
	///
	/// @param doClone   If true, create a new Instrument even when the target file is already
	///                  loaded as instrumentToReplace, instead of reusing it.
	/// @param snapshot  If non-null, a LoadTarget captured at dispatch time (see LoadTarget) whose
	///                  fields are used instead of live Browser state; if null, the live current
	///                  file selection is read directly.
	/// @return Error::NONE on success, otherwise the failure.
	Error performLoad(bool doClone = false, const LoadTarget* snapshot = nullptr);
	/// @brief Load the selected instrument preset as a SoundDrum into the kit row being edited
	///        (soundDrumToReplace / noteRow).
	///
	/// @param snapshot If non-null, a LoadTarget captured at dispatch time (see LoadTarget) whose
	///                 fields are used instead of live Browser state; if null, the live current
	///                 file selection is read directly.
	/// @return Error::NONE on success, otherwise the failure.
	Error performLoadSynthToKit(const LoadTarget* snapshot = nullptr);
	ActionResult timerCallback() override;
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) override;
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth] = nullptr,
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = nullptr, bool drawUndefinedArea = true,
	                    int32_t navSys = -1) {
		return true;
	}
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	std::expected<FileItem*, Error>
	findAnUnlaunchedPresetIncludingWithinSubfolders(Song* song, OutputType outputType,
	                                                Availability availabilityRequirement);
	std::expected<FileItem*, Error> confirmPresetOrNextUnlaunchedOne(OutputType outputType, std::string* searchName,
	                                                                 Availability availabilityRequirement);
	PresetNavigationResult doPresetNavigation(int32_t offset, Instrument* oldInstrument,
	                                          Availability availabilityRequirement, bool doBlink);
	void setupLoadInstrument(OutputType newOutputType, Instrument* instrumentToReplace_,
	                         InstrumentClip* instrumentClipToLoadFor_) {
		Browser::outputTypeToLoad = newOutputType;
		instrumentToReplace = instrumentToReplace_;
		instrumentClipToLoadFor = instrumentClipToLoadFor_;
		loadingSynthToKitRow = false;
		soundDrumToReplace = nullptr;
		noteRowIndex = 255; // (not set value for note rows)
		noteRow = nullptr;
	}
	void setupLoadSynthToKit(Instrument* kit, InstrumentClip* clip, SoundDrum* drum, NoteRow* row, int32_t rowIndex) {
		Browser::outputTypeToLoad = OutputType::SYNTH;
		instrumentToReplace = kit;
		instrumentClipToLoadFor = clip;
		loadingSynthToKitRow = true;
		soundDrumToReplace = drum;
		noteRowIndex = rowIndex; // (not set value for note rows)
		noteRow = row;
	}

	// ui
	UIType getUIType() override { return UIType::LOAD_INSTRUMENT_PRESET; }
	bool canDisplayFavourites() override { return true; }

protected:
	void enterKeyPress() override;
	void folderContentsReady(int32_t entryDirection) override;
	void currentFileChanged(int32_t movementDirection) override;
	void onBrowserOpened() override;
	void onListingFailed(Error error) override;

private:
	bool showingAuditionPads();
	std::string setupForOutputType();
	void finishArrivedInFolder(char const* defaultDir);
	void changeOutputType(OutputType newOutputType);
	void revertToInitialPreset();
	void exitAction() override;
	bool isInstrumentInList(Instrument* searchInstrument, Output* list);
	bool findUnusedSlotVariation(std::string* oldName, std::string* newName);

	/// @brief The dispatched scroll-load op: loads loadCoalescer_.current() (performLoad()/
	///        performLoadSynthToKit()), surfaces a failure, then re-dispatches the latest-wins
	///        target if a newer scroll arrived while it ran.
	///
	/// Runs on the storage owner (inline on legacy/host).
	/// @param self The LoadInstrumentPresetUI instance.
	static void runScrollLoadOp(void* self);
	/// @brief The dispatched commit op (enterKeyPress).
	///
	/// Does its OWN authoritative performLoad()/performLoadSynthToKit() - a cache hit if a
	/// scroll-load already warmed it - handles the error, and runs the post-load commit tail
	/// (recalculateColours() + close()) that used to live inline in enterKeyPress().
	/// @param self The LoadInstrumentPresetUI instance.
	static void runCommitOp(void* self);

	// Tells changeOutputType()'s Open listing apart from opened()'s in the shared onBrowserOpened()/
	// onListingFailed() hooks (see changeOutputType()'s comment).
	bool changingOutputType_{};
	OutputType outputTypeBeforeChange_{};

	InstrumentClip* instrumentClipToLoadFor{}; // Can be NULL - if called from Arranger.
	Instrument* instrumentToReplace{}; // The Instrument that's actually successfully loaded and assigned to the Clip.

	// these are all necessary to setup a sound drum
	bool loadingSynthToKitRow{};
	SoundDrum* soundDrumToReplace{};
	int32_t noteRowIndex{};
	NoteRow* noteRow{};
	Error currentInstrumentLoadError;

	// Coalesces the scroll-triggered load (currentFileChanged(), fires on every non-reload encoder
	// tick) onto the storage worker: a fast scroll only actually loads the preset the user settles
	// on. Mirrors SampleBrowser's previewCoalescer_.
	deluge::storage::LatestWins<LoadTarget> loadCoalescer_{};

	int16_t initialChannel{};
	int8_t initialChannelSuffix{};
	OutputType initialOutputType;

	bool changedInstrumentForClip{};
	bool replacedWholeInstrument{};

	std::string initialName{};
	std::string initialDirPath{};
};

extern LoadInstrumentPresetUI loadInstrumentPresetUI;
