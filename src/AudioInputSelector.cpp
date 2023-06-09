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

#include <AudioInputSelector.h>
#include <RootUI.h>
#include "AudioOutput.h"
#include "matrixdriver.h"
#include "numericdriver.h"
#include "IndicatorLEDs.h"
#include "extern.h"

#define VALUE_OFF 0

#define VALUE_LEFT 1
#define VALUE_LEFT_ECHO 2

#define VALUE_RIGHT 3
#define VALUE_RIGHT_ECHO 4

#define VALUE_STEREO 5
#define VALUE_STEREO_ECHO 6

#define VALUE_BALANCED 7
#define VALUE_BALANCED_ECHO 8

#define VALUE_MASTER 9

#define VALUE_OUTPUT 10

#define NUM_VALUES 11

extern int8_t defaultAudioOutputInputChannel;

AudioInputSelector audioInputSelector;

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

AudioInputSelector::AudioInputSelector() {
	basicOptions = options;
	basicNumOptions = NUM_VALUES;

#if HAVE_OLED
	title = "Audio source";
#endif
}

bool AudioInputSelector::setupAndCheckAvailability() {

	switch (audioOutput->inputChannel) {
	case AUDIO_INPUT_CHANNEL_LEFT:
		currentOption = VALUE_LEFT;
		break;

	case AUDIO_INPUT_CHANNEL_RIGHT:
		currentOption = VALUE_RIGHT;
		break;

	case AUDIO_INPUT_CHANNEL_STEREO:
		currentOption = VALUE_STEREO;
		break;

	case AUDIO_INPUT_CHANNEL_BALANCED:
		currentOption = VALUE_BALANCED;
		break;

	case AUDIO_INPUT_CHANNEL_MIX:
		currentOption = VALUE_MASTER;
		break;

	case AUDIO_INPUT_CHANNEL_OUTPUT:
		currentOption = VALUE_OUTPUT;
		break;

	default:
		currentOption = VALUE_OFF;
	}

	if (audioOutput->echoing) currentOption++;
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
	if (currentUIMode) return;

	ContextMenu::selectEncoderAction(offset);

	audioOutput->echoing = false;

	switch (currentOption) {
	case VALUE_LEFT_ECHO:
		audioOutput->echoing = true;
	case VALUE_LEFT:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_LEFT;
		break;

	case VALUE_RIGHT_ECHO:
		audioOutput->echoing = true;
	case VALUE_RIGHT:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_RIGHT;
		break;

	case VALUE_STEREO_ECHO:
		audioOutput->echoing = true;
	case VALUE_STEREO:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_STEREO;
		break;

	case VALUE_BALANCED_ECHO:
		audioOutput->echoing = true;
	case VALUE_BALANCED:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_BALANCED;
		break;

	case VALUE_MASTER:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_MIX;
		break;

	case VALUE_OUTPUT:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_OUTPUT;
		break;

	default:
		audioOutput->inputChannel = AUDIO_INPUT_CHANNEL_NONE;
	}

	defaultAudioOutputInputChannel = audioOutput->inputChannel;
}
