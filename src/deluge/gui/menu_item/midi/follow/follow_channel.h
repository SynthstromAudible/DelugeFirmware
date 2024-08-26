/*
 * Copyright (c) 2024 Sean Ditny
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
#include "hid/display/oled.h"
#include "io/midi/midi_engine.h"

class MIDIDevice;

namespace deluge::gui::menu_item::midi {
class FollowChannel final : public Integer {
public:
	using Integer::Integer;

	FollowChannel(l10n::String newName, l10n::String title, MIDIFollowChannelType type)
	    : Integer(newName, title), channelType(type),
	      midiInput(midiEngine.midiFollowChannelType[util::to_underlying(type)]) {}

	void readCurrentValue() override { this->setValue(midiInput.channelOrZone); }
	void writeCurrentValue() override { midiInput.channelOrZone = this->getValue(); }
	[[nodiscard]] int32_t getMaxValue() const override { return NUM_CHANNELS; }
	bool allowsLearnMode() override { return true; }

	void drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) override {
		deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;

		yPixel = 20;

		char const* differentiationString;
		if (MIDIDeviceManager::differentiatingInputsByDevice) {
			differentiationString = l10n::get(l10n::String::STRING_FOR_INPUT_DIFFERENTIATION_ON);
		}
		else {
			differentiationString = l10n::get(l10n::String::STRING_FOR_INPUT_DIFFERENTIATION_OFF);
		}
		canvas.drawString(differentiationString, 0, yPixel, kTextSpacingX, kTextSizeYUpdated);

		yPixel += kTextSpacingY;

		char const* deviceString = l10n::get(l10n::String::STRING_FOR_FOLLOW_DEVICE_UNASSIGNED);
		if (midiInput.device) {
			deviceString = midiInput.device->getDisplayName();
		}
		canvas.drawString(deviceString, 0, yPixel, kTextSpacingX, kTextSizeYUpdated);
		deluge::hid::display::OLED::setupSideScroller(0, deviceString, kTextSpacingX, OLED_MAIN_WIDTH_PIXELS, yPixel,
		                                              yPixel + 8, kTextSpacingX, kTextSpacingY, false);

		yPixel += kTextSpacingY;

		char const* channelText;
		if (this->getValue() == MIDI_CHANNEL_MPE_LOWER_ZONE) {
			channelText = l10n::get(l10n::String::STRING_FOR_MPE_LOWER_ZONE);
		}
		else if (this->getValue() == MIDI_CHANNEL_MPE_UPPER_ZONE) {
			channelText = l10n::get(l10n::String::STRING_FOR_MPE_UPPER_ZONE);
		}
		else if (this->getValue() == MIDI_CHANNEL_NONE) {
			channelText = l10n::get(l10n::String::STRING_FOR_FOLLOW_CHANNEL_UNASSIGNED);
		}
		else {
			channelText = l10n::get(l10n::String::STRING_FOR_CHANNEL);
			char buffer[12];
			int32_t channelmod = (midiInput.channelOrZone >= IS_A_CC) * IS_A_CC;
			intToString(midiInput.channelOrZone + 1 - channelmod, buffer, 1);
			canvas.drawString(buffer, kTextSpacingX * 8, yPixel, kTextSpacingX, kTextSizeYUpdated);
		}
		canvas.drawString(channelText, 0, yPixel, kTextSpacingX, kTextSizeYUpdated);
	}

	void drawValue() override {
		if (this->getValue() == MIDI_CHANNEL_MPE_LOWER_ZONE) {
			display->setText(l10n::get(l10n::String::STRING_FOR_MPE_LOWER_ZONE));
		}
		else if (this->getValue() == MIDI_CHANNEL_MPE_UPPER_ZONE) {
			display->setText(l10n::get(l10n::String::STRING_FOR_MPE_UPPER_ZONE));
		}
		else if (this->getValue() == MIDI_CHANNEL_NONE) {
			display->setText(l10n::get(l10n::String::STRING_FOR_NONE));
		}
		else {
			display->setTextAsNumber(this->getValue() + 1);
		}
	}

	void selectEncoderAction(int32_t offset) override {
		if (this->getValue() == MIDI_CHANNEL_NONE) {
			if (offset > 0) {
				this->setValue(0);
			}
			else if (offset < 0) {
				this->setValue(MIDI_CHANNEL_MPE_UPPER_ZONE);
			}
		}
		else {
			this->setValue(this->getValue() + offset);
			if ((this->getValue() >= NUM_CHANNELS) || (this->getValue() < 0)) {
				this->setValue(MIDI_CHANNEL_NONE);
				midiInput.clear();
				renderDisplay();
				return;
			}
		}
		Number::selectEncoderAction(offset);
	}

	void unlearnAction() {
		this->setValue(MIDI_CHANNEL_NONE);
		midiInput.clear();
		if (soundEditor.getCurrentMenuItem() == this) {
			renderDisplay();
		}
		else {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_UNLEARNED));
		}
	}

	bool learnNoteOn(MIDIDevice* device, int32_t channel, int32_t noteCode) {
		this->setValue(channel);
		midiInput.device = device;
		midiInput.channelOrZone = channel;

		if (soundEditor.getCurrentMenuItem() == this) {
			renderDisplay();
		}
		else {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_LEARNED));
		}

		return true;
	}

	void learnCC(MIDIDevice* device, int32_t channel, int32_t ccNumber, int32_t value) {
		this->setValue(channel);
		midiInput.device = device;
		midiInput.channelOrZone = channel;

		if (soundEditor.getCurrentMenuItem() == this) {
			renderDisplay();
		}
		else {
			display->displayPopup(l10n::get(l10n::String::STRING_FOR_LEARNED));
		}
	}

	void renderDisplay() {
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawValue();
		}
	}

private:
	MIDIFollowChannelType channelType;
	LearnedMIDI& midiInput;
};
} // namespace deluge::gui::menu_item::midi
