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

class Instrument;
class InstrumentClip;
class Output;

class LoadInstrumentPresetUI final : public LoadUI {
public:
	LoadInstrumentPresetUI() = default;
	bool opened() override;
	// void selectEncoderAction(int8_t offset);
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	void instrumentEdited(Instrument* instrument);
	Error performLoad(bool doClone = false);
	Error performLoadSynthToKit();
	ActionResult timerCallback();
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows);
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth] = NULL,
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = NULL, bool drawUndefinedArea = true,
	                    int32_t navSys = -1) {
		return true;
	}
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	ReturnOfConfirmPresetOrNextUnlaunchedOne
	findAnUnlaunchedPresetIncludingWithinSubfolders(Song* song, OutputType outputType,
	                                                Availability availabilityRequirement);
	ReturnOfConfirmPresetOrNextUnlaunchedOne confirmPresetOrNextUnlaunchedOne(OutputType outputType, String* searchName,
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

protected:
	void enterKeyPress();
	void folderContentsReady(int32_t entryDirection);
	void currentFileChanged(int32_t movementDirection);

private:
	bool showingAuditionPads();
	Error setupForOutputType();
	void changeOutputType(OutputType newOutputType);
	void revertToInitialPreset();
	void exitAction();
	bool isInstrumentInList(Instrument* searchInstrument, Output* list);
	bool findUnusedSlotVariation(String* oldName, String* newName);

	InstrumentClip* instrumentClipToLoadFor; // Can be NULL - if called from Arranger.
	Instrument* instrumentToReplace; // The Instrument that's actually successfully loaded and assigned to the Clip.

	// these are all necessary to setup a sound drum
	bool loadingSynthToKitRow;
	SoundDrum* soundDrumToReplace;
	int32_t noteRowIndex;
	NoteRow* noteRow;
	Error currentInstrumentLoadError;

	int16_t initialChannel;
	int8_t initialChannelSuffix;
	OutputType initialOutputType;

	bool changedInstrumentForClip;
	bool replacedWholeInstrument;

	String initialName;
	String initialDirPath;
};

extern LoadInstrumentPresetUI loadInstrumentPresetUI;
