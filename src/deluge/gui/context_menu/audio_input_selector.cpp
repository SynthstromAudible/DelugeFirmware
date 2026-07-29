/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/context_menu/audio_input_selector.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "gui/views/session_view.h"
#include "model/song/song.h"
#include "processing/audio_output.h"

extern AudioInputChannel defaultAudioOutputInputChannel;

namespace deluge::gui::context_menu {

enum class AudioInputSelector::Value {
	OFF,
	LEFT,
	RIGHT,
	STEREO,
	BALANCED,
	MASTER,
	OUTPUT,
	TRACK,
};
constexpr size_t kNumValues = 8;

AudioInputSelector audioInputSelector{};

namespace {
Output* getRecordableOutputInSong(AudioOutput* audioOutput, Output* selectedOutput) {
	if (!audioOutput->canRecordFrom(selectedOutput)) {
		return nullptr;
	}

	for (Output* output = currentSong->firstOutput; output; output = output->next) {
		if (output == selectedOutput) {
			return selectedOutput;
		}
	}

	return nullptr;
}

Output* getFirstRecordableOutput(AudioOutput* audioOutput) {
	for (Output* output = currentSong->firstOutput; output; output = output->next) {
		if (audioOutput->canRecordFrom(output)) {
			return output;
		}
	}

	return nullptr;
}
} // namespace

char const* AudioInputSelector::getTitle() {
	using enum l10n::String;
	return l10n::get(STRING_FOR_AUDIO_SOURCE);
}

std::span<const char*> AudioInputSelector::getOptions() {
	using enum l10n::String;
	static const char* options[] = {
	    l10n::get(STRING_FOR_DISABLED),     l10n::get(STRING_FOR_LEFT_INPUT),     l10n::get(STRING_FOR_RIGHT_INPUT),
	    l10n::get(STRING_FOR_STEREO_INPUT), l10n::get(STRING_FOR_BALANCED_INPUT), l10n::get(STRING_FOR_MIX_PRE_FX),
	    l10n::get(STRING_FOR_MIX_POST_FX),  l10n::get(STRING_FOR_TRACK),
	};
	return {options, kNumValues};
}

bool AudioInputSelector::setupAndCheckAvailability() {
	Value valueOption = Value::OFF;

	switch (audioOutput->inputChannel) {
	case AudioInputChannel::LEFT:
		valueOption = Value::LEFT;
		break;

	case AudioInputChannel::RIGHT:
		valueOption = Value::RIGHT;
		break;

	case AudioInputChannel::STEREO:
		valueOption = Value::STEREO;
		break;

	case AudioInputChannel::BALANCED:
		valueOption = Value::BALANCED;
		break;

	case AudioInputChannel::MIX:
		valueOption = Value::MASTER;
		break;

	case AudioInputChannel::OUTPUT:
		valueOption = Value::OUTPUT;
		break;

	case AudioInputChannel::SPECIFIC_OUTPUT:
		valueOption = Value::TRACK;
		break;

	default:
		valueOption = Value::OFF;
	}

	currentOption = static_cast<int32_t>(valueOption);

	scrollPos = currentOption;
	return true;
}

bool AudioInputSelector::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*rows = getRootUI()->getGreyedOutRowsNotRepresentingOutput(audioOutput);
	return true;
}

void AudioInputSelector::selectEncoderAction(int8_t offset) {
	if (currentUIMode != 0u) {
		return;
	}

	ContextMenu::selectEncoderAction(offset);

	auto valueOption = static_cast<Value>(currentOption);
	if (display->haveOLED() && valueOption == Value::TRACK) {
		scrollPos = currentOption;
	}

	// When switching away from SPECIFIC_OUTPUT, clear the recording-from state
	// so the previously-selected track is no longer silently muted
	if (audioOutput->inputChannel == AudioInputChannel::SPECIFIC_OUTPUT && valueOption != Value::TRACK) {
		audioOutput->clearRecordingFrom();
	}

	switch (valueOption) {

	case Value::LEFT:
		audioOutput->inputChannel = AudioInputChannel::LEFT;
		break;

	case Value::RIGHT:
		audioOutput->inputChannel = AudioInputChannel::RIGHT;
		break;

	case Value::STEREO:
		audioOutput->inputChannel = AudioInputChannel::STEREO;
		break;

	case Value::BALANCED:
		audioOutput->inputChannel = AudioInputChannel::BALANCED;
		break;

	case Value::MASTER:
		audioOutput->inputChannel = AudioInputChannel::MIX;
		break;

	case Value::OUTPUT:
		audioOutput->inputChannel = AudioInputChannel::OUTPUT;
		break;
	case Value::TRACK: {
		audioOutput->inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
		Output* recordFrom = getRecordableOutputInSong(audioOutput, audioOutput->getOutputRecordingFrom());
		if (!recordFrom) {
			recordFrom = getFirstRecordableOutput(audioOutput);
		}
		audioOutput->setOutputRecordingFrom(recordFrom);
		break;
	}

	default:
		audioOutput->inputChannel = AudioInputChannel::NONE;
	}

	defaultAudioOutputInputChannel = audioOutput->inputChannel;

	if (display->haveOLED()) {
		renderUIsForOled();
	}
}

// if they're in session view and press a clip's pad, record from that output
ActionResult AudioInputSelector::padAction(int32_t x, int32_t y, int32_t on) {
	if (on && getUIUpOneLevel() == &sessionView) {
		auto track = (&sessionView)->getOutputFromPad(x, y);
		if (audioOutput->canRecordFrom(track)) {
			audioOutput->inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
			audioOutput->setOutputRecordingFrom(track);
			if (display->have7SEG()) {
				display->popupTextTemporary(track->name.get());
			}
			// sets scroll to the position of specific output
			scrollPos = static_cast<int32_t>(Value::TRACK);
			currentOption = scrollPos;
			renderUIsForOled();
		}
		else if (track && track == audioOutput) {
			display->popupTextTemporary("Can't record self!");
		}
		else if (track) {
			display->popupTextTemporary("Can't record MIDI or CV!");
		}

		return ActionResult::DEALT_WITH;
	}
	return ContextMenu::padAction(x, y, on);
}

void AudioInputSelector::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	ContextMenu::renderOLED(canvas);

	if (audioOutput->inputChannel != AudioInputChannel::SPECIFIC_OUTPUT) {
		return;
	}

	Output* recordFrom = getRecordableOutputInSong(audioOutput, audioOutput->getOutputRecordingFrom());
	char const* trackName = recordFrom ? recordFrom->name.get() : "No track";

	int32_t windowHeight = 40;
	int32_t windowMinY = (OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1;
	int32_t textPixelY = windowMinY + 20 + kTextSpacingY;
	canvas.drawString(trackName, 22, textPixelY, kTextSpacingX, kTextSpacingY, 0, OLED_MAIN_WIDTH_PIXELS - 26);
}

} // namespace deluge::gui::context_menu
