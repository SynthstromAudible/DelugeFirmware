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

	void beginSession(MenuItem* navigatedBackwardFrom) {
		audioOutputBeingEdited = (AudioOutput*)getCurrentOutput();
		if (audioOutputBeingEdited->outputRecordingFrom) {
			outputIndex = currentSong->getOutputIndex(audioOutputBeingEdited->outputRecordingFrom);
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
		audioOutputBeingEdited->outputRecordingFrom->setRenderingToAudioOutput(false);
		auto newRecordingFrom = currentSong->getOutputFromIndex(outputIndex);
		audioOutputBeingEdited->outputRecordingFrom = newRecordingFrom;
		audioOutputBeingEdited->outputRecordingFrom->setRenderingToAudioOutput(audioOutputBeingEdited->echoing);
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawFor7seg(); // Probably not necessary either...
		}
	}
	void drawPixelsForOled() override {
		char const* text = audioOutputBeingEdited->outputRecordingFrom->name.get();
		deluge::hid::display::OLED::main.drawStringCentred(text, 20 + OLED_MAIN_TOPMOST_PIXEL, kTextBigSpacingX,
		                                                   kTextBigSizeY);
	}

	void drawFor7seg() {
		char const* text = audioOutputBeingEdited->outputRecordingFrom->name.get();
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
