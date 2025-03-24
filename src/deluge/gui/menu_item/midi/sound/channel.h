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
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::midi::sound {

class OutputMidiChannel final : public Integer {
public:
	using Integer::Integer;
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 16; }
	void readCurrentValue() override {
		int32_t value = soundEditor.currentSound->outputMidiChannel;
		if (value == MIDI_CHANNEL_NONE) {
			value = 0;
		}
		else {
			value = value + 1;
		}
		this->setValue(value);
	}
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		int32_t value = this->getValue();
		if (value == 0) {
			value = MIDI_CHANNEL_NONE;
		}
		else {
			value = value - 1;
		}
		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->outputMidiChannel = value;
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->outputMidiChannel = value;
		}
	}

	void drawValue() override {
		int32_t value = this->getValue();
		if (value == 0) {
			display->setScrollingText(l10n::get(l10n::String::STRING_FOR_OFF));
		}
		else {
			char name[12];
			snprintf(name, sizeof(name), "%d", value);
			display->setScrollingText(name);
		}
	}

	void drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) override {
		deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
		int32_t value = this->getValue();
		if (value == 0) {
			canvas.drawStringCentred(l10n::get(l10n::String::STRING_FOR_OFF), yPixel + OLED_MAIN_TOPMOST_PIXEL,
			                         textWidth, textHeight);
		}
		else {
			char name[12];
			snprintf(name, sizeof(name), "%d", value);
			canvas.drawStringCentred(name, yPixel + OLED_MAIN_TOPMOST_PIXEL, textWidth, textHeight);
		}
	}
};
} // namespace deluge::gui::menu_item::midi::sound
