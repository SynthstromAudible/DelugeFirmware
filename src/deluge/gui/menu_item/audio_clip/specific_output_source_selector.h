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
#include "gui/context_menu/audio_input_selector.h"
#include "gui/menu_item/menu_item.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/song/song.h"
#include "processing/audio_output.h"

namespace deluge::gui::menu_item::audio_clip {
class SpecificSourceOutputSelector final : public MenuItem {
public:
	using MenuItem::MenuItem;

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		audioOutputBeingEdited = (AudioOutput*)getCurrentOutput();
		if (audioOutputBeingEdited->getOutputRecordingFrom()) {
			outputIndex = currentSong->getOutputIndex(audioOutputBeingEdited->getOutputRecordingFrom());
		}
		else {
			outputIndex = 0;
		}
		numOutputs = currentSong->getNumOutputs();
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawFor7seg(); // Probably not necessary either...
		}
	}

	void selectEncoderAction(int32_t offset) override {
		outputIndex += offset;
		outputIndex = std::clamp<int32_t>(outputIndex, 0, numOutputs - 1);
		auto newRecordingFrom = currentSong->getOutputFromIndex(outputIndex);
		audioOutputBeingEdited->setOutputRecordingFrom(newRecordingFrom);
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawFor7seg(); // Probably not necessary either...
		}
	}
	void drawPixelsForOled() override {
		deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;

		// track
		Output* output = currentSong->getOutputFromIndex(outputIndex);

		// track type
		OutputType outputType = output->type;

		// for midi instruments, get the channel
		int32_t channel;
		if (outputType == OutputType::MIDI_OUT) {
			Instrument* instrument = (Instrument*)output;
			channel = ((NonAudioInstrument*)instrument)->getChannel();
		}

		char const* outputTypeText = getOutputTypeName(outputType, channel, output);

		// draw the track type
		canvas.drawStringCentred(outputTypeText, OLED_MAIN_TOPMOST_PIXEL + 14, kTextSpacingX, kTextSpacingY);

		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 28;

		// draw the track name
		char const* name = audioOutputBeingEdited->getOutputRecordingFrom()->name.get();

		int32_t stringLengthPixels = canvas.getStringWidthInPixels(name, kTextTitleSizeY);

		if (stringLengthPixels <= OLED_MAIN_WIDTH_PIXELS) {
			canvas.drawStringCentred(name, yPos, kTextTitleSpacingX, kTextTitleSizeY);
		}
		else {
			canvas.drawString(name, 0, yPos, kTextTitleSpacingX, kTextTitleSizeY);
			deluge::hid::display::OLED::setupSideScroller(0, name, 0, OLED_MAIN_WIDTH_PIXELS, yPos,
			                                              yPos + kTextTitleSizeY, kTextTitleSpacingX, kTextTitleSizeY,
			                                              false);
		}
	}

	void drawFor7seg() {
		char const* text = audioOutputBeingEdited->getOutputRecordingFrom()->name.get();
		display->setScrollingText(text, 0);
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		audioOutputBeingEdited = (AudioOutput*)getCurrentOutput();
		return audioOutputBeingEdited->inputChannel == AudioInputChannel::SPECIFIC_OUTPUT;
	}

	bool shouldEnterSubmenu() override { return true; }

	AudioOutput* audioOutputBeingEdited{nullptr};
	// this is the index that the output is recording from
	int32_t outputIndex{0};
	int32_t numOutputs{0};
};
} // namespace deluge::gui::menu_item::audio_clip
