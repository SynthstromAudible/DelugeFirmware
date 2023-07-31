/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/menu_item.h"
#include "util/string.h"

class MIDIDevice;

namespace deluge::gui::menu_item::midi {

class Command final : public MenuItem {
public:
	Command(const string& newName, GlobalMIDICommand newCommandNumber = GlobalMIDICommand::PLAYBACK_RESTART)
	    : MenuItem(newName), commandNumber(newCommandNumber) {}
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void drawValue() const;
	void selectEncoderAction(int offset) override;
	bool allowsLearnMode() override { return true; }
	bool shouldBlinkLearnLed() override { return true; }
	void unlearnAction() override;
	bool learnNoteOn(MIDIDevice* device, int channel, int noteCode) override;
	void learnCC(MIDIDevice* device, int channel, int ccNumber, int value) override;
#if HAVE_OLED
	void drawPixelsForOled();
#endif

	GlobalMIDICommand commandNumber;
};
} // namespace deluge::gui::menu_item::midi
