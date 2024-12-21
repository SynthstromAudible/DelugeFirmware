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

#include "model/voice/voice_vector.h"
#include "definitions_cxx.hpp"

void VoiceVector::checkVoiceExists(Voice* voice, Sound* sound, char const* errorCode) {

	if (ALPHA_OR_BETA_VERSION) {
		uint32_t searchWords[2];
		searchWords[0] = (uint32_t)sound;
		searchWords[1] = (uint32_t)voice;
		int32_t i = searchMultiWordExact(searchWords);

		if (i == -1) {
			FREEZE_WITH_ERROR(errorCode);
		}
	}
}

// Returns results as if GREAT_OR_EQUAL had been supplied to search. To turn this into LESS, subtract 1
void VoiceVector::getRangeForSound(Sound* sound, int32_t* __restrict__ ends) {

	int32_t searchTerms[2];
	searchTerms[0] = (uint32_t)sound;
	searchTerms[1] = (uint32_t)sound + 1;

	searchDual(searchTerms, ends);
}
