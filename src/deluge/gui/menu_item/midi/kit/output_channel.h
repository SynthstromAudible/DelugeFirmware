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
#include "model/instrument/kit.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::midi::kit {

// Sets the Kit-level MIDI output channel. Stored on the Kit instrument (not per-drum Sound data),
// so the setting survives when drum row presets are swapped. Note sends/offs are injected in
// Kit::noteOnPreKitArp() / noteOffPreKitArp() using drum row index + Kit::outputMidiBaseNote.
class OutputMidiChannel final : public Integer {
public:
	using Integer::Integer;
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 16; }

	bool isRelevant(ModControllableAudio* /*modControllable*/, int32_t /*whichThing*/) override {
		return soundEditor.editingKitAffectEntire();
	}

	void readCurrentValue() override {
		Kit* kit = getCurrentKit();
		if (!kit) {
			this->setValue(0);
			return;
		}
		int32_t ch = kit->outputMidiChannel;
		this->setValue(ch == MIDI_CHANNEL_NONE ? 0 : ch + 1);
	}

	void writeCurrentValue() override {
		int32_t display = this->getValue();
		Kit* kit = getCurrentKit();
		if (!kit) {
			return;
		}
		if (display == 0) {
			kit->outputMidiChannel = MIDI_CHANNEL_NONE;
		}
		else {
			kit->outputMidiChannel = display - 1; // convert 1-based display to 0-based MIDI channel
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

} // namespace deluge::gui::menu_item::midi::kit
