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
#include "definitions_cxx.hpp"
#include "gui/menu_item/arpeggiator/midi_cv/arp_integer.h"
#include "gui/menu_item/value_scaling.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include "model/song/song.h"
#include "modulation/arpeggiator_rhythms.h"

namespace deluge::gui::menu_item::arpeggiator::midi_cv {

using namespace hid::display;

class Rhythm final : public ArpNonSoundInteger {
public:
	using ArpNonSoundInteger::ArpNonSoundInteger;
	void readCurrentValue() override {
		this->setValue(computeCurrentValueForUnsignedMenuItem(soundEditor.currentArpSettings->rhythm));
	}
	void writeCurrentValue() override {
		int32_t value = computeFinalValueForUnsignedMenuItem(this->getValue());
		soundEditor.currentArpSettings->rhythm = value;
	}

	void drawValue() override { display->setScrollingText(arpRhythmPatternNames[this->getValue()]); }

	void drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) override {
		oled_canvas::Canvas& canvas = OLED::main;

		canvas.drawStringCentred(arpRhythmPatternNames[this->getValue()], yPixel + OLED_MAIN_TOPMOST_PIXEL, textWidth,
		                         textHeight);
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		oled_canvas::Canvas& image = OLED::main;

		const auto value = this->getValue();
		const auto pattern = std::string_view(arpRhythmPatternNames[value]);
		if (value == 0) {
			return image.drawStringCentered(pattern.data(), slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
			                                kTextSpacingX, kTextSpacingY, slot.width);
		}

		constexpr int32_t paddingBetween = 2;
		const int32_t rhythmWidth = pattern.size() * kTextSpacingX + pattern.size() * paddingBetween;

		int32_t x = slot.start_x + (slot.width - rhythmWidth) / 2 + 1;
		for (const char character : pattern) {
			image.drawChar(character == '0' ? 'X' : character, x, slot.start_y + kHorizontalMenuSlotYOffset,
			               kTextSpacingX, kTextSpacingY);
			x += kTextSpacingX + paddingBetween;
		}
	}

protected:
	[[nodiscard]] int32_t getOccupiedSlots() const override { return 2; }
};
} // namespace deluge::gui::menu_item::arpeggiator::midi_cv
