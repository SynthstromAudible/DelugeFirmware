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
#include "model/song/song.h"
#include "processing/audio_output.h"

extern AudioInputChannel defaultAudioOutputInputChannel;

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

	TRACK,
};
constexpr size_t kNumValues = 12;

AudioInputSelector audioInputSelector{};

char const* AudioInputSelector::getTitle() {
	using enum l10n::String;
	return l10n::get(STRING_FOR_AUDIO_SOURCE);
}

Sized<const char**> AudioInputSelector::getOptions() {
	using enum l10n::String;
	static const char* options[] = {l10n::get(STRING_FOR_DISABLED),
	                                l10n::get(STRING_FOR_LEFT_INPUT),
	                                l10n::get(STRING_FOR_LEFT_INPUT_MONITORING),
	                                l10n::get(STRING_FOR_RIGHT_INPUT),
	                                l10n::get(STRING_FOR_RIGHT_INPUT_MONITORING),
	                                l10n::get(STRING_FOR_STEREO_INPUT),
	                                l10n::get(STRING_FOR_STEREO_INPUT_MONITORING),
	                                l10n::get(STRING_FOR_BALANCED_INPUT),
	                                l10n::get(STRING_FOR_BALANCED_INPUT_MONITORING),
	                                l10n::get(STRING_FOR_MIX_PRE_FX),
	                                l10n::get(STRING_FOR_MIX_POST_FX),
	                                l10n::get(STRING_FOR_TRACK)};
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

	default:
		valueOption = Value::OFF;
	}

	currentOption = static_cast<int32_t>(valueOption);

	if (audioOutput->echoing) {
		currentOption += 1;
	}
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

	audioOutput->echoing = false;

	auto valueOption = static_cast<Value>(currentOption);

	switch (valueOption) {
	case Value::LEFT_ECHO:
		audioOutput->echoing = true;
	case Value::LEFT:
		audioOutput->inputChannel = AudioInputChannel::LEFT;
		break;

	case Value::RIGHT_ECHO:
		audioOutput->echoing = true;
	case Value::RIGHT:
		audioOutput->inputChannel = AudioInputChannel::RIGHT;
		break;

	case Value::STEREO_ECHO:
		audioOutput->echoing = true;
	case Value::STEREO:
		audioOutput->inputChannel = AudioInputChannel::STEREO;
		break;

	case Value::BALANCED_ECHO:
		audioOutput->echoing = true;
	case Value::BALANCED:
		audioOutput->inputChannel = AudioInputChannel::BALANCED;
		break;

	case Value::MASTER:
		audioOutput->inputChannel = AudioInputChannel::MIX;
		break;

	case Value::OUTPUT:
		audioOutput->inputChannel = AudioInputChannel::OUTPUT;
		break;
	case Value::TRACK:
		audioOutput->inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
		audioOutput->outputRecordingFrom = currentSong->getOutputFromIndex(0);
		break;

	default:
		audioOutput->inputChannel = AudioInputChannel::NONE;
	}

	defaultAudioOutputInputChannel = audioOutput->inputChannel;
}

} // namespace deluge::gui::context_menu
