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
#include "gui/menu_item/integer.h"
#include "hid/display/oled.h"
#include "processing/engines/audio_engine.h"

namespace deluge::gui::menu_item::reverb::sidechain {

class Volume final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->setValue(AudioEngine::reverbSidechainVolume / 21474836); }
	void writeCurrentValue() override {
		AudioEngine::reverbSidechainVolume = this->getValue() * 21474836;
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] int32_t getMinValue() const override { return -1; }

	void drawValue() override {
		if (this->getValue() < 0) {
			display->setText(l10n::get(l10n::String::STRING_FOR_AUTO));
		}
		else {
			Integer::drawValue();
		}
	}

	void drawPixelsForOled() override {
		oled_canvas::Canvas& canvas = OLED::main;
		if (getValue() < 0) {
			canvas.drawStringCentred(l10n::get(l10n::String::STRING_FOR_AUTO), 18 + OLED_MAIN_TOPMOST_PIXEL,
			                         kTextHugeSpacingX, kTextHugeSizeY);
		}
		else {
			Integer::drawPixelsForOled();
		}
	}

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		oled_canvas::Canvas& canvas = OLED::main;
		if (getValue() < 0) {
			const char* string_for_auto = l10n::get(l10n::String::STRING_FOR_AUTO);
			canvas.drawStringCentered(string_for_auto, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
			                          kTextSpacingX, kTextSpacingY, slot.width);
		}
		else {
			drawSidechainDucking(slot);
		}
	}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		Integer::configureRenderingOptions(options);

		options.label = l10n::get(l10n::String::STRING_FOR_VOLUME_DUCKING_SHORT);
		options.notification_value =
		    getValue() < 0 ? deluge::l10n::get(l10n::String::STRING_FOR_AUTO) : std::to_string(getValue());
	}
};

} // namespace deluge::gui::menu_item::reverb::sidechain
