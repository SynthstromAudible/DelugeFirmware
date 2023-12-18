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
#include "gui/ui/sound_editor.h"
#include "io/midi/midi_engine.h"

class MIDIDevice;

namespace deluge::gui::menu_item::midi {

class FollowChannelKit final : public Integer {
public:
	using Integer::Integer;

	void readCurrentValue() override {
		this->setValue(midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)].channelOrZone);
	}
	void writeCurrentValue() override {
		midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)].channelOrZone =
		    this->getValue();
	}
	[[nodiscard]] int32_t getMaxValue() const override { return NUM_CHANNELS; }
	bool allowsLearnMode() override { return true; }

	void drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) {
		char buffer[12];
		char const* channelText;
		if (this->getValue() == MIDI_CHANNEL_MPE_LOWER_ZONE) {
			channelText = l10n::get(l10n::String::STRING_FOR_MPE_LOWER_ZONE);
		}
		else if (this->getValue() == MIDI_CHANNEL_MPE_UPPER_ZONE) {
			channelText = l10n::get(l10n::String::STRING_FOR_MPE_UPPER_ZONE);
		}
		else {
			intToString(this->getValue() + 1, buffer, 1);
			channelText = buffer;
		}
		deluge::hid::display::OLED::drawStringCentred(channelText, yPixel + OLED_MAIN_TOPMOST_PIXEL,
		                                              deluge::hid::display::OLED::oledMainImage[0],
		                                              OLED_MAIN_WIDTH_PIXELS, textWidth, textHeight);
	}

	void drawValue() override {
		if (this->getValue() == MIDI_CHANNEL_MPE_LOWER_ZONE) {
			display->setText(l10n::get(l10n::String::STRING_FOR_MPE_LOWER_ZONE));
		}
		else if (this->getValue() == MIDI_CHANNEL_MPE_UPPER_ZONE) {
			display->setText(l10n::get(l10n::String::STRING_FOR_MPE_UPPER_ZONE));
		}
		else {
			display->setTextAsNumber(this->getValue() + 1);
		}
	}

	void selectEncoderAction(int32_t offset) override {
		this->setValue(this->getValue() + offset);
		if (this->getValue() >= NUM_CHANNELS) {
			this->setValue(this->getValue() - NUM_CHANNELS);
		}
		else if (this->getValue() < 0) {
			this->setValue(this->getValue() + NUM_CHANNELS);
		}
		Number::selectEncoderAction(offset);
	}

	bool learnNoteOn(MIDIDevice* device, int32_t channel, int32_t noteCode) {
		this->setValue(channel);
		midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)].device = device;

		if (soundEditor.getCurrentMenuItem() == this) {
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			else {
				drawValue();
			}
		}
		else {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_LEARNED));
		}

		return true;
	}
};
} // namespace deluge::gui::menu_item::midi
