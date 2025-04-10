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

#include "gui/context_menu/context_menu.h"
#include "hid/led/indicator_leds.h"

using namespace indicator_leds;

namespace deluge::gui::context_menu::clip_settings {

class NewClipType final : public ContextMenu {

public:
	NewClipType() = default;
	void selectEncoderAction(int8_t offset) override;
	bool setupAndCheckAvailability();
	bool canSeeViewUnderneath() override { return true; }
	bool acceptCurrentOption() override; // If returns false, will cause UI to exit
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	void updateSelectedOption();
	void updateOutputToCreate();

	LED getLedFromOption(int32_t option);
	void disableLedForOption(int32_t option);
	void blinkLedForOption(int32_t option);

	OutputType toCreate = OutputType::NONE;

	/// Title
	char const* getTitle() override;

	/// Options
	std::span<const char*> getOptions() override;
	ActionResult padAction(int32_t x, int32_t y, int32_t on) override;
};

extern NewClipType newClipType;
} // namespace deluge::gui::context_menu::clip_settings
