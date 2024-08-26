/*
 * Copyright (c) 2024 Sean Ditny
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

class LoadMidiDeviceDefinitionUI final : public LoadUI {
public:
	LoadMidiDeviceDefinitionUI() = default;

	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows);
	bool opened();

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);

	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth] = NULL,
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = NULL, bool drawUndefinedArea = true,
	                    int32_t navSys = -1) {
		return true;
	}
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
		return true;
	}

	Error performLoad(bool doClone = false);

	// ui
	UIType getUIType() override { return UIType::LOAD_MIDI_DEVICE_DEFINITION; }

protected:
	void folderContentsReady(int32_t entryDirection);
	void enterKeyPress();

private:
	Error setupForLoadingMidiDeviceDefinition();
	Error currentLabelLoadError = Error::NONE;
};

extern LoadMidiDeviceDefinitionUI loadMidiDeviceDefinitionUI;
