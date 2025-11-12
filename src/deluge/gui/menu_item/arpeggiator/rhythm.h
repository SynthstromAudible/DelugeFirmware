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
#include "modulation/arpeggiator_rhythms.h"
#include <sys/_intsup.h>

namespace deluge::gui::menu_item::arpeggiator {

using namespace hid::display;

class Rhythm final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxPresetArpRhythm; }

	void drawValue() override { display->setScrollingText(arpRhythmPatternNames[this->getValue()]); }

	void drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) override {
		char name[12];
		// Index: Name
		snprintf(name, sizeof(name), "%d: %s", this->getValue(), arpRhythmPatternNames[this->getValue()]);
		OLED::main.drawStringCentred(name, yPixel + OLED_MAIN_TOPMOST_PIXEL, textWidth, textHeight);
	}

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		oled_canvas::Canvas& image = OLED::main;

		const auto value = this->getValue();
		const auto pattern = std::string_view(arpRhythmPatternNames[value]);
		if (value == 0) {
			return image.drawStringCentered(pattern.data(), slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
			                                kTextSpacingX, kTextSpacingY, slot.width);
		}

		constexpr int32_t paddingBetween = 2;
		const int32_t rhythmWidth = pattern.size() * kTextSpacingX + pattern.size() * paddingBetween;

		int32_t x = slot.start_x + (slot.width - rhythmWidth) / 2 + 2;
		for (const char character : pattern) {
			image.drawChar(character == '0' ? 'X' : character, x, slot.start_y + kHorizontalMenuSlotYOffset,
			               kTextSpacingX, kTextSpacingY);
			x += kTextSpacingX + paddingBetween;
		}
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return !soundEditor.editingCVOrMIDIClip() && !soundEditor.editingNonAudioDrumRow();
	}

protected:
	void configureRenderingOptions(const HorizontalMenuRenderingOptions &options) override {
		options.occupied_slots = 2;
	}
};

} // namespace deluge::gui::menu_item::arpeggiator
