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

class MIDIDevice;

namespace deluge::gui::menu_item::midi {

class Command final : public MenuItem {
public:
	Command(l10n::String newName, GlobalMIDICommand newCommandNumber = GlobalMIDICommand::PLAYBACK_RESTART)
	    : MenuItem(newName), commandNumber(newCommandNumber) {}
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void drawValue() const;
	void selectEncoderAction(int32_t offset) override;
	bool allowsLearnMode() override { return true; }
	bool shouldBlinkLearnLed() override { return true; }
	void unlearnAction() override;
	bool learnNoteOn(MIDIDevice* device, int32_t channel, int32_t noteCode) override;
	void learnProgramChange(MIDIDevice* device, int32_t channel, int32_t programNumber) override;
	void learnCC(MIDIDevice* device, int32_t channel, int32_t ccNumber, int32_t value) override;

	void drawPixelsForOled();

	GlobalMIDICommand commandNumber;
};
} // namespace deluge::gui::menu_item::midi
