/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#ifndef RENAMEDRUMUI_H_
#define RENAMEDRUMUI_H_

#include "gui/ui/rename/rename_ui.h"
#include "hid/button.h"

class SoundDrum;

class RenameDrumUI final : public RenameUI {
public:
	RenameDrumUI();
	bool opened();
	int buttonAction(hid::Button b, bool on, bool inCardRoutine);
	int padAction(int x, int y, int velocity);
	int verticalEncoderAction(int offset, bool inCardRoutine);
	bool getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows);

protected:
	void enterKeyPress();

private:
	SoundDrum* getDrum();
	void exitUI();
};

extern RenameDrumUI renameDrumUI;

#endif /* RENAMEDRUMUI_H_ */
