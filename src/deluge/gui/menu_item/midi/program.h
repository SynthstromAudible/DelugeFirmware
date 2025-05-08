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
#include "gui/menu_item/value.h"

#include "model/clip/instrument_clip.h"

namespace deluge::gui::menu_item::midi {
class Program : public Value<int32_t> {
public:
	using Value::Value;

	int32_t getBank() { return (this->getValue() >> 16) & 0x0000FF; }
	int32_t getSub() { return (this->getValue() >> 8) & 0x0000FF; }
	int32_t getPgm() { return this->getValue() & 0x0000FF; }

	void setBank(int32_t bank) { this->setValue((this->getValue() & 0x00FFFF) | (bank << 16)); }
	void setSub(int32_t sub) { this->setValue((this->getValue() & 0xFF00FF) | (sub << 8)); }
	void setPgm(int32_t pgm) { this->setValue((this->getValue() & 0xFFFF00) | pgm); }

	void selectEncoderAction(int32_t offset) final;
	void horizontalEncoderAction(int32_t offset) final;

	void readCurrentValue() override {
		auto& currentClip = *getCurrentInstrumentClip();
		this->setBank(currentClip.midiBank);
		this->setSub(currentClip.midiSub);
		this->setPgm(currentClip.midiPGM);
	}
	void writeCurrentValue() override {
		auto& currentClip = *getCurrentInstrumentClip();
		currentClip.midiBank = this->getBank();
		currentClip.midiSub = this->getSub();
		currentClip.midiPGM = this->getPgm();

		if (currentClip.isActiveOnOutput()) {
			currentClip.sendMIDIPGM();
		}
	}

	void drawValueAtPos(int32_t v, int32_t x);

	void drawValue() override {
		if (this->getPgm() == 128) {
			display->setText(l10n::get(l10n::String::STRING_FOR_NONE));
		}
		else {
			display->setTextAsNumber(this->getPgm() + 1);
		}
	}

	// OLED ONLY
	void drawPixelsForOled();

private:
	int32_t cursorPos = 2;
};
} // namespace deluge::gui::menu_item::midi
