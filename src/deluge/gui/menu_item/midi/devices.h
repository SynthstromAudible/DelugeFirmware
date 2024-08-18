/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#include "gui/menu_item/value.h"

class MIDIDevice;

namespace deluge::gui::menu_item::midi {

class Devices final : public Value<int32_t> {
public:
	using Value::Value;
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void selectEncoderAction(int32_t offset) override;
	MIDIDevice* getDevice(int32_t deviceIndex);
	virtual void drawValue();
	MenuItem* selectButtonPress() override;
	void drawPixelsForOled();

private:
	size_t currentScroll;
};

extern Devices devicesMenu;
} // namespace deluge::gui::menu_item::midi
