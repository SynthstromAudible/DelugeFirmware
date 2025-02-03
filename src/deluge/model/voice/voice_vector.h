/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "util/container/array/ordered_resizeable_array_with_multi_word_key.h"

class Voice;
class Sound;

struct VoiceVectorElement {
	Sound* sound;
	Voice* voice;
};

struct VoiceVector : OrderedResizeableArrayWithMultiWordKey {
	VoiceVector() : OrderedResizeableArrayWithMultiWordKey(sizeof(VoiceVectorElement), 2, 48, 48) {
		emptyingShouldFreeMemory = false;
	}
	void getRangeForSound(Sound* sound, int32_t* __restrict__ ends);
	void checkVoiceExists(Voice* voice, Sound* sound, char const* errorCode);

	inline Voice* getVoice(int32_t index) { return ((VoiceVectorElement*)getElementAddress(index))->voice; }
};
