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
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::osc {
class RetriggerPhase final : public Decimal, public FormattedTitle {
public:
	RetriggerPhase(l10n::String newName, l10n::String title_format_str, uint8_t source_id, bool newForModulator = false)
	    : Decimal(newName), FormattedTitle(title_format_str, source_id + 1), for_modulator_(newForModulator),
	      source_id_(source_id) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	[[nodiscard]] int32_t getMinValue() const override { return -1; }
	[[nodiscard]] int32_t getMaxValue() const override { return 360; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 0; }
	[[nodiscard]] int32_t getDefaultEditPos() const override { return 1; }

	void readCurrentValue() override {
		uint32_t value = *getValueAddress();
		if (value == 0xFFFFFFFF) {
			this->setValue(-1);
		}
		else {
			this->setValue(value / 11930464);
		}
	}

	void writeCurrentValue() override {
		uint32_t value;
		if (this->getValue() < 0) {
			value = 0xFFFFFFFF;
		}
		else {
			value = this->getValue() * 11930464;
		}
		*getValueAddress() = value;
	}

	void drawValue() override {
		if (this->getValue() < 0) {
			display->setText(l10n::get(l10n::String::STRING_FOR_DISABLED), false, 255, true);
		}
		else {
			Decimal::drawValue();
		}
	}

	void drawPixelsForOled() override {
		oled_canvas::Canvas& canvas = OLED::main;
		if (this->getValue() < 0) {
			canvas.drawStringCentred(l10n::get(l10n::String::STRING_FOR_OFF), 20, kTextHugeSpacingX, kTextHugeSizeY);
		}
		else {
			Decimal::drawPixelsForOled();
		}
	}

	void horizontalEncoderAction(int32_t offset) override {
		if (this->getValue() >= 0) {
			Decimal::horizontalEncoderAction(offset);
		}
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		const auto sound = static_cast<Sound*>(modControllable);
		Source& source = sound->sources[source_id_];

		if (for_modulator_ && sound->getSynthMode() != SynthMode::FM) {
			return false;
		}
		if (source.oscType == OscType::WAVETABLE) {
			return source.hasAtLeastOneAudioFileLoaded();
		}

		return source.oscType != OscType::SAMPLE || sound->getSynthMode() == SynthMode::FM;
	}

	int32_t getNumberEditSize() override {
		if (parent != nullptr && parent->renderingStyle() == Submenu::RenderingStyle::HORIZONTAL) {
			// In Horizontal menus we use 0.10 step by default, and 0.01 step for fine editing
			return Buttons::isAnyOfButtonsPressed({hid::button::SELECT_ENC, hid::button::SHIFT}) ? 1 : 10;
		}
		return soundEditor.numberEditSize;
	}

	void selectEncoderAction(int32_t offset) override {
		if (offset > 0 && getValue() < 0) {
			setValue(-getNumberEditSize());
		}
		Decimal::selectEncoderAction(offset);
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		oled_canvas::Canvas& canvas = OLED::main;
		if (this->getValue() < 0) {
			const char* off = l10n::get(l10n::String::STRING_FOR_OFF);
			return canvas.drawStringCentered(off, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
			                                 kTextSpacingX, kTextSpacingY, slot.width);
		}

		return Decimal::renderInHorizontalMenu(slot);
	}

	void getNotificationValue(StringBuf& valueBuf) override {
		if (this->getValue() < 0) {
			return valueBuf.append(l10n::get(l10n::String::STRING_FOR_OFF));
		}
		valueBuf.appendInt(getValue());
	}

	void getColumnLabel(StringBuf& label) override {
		label.append(l10n::get(l10n::String::STRING_FOR_RETRIGGER_PHASE_SHORT));
	}

	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return SLIDER; }

private:
	bool for_modulator_;
	uint8_t source_id_;

	[[nodiscard]] uint32_t* getValueAddress() const {
		if (for_modulator_) {
			return &soundEditor.currentSound->modulatorRetriggerPhase[source_id_];
		}
		return &soundEditor.currentSound->oscRetriggerPhase[source_id_];
	}
};
} // namespace deluge::gui::menu_item::osc
