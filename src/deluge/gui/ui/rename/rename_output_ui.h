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

#pragma once

#include "gui/ui/rename/rename_ui.h"
#include "hid/button.h"

class Output;

class RenameOutputUI final : public RenameUI {
public:
	RenameOutputUI();
	bool opened();
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows);

	Output* output;

	// ui
	UIType getUIType() { return UIType::RENAME_OUTPUT; }
	const char* getName() { return "rename_output_ui"; }
	bool exitUI() override;

protected:
	void enterKeyPress();
};

extern RenameOutputUI renameOutputUI;
