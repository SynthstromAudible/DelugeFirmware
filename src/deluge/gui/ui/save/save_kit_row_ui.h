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
#include "gui/ui/save/save_ui.h"

class Song;
class Instrument;

class SaveKitRowUI final : public SaveUI {
public:
	SaveKitRowUI();
	void setup(SoundDrum* drum, ParamManagerForTimeline* paramManager) {
		soundDrumToSave = drum;
		paramManagerToSave = paramManager;
		outputTypeToLoad = OutputType::SYNTH;
	}
	bool opened();
	// void selectEncoderAction(int8_t offset);
	void verticalEncoderAction(int32_t offset, bool encoderButtonPressed, bool shiftButtonPressed){};
	void endSession(){};
	bool performSave(bool mayOverwrite);

	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth] = NULL,
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = NULL) {
		return true;
	}
	const char* getName() { return "save_kit_row_ui"; }

protected:
	SoundDrum* soundDrumToSave;
	ParamManagerForTimeline* paramManagerToSave;
	// int32_t arrivedInNewFolder(int32_t direction);
};

extern SaveKitRowUI saveKitRowUI;
