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
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/load/load_ui.h"
#include "hid/button.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "processing/sound/sound_drum.h"
#include "storage/latest_wins.h"
#include "storage/owner.h"
#include "storage/preset_acquisition.h"

class Instrument;
class InstrumentClip;
class Output;

class LoadInstrumentPresetUI final : public LoadUI {
private:
	/// InstrumentLoad runs the browser's own loads, and calls back into its card-free prologue and its swap.
	friend class deluge::gui::InstrumentLoad;

public:
	LoadInstrumentPresetUI() = default;
	bool opened() override;
	// void selectEncoderAction(int8_t offset);
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	/// @brief Clone the selected preset: a new Instrument under an unused variation of its name, as the long-press
	///        menu asks. The browser's gate is closed until the clone ends.
	/// @param completion Told how the clone ended: Error::NONE once it is swapped in.
	void requestClone(void (*completion)(Error error));
	void selectEncoderAction(int8_t offset) override;
	/// @brief Close the browser, cancelling any load still out: whatever last swapped in stays.
	void close() override;
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
	/// @brief The walk of a preset navigation: the next preset along from @p fromName in @p fromDir.
	/// @param offset Which way to move.
	/// @param oldInstrument The Instrument moved on from: compared with the list's, never read through.
	/// @param outputType The type navigated through.
	/// @param fromName The preset moved on from, without extension.
	/// @param fromDir The folder it is in.
	/// @param availabilityRequirement Which presets may be landed on.
	/// @return The FileItem landed on, or an error (NO_ERROR_BUT_GET_OUT when there is nowhere else to go). The
	///         caller empties the browser's list.
	PresetNavigationResult doPresetNavigation(int32_t offset, Instrument* oldInstrument, OutputType outputType,
	                                          const std::string& fromName, const std::string& fromDir,
	                                          Availability availabilityRequirement);
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

	/// @brief How many preset acquisitions have actually RUN since boot: walks InstrumentLoad has queued.
	///
	/// A request superseded or refused before its walk is queued is never counted. Read by the song-preview A/B
	/// harness's acquisition stage to tell a coalesced gesture from a dropped one, and to prove a CV switch dispatches
	/// NOTHING.
	/// @return The running acquisition-run count.
	[[nodiscard]] static int32_t presetAcquisitionsRun() { return presetAcquisitionsRun_; }

	/// @brief Whether a preset acquisition is in flight or queued on InstrumentLoad.
	[[nodiscard]] bool presetAcquisitionInFlight() const;

	/// @brief Whether a preset load is in flight or queued.
	[[nodiscard]] bool scrollLoadInFlight() const;

	/// @brief Ask InstrumentLoad for one instrument acquisition: its walk runs on the storage owner, its load on the
	///        interaction tier, and the gesture's follow-up runs once the Instrument is ready.
	///
	/// The caller builds `request` from live state HERE, the only point where reading it is safe; the acquisition then
	/// acts only on that snapshot. Callers ask ONLY on the Synth/Kit arm: MIDI and CV read no card.
	///
	/// The return means "the gesture was ACCEPTED", never "the acquisition finished". A caller that has nothing to do
	/// with a refused gesture writes an explicit `(void)`: for a pad press or an encoder turn there is nothing left
	/// half-applied, and the user's next press asks again.
	///
	/// @param request The snapshot; see PresetRequest.
	/// @return false when the gesture is refused: a creation is already queued (see
	///         deluge::storage::isCreationConsumer()), and is never replaced.
	[[nodiscard]] bool dispatchPresetAcquisition(const deluge::storage::PresetRequest& request);

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

	/// @brief The snapshot a load of the current file is asked for with. Taken when it is asked for, the only point
	///        at which the browser's live list and entered text may be read for it.
	[[nodiscard]] deluge::gui::InstrumentLoadTarget snapshotTarget(int32_t movementDirection);
	/// @brief The card-free prologue of a load: nothing to load, reuse an existing or hibernating Instrument, or read
	///        the preset (with a clone's name found). Reads instrumentToReplace live, as the load it follows left it.
	deluge::gui::InstrumentLoadDecision decideLoad(const deluge::gui::InstrumentLoadRequest& request);
	/// @brief Swap @p newInstrument in for instrumentToReplace, in one pass, as @p decision says.
	/// @return Error::NONE, or the swap's failure.
	Error swapInstrument(const deluge::gui::InstrumentLoadRequest& request,
	                     const deluge::gui::InstrumentLoadDecision& decision, Instrument* newInstrument);
	/// @brief Replace the kit row's drum with @p newDrum, in one pass.
	/// @return Error::NONE, or FILE_CORRUPTED when the new drum has no param manager.
	Error swapKitRow(const deluge::gui::InstrumentLoadRequest& request, SoundDrum* newDrum);
	/// @brief Put @p initialInstrument back on the clip, as @p target says: the tail every revert ends with, whether
	/// the
	///        original came from memory or off the card.
	void swapRevert(const deluge::gui::RevertTarget& target, Instrument* initialInstrument);
	/// @brief A scroll-load's end: records the error, and shows it.
	static void onScrollLoaded(Error error);
	/// @brief A commit's end: shows an error, or recalculates a kit's colours and closes the browser.
	static void onCommitLoaded(Error error);

	/// @brief Runs one acquisition's consumer follow-up with what InstrumentLoad acquired.
	/// @param request The snapshot the acquisition was asked for with; see PresetRequest.
	/// @param acquired The Instrument (or, for a new instrument, the slot name), or why there is none.
	void runAcquisitionFollowUp(const deluge::storage::PresetRequest& request,
	                            const deluge::storage::AcquireResult& acquired);

	/// @brief An acquisition's card-free prologue, as InstrumentLoad starts it: which walk, from where.
	/// @param request The gesture.
	/// @param walk Filled with the walk to run.
	/// @return false when there is nothing to acquire (the gesture's Instrument has gone, or its clip already has the
	///         type); the request then ends telling no one.
	bool beginAcquisition(const deluge::storage::PresetRequest& request, deluge::storage::AcquireWalkSpec* walk);

	/// @brief Hand an acquired Instrument to its gesture's follow-up. A preset parsed from the card that the follow-up
	///        did not take (its Instrument gone since, or its clip or track not made) is deleted.
	void deliverAcquisition(const deluge::storage::PresetRequest& request,
	                        const deluge::storage::AcquireResult& acquired);

	/// @brief Whether `instrument` is still one of currentSong's live outputs.
	///
	/// `PresetRequest::oldInstrument` is Song-owned, so it never dangles across the op's yields (spec
	/// section 6 covers that LIFETIME guarantee) -- but lifetime is not currency. Under coalescing,
	/// two rapid change-type/navigate gestures on the same row both snapshot the SAME `oldInstrument`
	/// pointer, because the second reads live state (outputsOnScreen) before the first gesture's op
	/// has completed. If the first op replaces or hibernates that Instrument, a coalesced follow-up
	/// for the second gesture must not act on it as though it were still the row's live output --
	/// this is what re-validates that before any such follow-up runs. Reused by every consumer that
	/// carries an `oldInstrument` (Tasks 4 and 5 add more).
	/// @param instrument The snapshot's `oldInstrument`; null is not "still live" (callers should
	///                    skip this check for consumers that never set it).
	/// @return true if `instrument` is still in currentSong's live output list.
	bool isRequestInstrumentStillLive(Instrument* instrument);

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

	int16_t initialChannel{};
	int8_t initialChannelSuffix{};
	OutputType initialOutputType;

	bool changedInstrumentForClip{};
	bool replacedWholeInstrument{};

	std::string initialName{};
	std::string initialDirPath{};

	/// @brief Backing counter for presetAcquisitionsRun(). Incremented by InstrumentLoad as it queues a walk.
	static inline int32_t presetAcquisitionsRun_{};
};

extern LoadInstrumentPresetUI loadInstrumentPresetUI;
