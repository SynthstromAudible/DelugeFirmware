/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "lib/printf.h"
#include "modulation/arpeggiator.h"

namespace deluge::gui::menu_item::arpeggiator {
class Rhythm final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return NUM_PRESET_ARP_RHYTHMS - 1; }

	void drawValue() override { display->setScrollingText(arpRhythmPatternNames[this->getValue()]); }

	void drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) override {
		char name[12];
		deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;

		// Index: Name
		snprintf(name, sizeof(name), "%d: %s", this->getValue(), arpRhythmPatternNames[this->getValue()]);
		canvas.drawStringCentred(name, yPixel + OLED_MAIN_TOPMOST_PIXEL, textWidth, textHeight);
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return !soundEditor.editingCVOrMIDIClip();
	}
};

} // namespace deluge::gui::menu_item::arpeggiator
