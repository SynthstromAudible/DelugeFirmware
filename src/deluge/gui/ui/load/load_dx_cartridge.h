
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
#include "gui/ui/ui.h"
#include "storage/DX7Cartridge.h"

class SoundInstrument;

class LoadDxCartridgeUI : public UI {
	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;
	void drawValue();

	void selectEncoderAction(int8_t offset) override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t on) override;
	UIType getUIType() override { return UIType::BROWSER; }
	void readValue();

public:

	LoadDxCartridgeUI() {};

	bool tryLoad(const char* path);
	void navigate(int8_t offset, bool wrapAround = true);
	// this thing is big. allocate external mem on demand
	DX7Cartridge* pd = nullptr;

	int32_t currentValue = 0;
	int scrollPos = 0; // Each instance needs to store this separately

	SoundInstrument *currentSound = nullptr;
};

extern LoadDxCartridgeUI loadDxCartridgeUI;
