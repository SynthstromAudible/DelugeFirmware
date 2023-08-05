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
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/formatted_title.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::osc {
class RetriggerPhase final : public Decimal, public FormattedTitle {
public:
	RetriggerPhase(const std::string& newName, const fmt::format_string<int32_t>& title_format_str,
	               bool newForModulator = false)
	    : Decimal(newName), FormattedTitle(title_format_str), forModulator(newForModulator) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	[[nodiscard]] int32_t getMinValue() const override { return -soundEditor.numberEditSize; }
	[[nodiscard]] int32_t getMaxValue() const override { return 360; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 0; }
	[[nodiscard]] int32_t getDefaultEditPos() const override { return 1; }

	void readCurrentValue() override {
		uint32_t value = *getValueAddress();
		if (value == 0xFFFFFFFF) {
			this->value_ = -soundEditor.numberEditSize;
		}
		else {
			this->value_ = value / 11930464;
		}
	}

	void writeCurrentValue() override {
		uint32_t value;
		if (this->value_ < 0) {
			value = 0xFFFFFFFF;
		}
		else {
			value = this->value_ * 11930464;
		}
		*getValueAddress() = value;
	}

	void drawValue() override {
		if (this->value_ < 0) {
			numericDriver.setText("OFF", false, 255, true);
		}
		else {
			Decimal::drawValue();
		}
	}

#if HAVE_OLED
	void drawPixelsForOled() override {
		if (this->value_ < 0) {
			OLED::drawStringCentred("OFF", 20, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, kTextHugeSpacingX,
			                        kTextHugeSizeY);
		}
		else {
			Decimal::drawPixelsForOled();
		}
	}
#endif
	void horizontalEncoderAction(int32_t offset) override {
		if (this->value_ >= 0) {
			Decimal::horizontalEncoderAction(offset);
		}
	}

	bool isRelevant(Sound* sound, int32_t whichThing) override {
		Source* source = &sound->sources[whichThing];
		if (forModulator && sound->getSynthMode() != SynthMode::FM) {
			return false;
		}
		return (source->oscType != OscType::SAMPLE || sound->getSynthMode() == SynthMode::FM);
	}

private:
	bool forModulator;
	[[nodiscard]] uint32_t* getValueAddress() const {
		if (forModulator) {
			return &soundEditor.currentSound->modulatorRetriggerPhase[soundEditor.currentSourceIndex];
		}
		return &soundEditor.currentSound->oscRetriggerPhase[soundEditor.currentSourceIndex];
	}
};
} // namespace deluge::gui::menu_item::osc
