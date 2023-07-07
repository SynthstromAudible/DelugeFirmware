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

#include "gui/ui/load/load_ui.h"
#include "hid/button.h"

class Instrument;
class InstrumentClip;
class Output;

class LoadInstrumentPresetUI final : public LoadUI {
public:
	LoadInstrumentPresetUI();
	bool opened();
	//void selectEncoderAction(int8_t offset);
	int buttonAction(hid::Button b, bool on, bool inCardRoutine);
	int padAction(int x, int y, int velocity);
	int verticalEncoderAction(int offset, bool inCardRoutine);
	void instrumentEdited(Instrument* instrument);
	int performLoad(bool doClone = false);
	int timerCallback();
	bool getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows);
	bool renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3] = NULL,
	                    uint8_t occupancyMask[][displayWidth + sideBarWidth] = NULL, bool drawUndefinedArea = true,
	                    int navSys = -1) {
		return true;
	}
	bool renderSidebar(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
	                   uint8_t occupancyMask[][displayWidth + sideBarWidth]);
	ReturnOfConfirmPresetOrNextUnlaunchedOne
	findAnUnlaunchedPresetIncludingWithinSubfolders(Song* song, int instrumentType, int availabilityRequirement);
	ReturnOfConfirmPresetOrNextUnlaunchedOne confirmPresetOrNextUnlaunchedOne(int instrumentType, String* searchName,
	                                                                          int availabilityRequirement);
	PresetNavigationResult doPresetNavigation(int offset, Instrument* oldInstrument, int availabilityRequirement,
	                                          bool doBlink);

	InstrumentClip* instrumentClipToLoadFor; // Can be NULL - if called from Arranger.
	Instrument* instrumentToReplace; // The Instrument that's actually successfully loaded and assigned to the Clip.

protected:
	void enterKeyPress();
	void folderContentsReady(int entryDirection);
	void currentFileChanged(int movementDirection);

private:
	bool showingAuditionPads();
	int setupForInstrumentType();
	void changeInstrumentType(int newInstrumentType);
	void revertToInitialPreset();
	void exitAction();
	bool isInstrumentInList(Instrument* searchInstrument, Output* list);
	bool findUnusedSlotVariation(String* oldName, String* newName);

	uint8_t currentInstrumentLoadError;

	int16_t initialChannel;
	int8_t initialChannelSuffix;
	uint8_t initialInstrumentType;

	bool changedInstrumentForClip;
	bool replacedWholeInstrument;

	String initialName;
	String initialDirPath;
};

extern LoadInstrumentPresetUI loadInstrumentPresetUI;
