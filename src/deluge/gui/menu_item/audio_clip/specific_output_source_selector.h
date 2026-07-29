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
		numOutputs = currentSong->getNumOutputs();

		Output* selectedOutput = audioOutputBeingEdited->getOutputRecordingFrom();
		outputIndex = getRecordableOutputIndex(selectedOutput);
		if (outputIndex < 0) {
			// If the stored source was removed or became invalid, land on the first valid target instead.
			outputIndex = getNextRecordableOutputIndex(-1, 1);
			selectedOutput = getOutputFromSelectedIndex();
			audioOutputBeingEdited->setOutputRecordingFrom(selectedOutput);
		}

		if (outputIndex < 0) {
			outputIndex = 0;
		}
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawFor7seg(); // Probably not necessary either...
		}
	}

	void selectEncoderAction(int32_t offset) override {
		int32_t newOutputIndex = getNextRecordableOutputIndex(outputIndex, offset);
		if (newOutputIndex < 0) {
			return;
		}
		outputIndex = newOutputIndex;
		auto newRecordingFrom = getOutputFromSelectedIndex();
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
		Output* output = getOutputFromSelectedIndex();
		if (!output) {
			canvas.drawStringCentred("No track", OLED_MAIN_TOPMOST_PIXEL + 21, kTextSpacingX, kTextSpacingY);
			return;
		}

		// track type
		OutputType outputType = output->type;

		// for midi instruments, get the channel
		int32_t channel = 0;
		if (outputType == OutputType::MIDI_OUT) {
			Instrument* instrument = (Instrument*)output;
			channel = ((NonAudioInstrument*)instrument)->getChannel();
		}

		char const* outputTypeText = getOutputTypeName(outputType, channel);

		// draw the track type
		canvas.drawStringCentred(outputTypeText, OLED_MAIN_TOPMOST_PIXEL + 14, kTextSpacingX, kTextSpacingY);

		int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 28;

		// draw the track name
		char const* name = output->name.get();

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
		Output* output = getOutputFromSelectedIndex();
		char const* text = output ? output->name.get() : "No track";
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

private:
	bool canRecordFromOutput(Output* output) const { return audioOutputBeingEdited->canRecordFrom(output); }

	int32_t getRecordableOutputIndex(Output* output) const {
		if (!canRecordFromOutput(output)) {
			return -1;
		}

		// Only outputs still in the main song list are selectable; hibernated instruments should not display here.
		int32_t index = 0;
		for (Output* candidate = currentSong->firstOutput; candidate; candidate = candidate->next) {
			if (candidate == output) {
				return index;
			}
			index++;
		}

		return -1;
	}

	Output* getOutputFromSelectedIndex() const {
		Output* output = currentSong->getOutputFromIndex(outputIndex);
		return canRecordFromOutput(output) ? output : nullptr;
	}

	int32_t getNextRecordableOutputIndex(int32_t startIndex, int32_t offset) const {
		if (offset == 0) {
			return getOutputFromSelectedIndex()
			           ? outputIndex
			           : getRecordableOutputIndex(audioOutputBeingEdited->getOutputRecordingFrom());
		}

		int32_t direction = offset > 0 ? 1 : -1;
		int32_t steps = offset > 0 ? offset : -offset;
		int32_t selectedIndex = startIndex;

		for (int32_t step = 0; step < steps; step++) {
			int32_t candidateIndex = selectedIndex;
			// Walk the raw song indices, but stop only on rows the audio track can actually record from.
			while (true) {
				candidateIndex += direction;
				if (candidateIndex < 0 || candidateIndex >= numOutputs) {
					return selectedIndex;
				}

				Output* candidate = currentSong->getOutputFromIndex(candidateIndex);
				if (canRecordFromOutput(candidate)) {
					selectedIndex = candidateIndex;
					break;
				}
			}
		}

		return selectedIndex;
	}
};
} // namespace deluge::gui::menu_item::audio_clip
