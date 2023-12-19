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
		yPixel = 20;

		char const* differentiationString;
		if (MIDIDeviceManager::differentiatingInputsByDevice) {
			differentiationString = l10n::get(l10n::String::STRING_FOR_INPUT_DIFFERENTIATION_ON);
		}
		else {
			differentiationString = l10n::get(l10n::String::STRING_FOR_INPUT_DIFFERENTIATION_OFF);
		}
		deluge::hid::display::OLED::drawString(differentiationString, 0, yPixel, deluge::hid::display::OLED::oledMainImage[0],
		                                       OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSizeYUpdated);

		yPixel += kTextSpacingY;

		char const* deviceString = l10n::get(l10n::String::STRING_FOR_FOLLOW_DEVICE_UNASSIGNED);
		if (midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)].device) {
			deviceString = midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)]
			                   .device->getDisplayName();
		}
		deluge::hid::display::OLED::drawString(deviceString, 0, yPixel, deluge::hid::display::OLED::oledMainImage[0],
		                                       OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSizeYUpdated);
		deluge::hid::display::OLED::setupSideScroller(0, deviceString, kTextSpacingX, OLED_MAIN_WIDTH_PIXELS, yPixel,
		                                              yPixel + 8, kTextSpacingX, kTextSpacingY, false);

		yPixel += kTextSpacingY;

		char const* channelText;
		if (midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)].channelOrZone
		    == MIDI_CHANNEL_MPE_LOWER_ZONE) {
			channelText = l10n::get(l10n::String::STRING_FOR_MPE_LOWER_ZONE);
		}
		else if (midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)].channelOrZone
		         == MIDI_CHANNEL_MPE_UPPER_ZONE) {
			channelText = l10n::get(l10n::String::STRING_FOR_MPE_UPPER_ZONE);
		}
		else {
			channelText = l10n::get(l10n::String::STRING_FOR_CHANNEL);
			char buffer[12];
			int32_t channelmod =
			    (midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)].channelOrZone
			     >= IS_A_CC)
			    * IS_A_CC;
			intToString(midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)].channelOrZone
			                + 1 - channelmod,
			            buffer, 1);
			deluge::hid::display::OLED::drawString(buffer, kTextSpacingX * 8, yPixel,
			                                       deluge::hid::display::OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
			                                       kTextSpacingX, kTextSizeYUpdated);
		}
		deluge::hid::display::OLED::drawString(channelText, 0, yPixel, deluge::hid::display::OLED::oledMainImage[0],
		                                       OLED_MAIN_WIDTH_PIXELS, kTextSpacingX, kTextSizeYUpdated);
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

	void unlearnAction() {
		midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)].device = NULL;
		if (soundEditor.getCurrentMenuItem() == this) {
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			else {
				drawValue();
			}
		}
		else {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_UNLEARNED));
		}
	}

	bool learnNoteOn(MIDIDevice* device, int32_t channel, int32_t noteCode) {
		this->setValue(channel);
		midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)].device = device;
		midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::KIT)].channelOrZone = channel;

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
