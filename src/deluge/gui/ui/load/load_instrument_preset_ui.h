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
#include "model/drum/kit.h"
#include "model/note/note_row.h"
#include "processing/sound/sound_drum.h"

class Instrument;
class InstrumentClip;
class Output;

class LoadInstrumentPresetUI final : public LoadUI {
public:
	LoadInstrumentPresetUI();
	bool opened();
	//void selectEncoderAction(int8_t offset);
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	void instrumentEdited(Instrument* instrument);
	int32_t performLoad(bool doClone = false);
	int32_t performLoadSynthToKit();
	ActionResult timerCallback();
	bool getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows);
	bool renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3] = NULL,
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = NULL, bool drawUndefinedArea = true,
	                    int32_t navSys = -1) {
		return true;
	}
	bool renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	ReturnOfConfirmPresetOrNextUnlaunchedOne
	findAnUnlaunchedPresetIncludingWithinSubfolders(Song* song, InstrumentType instrumentType,
	                                                Availability availabilityRequirement);
	ReturnOfConfirmPresetOrNextUnlaunchedOne confirmPresetOrNextUnlaunchedOne(InstrumentType instrumentType,
	                                                                          String* searchName,
	                                                                          Availability availabilityRequirement);
	PresetNavigationResult doPresetNavigation(int32_t offset, Instrument* oldInstrument,
	                                          Availability availabilityRequirement, bool doBlink);

	InstrumentClip* instrumentClipToLoadFor; // Can be NULL - if called from Arranger.
	Instrument* instrumentToReplace; // The Instrument that's actually successfully loaded and assigned to the Clip.

	//these are all necessary to setup a sound drum
	bool loadingSynthToKitRow;
	SoundDrum* soundDrumToReplace;
	Kit* kitToLoadFor;
	int32_t noteRowIndex;
	NoteRow* noteRow;

protected:
	void enterKeyPress();
	void folderContentsReady(int32_t entryDirection);
	void currentFileChanged(int32_t movementDirection);

private:
	bool showingAuditionPads();
	int32_t setupForInstrumentType();
	void changeInstrumentType(InstrumentType newInstrumentType);
	void revertToInitialPreset();
	void exitAction();
	bool isInstrumentInList(Instrument* searchInstrument, Output* list);
	bool findUnusedSlotVariation(String* oldName, String* newName);
	bool checkFPs();

	uint8_t currentInstrumentLoadError;

	int16_t initialChannel;
	int8_t initialChannelSuffix;
	InstrumentType initialInstrumentType;

	bool changedInstrumentForClip;
	bool replacedWholeInstrument;

	String initialName;
	String initialDirPath;
};

extern LoadInstrumentPresetUI loadInstrumentPresetUI;
