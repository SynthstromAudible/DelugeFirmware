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

#ifndef SAVEUI_H_
#define SAVEUI_H_

#include <SlotBrowser.h>
#include "definitions.h"

class SaveUI : public SlotBrowser {
public:
	SaveUI();
	bool opened();

	virtual bool performSave(
	    bool mayOverwrite =
	        false) = 0; // Returns true if success, or if otherwise dealt with (e.g. "overwrite" context menu brought up)
	void focusRegained();
	bool renderSidebar(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3] = NULL,
	                   uint8_t occupancyMask[][displayWidth + sideBarWidth] = NULL) {
		return true;
	}
	bool canSeeViewUnderneath() final { return (DELUGE_MODEL == DELUGE_MODEL_40_PAD); }
	int timerCallback();
	int buttonAction(int x, int y, bool on, bool inCardRoutine);

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	bool getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows);
#endif

protected:
	//void displayText(bool blinkImmediately) final;
	void enterKeyPress() final;
	static bool currentFolderIsEmpty;
};

#endif /* SAVEUI_H_ */
