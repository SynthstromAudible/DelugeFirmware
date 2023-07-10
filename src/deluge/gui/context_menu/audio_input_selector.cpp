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
#include "gui/ui/root_ui.h"
#include "processing/audio_output.h"
#include "hid/matrix/matrix_driver.h"
#include "hid/display/numeric_driver.h"
#include "hid/led/indicator_leds.h"
#include "extern.h"
#include <cstddef>

extern int8_t defaultAudioOutputInputChannel;

namespace deluge::gui::context_menu {

enum class AudioInputSelector::Value {
	OFF,

	LEFT,
	LEFT_ECHO,

	RIGHT,
	RIGHT_ECHO,

	STEREO,
	STEREO_ECHO,

	BALANCED,
	BALANCED_ECHO,

	MASTER,

	OUTPUT,
};
constexpr size_t kNumValues = 11;

AudioInputSelector audioInputSelector{};

#if HAVE_OLED
char const* options[] = {"Off",
                         "Left input",
                         "Left input (monitoring)",
                         "Right input",
                         "Right input (monitoring)",
                         "Stereo input",
                         "Stereo input (monitoring)",
                         "Bal. input",
                         "Bal. input (monitoring)",
                         "Deluge mix (pre fx)",
                         "Deluge output (post fx)"};
#else
char const* options[] = {"OFF", "LEFT", "LEFT.", "RIGH", "RIGH.", "STER", "STER.", "BALA", "BALA.", "MIX", "OUTP"};
#endif

char const* AudioInputSelector::getTitle() {
	static char const* title = "Audio source";
	return title;
}

Sized<char const**> AudioInputSelector::getOptions() {
	return {options, kNumValues};
}

bool AudioInputSelector::setupAndCheckAvailability() {
	Value valueOption = Value::OFF;

	switch (audioOutput->inputChannel) {
	case AUDIO_INPUT_CHANNEL_LEFT:
		valueOption = Value::LEFT;
		break;

	case AUDIO_INPUT_CHANNEL_RIGHT:
		valueOption = Value::RIGHT;
		break;

	case AUDIO_INPUT_CHANNEL_STEREO:
		valueOption = Value::STEREO;
		break;

	case AUDIO_INPUT_CHANNEL_BALANCED:
		valueOption = Value::BALANCED;
		break;

	case AUDIO_INPUT_CHANNEL_MIX:
		valueOption = Value::MASTER;
		break;

	case AUDIO_INPUT_CHANNEL_OUTPUT:
		valueOption = Value::OUTPUT;
		break;

	default:
		valueOption = Value::OFF;
	}

	currentOption = static_cast<int>(valueOption);

	if (audioOutput->echoing) {
		currentOption += 1;
	}
#if HAVE_OLED
	scrollPos = currentOption;
#endif
	return true;
}

bool AudioInputSelector::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	*rows = getRootUI()->getGreyedOutRowsNotRepresentingOutput(audioOutput);
	return true;
}

void AudioInputSelector::selectEncoderAction(int8_t offset) {
	if (currentUIMode != 0u) {
		return;
	}

	ContextMenu::selectEncoderAction(offset);

	audioOutput->echoing = false;

	auto valueOption = static_cast<Value>(currentOption);

	switch (valueOption) {
	case Value::LEFT_ECHO:
		audioOutput->echoing = true;
	case Value::LEFT:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_LEFT;
		break;

	case Value::RIGHT_ECHO:
		audioOutput->echoing = true;
	case Value::RIGHT:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_RIGHT;
		break;

	case Value::STEREO_ECHO:
		audioOutput->echoing = true;
	case Value::STEREO:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_STEREO;
		break;

	case Value::BALANCED_ECHO:
		audioOutput->echoing = true;
	case Value::BALANCED:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_BALANCED;
		break;

	case Value::MASTER:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_MIX;
		break;

	case Value::OUTPUT:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_OUTPUT;
		break;

	default:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_NONE;
	}

	defaultAudioOutputInputChannel = audioOutput->inputChannel;
}

} // namespace deluge::gui::context_menu
