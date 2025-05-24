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
		deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;

		canvas.drawStringCentred(arpRhythmPatternNames[this->getValue()], yPixel + OLED_MAIN_TOPMOST_PIXEL, textWidth,
		                         textHeight);
	}

	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override {
		deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

		renderColumnLabel(startX, width, startY);

		// Render current value

		DEF_STACK_STRING_BUF(shortOpt, kShortStringBufferSize);
		char name[12];
		// Index:Name
		snprintf(name, sizeof(name), "%d: %s", this->getValue(), arpRhythmPatternNames[this->getValue()]);
		shortOpt.append(name);

		int32_t pxLen;
		// Trim characters from the end until it fits.
		while ((pxLen = image.getStringWidthInPixels(shortOpt.c_str(), kTextSpacingY)) >= width - 2) {
			shortOpt.truncate(shortOpt.size() - 1);
		}
		// Padding to center the string. If we can't center exactly, 1px left is better than 1px right.
		int32_t pad = ((width - pxLen) / 2) - 1;
		image.drawString(shortOpt.c_str(), startX + pad, startY + kTextSpacingY + 3, kTextSpacingX, kTextSpacingY, 0,
		                 startX + width - kTextSpacingX);
	}

protected:
	[[nodiscard]] int32_t getColumnSpan() const override { return 2; }
};
} // namespace deluge::gui::menu_item::arpeggiator::midi_cv
