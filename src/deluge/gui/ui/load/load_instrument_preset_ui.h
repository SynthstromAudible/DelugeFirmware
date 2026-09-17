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

	/// @brief How many preset-acquisition ops have actually RUN since boot.
	///
	/// Incremented once per runPresetAcquisitionOp() execution -- not once per
	/// dispatchPresetAcquisition() call, so a superseded or dropped dispatch is never counted. Read
	/// by the song-preview A/B harness's acquisition stage to tell a coalesced gesture from a
	/// dropped one, and to prove a CV switch dispatches NOTHING.
	/// @return The running acquisition-run count.
	[[nodiscard]] static int32_t presetAcquisitionsRun() { return presetAcquisitionsRun_; }

	/// @brief Whether a preset acquisition is in flight or queued.
	[[nodiscard]] bool presetAcquisitionInFlight() const { return presetCoalescer_.in_flight(); }

	/// @brief Whether a preset load is in flight or queued.
	[[nodiscard]] bool scrollLoadInFlight() const;

	/// @brief Dispatch one instrument acquisition onto the storage owner.
	///
	/// Mirrors currentFileChanged(): the caller builds `request` from live state HERE, the only
	/// point where reading it is safe, and the op then acts only on that snapshot.
	///
	/// Callers must dispatch ONLY on the Synth/Kit arm -- MIDI and CV read no card, and routing
	/// them through here would defer a close() behind a storage round-trip (spec section 2c).
	///
	/// The return means "the gesture was ACCEPTED", never "the acquisition finished" -- on the
	/// Embassy backing nothing has run yet when it comes back. It is [[nodiscard]] because callers
	/// kept gating their UI tail on it as though it were the old synchronous answer; a caller that
	/// genuinely has nothing to do with a dropped gesture writes an explicit `(void)`.
	///
	/// Nine call sites do exactly that, deliberately: for a pad press or an encoder turn there is no
	/// recovery to offer and no state left half-applied -- the coalescer is released, the view still
	/// shows what it showed, and the user's next press dispatches again. Silently dropping the gesture
	/// is the accepted behaviour for this rung; the `(void)`s are there so that stays a decision in
	/// the code rather than an oversight.
	///
	/// @param request The snapshot; see PresetRequest.
	/// @return true if the op was dispatched or ran inline; false if the gesture did nothing --
	///         either the owner queue was full (the coalescer is released in that case), or a
	///         creation gesture was already queued and must not be superseded (see
	///         deluge::storage::isCreationConsumer()).
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
	/// @brief A scroll-load's end: records the error, and shows it.
	static void onScrollLoaded(Error error);
	/// @brief A commit's end: shows an error, or recalculates a kit's colours and closes the browser.
	static void onCommitLoaded(Error error);

	/// @brief The dispatched acquisition op: counts itself in presetAcquisitionsRun(), takes the
	///        snapshot off presetCoalescer_, sets Browser::currentDir from it, runs that request's
	///        consumer follow-up -- which is what actually reads the directory and loads the preset,
	///        down each gesture's own unchanged call chain -- and then re-dispatches the latest-wins
	///        target if a newer gesture arrived while it ran.
	///
	/// Runs on the storage owner (inline on legacy/host).
	/// @param self The LoadInstrumentPresetUI instance.
	static void runPresetAcquisitionOp(void* self);

	/// @brief Runs one acquisition's consumer follow-up, on the owner. See runPresetAcquisitionOp().
	/// @param request The snapshot the acquisition was dispatched with; see PresetRequest.
	void runAcquisitionFollowUp(const deluge::storage::PresetRequest& request);

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

	// Coalesces repeated acquisition gestures (a held preset-navigation encoder, a mashed
	// output-type button) onto the storage worker, so only the gesture the user settles on
	// actually loads. Mirrors loadCoalescer_.
	deluge::storage::LatestWins<deluge::storage::PresetRequest> presetCoalescer_{};

	int16_t initialChannel{};
	int8_t initialChannelSuffix{};
	OutputType initialOutputType;

	bool changedInstrumentForClip{};
	bool replacedWholeInstrument{};

	std::string initialName{};
	std::string initialDirPath{};

	/// @brief Backing counter for presetAcquisitionsRun(). Incremented once per
	///        runPresetAcquisitionOp() execution (see its doc), not once per
	///        dispatchPresetAcquisition() call.
	static inline int32_t presetAcquisitionsRun_{};
};

extern LoadInstrumentPresetUI loadInstrumentPresetUI;
