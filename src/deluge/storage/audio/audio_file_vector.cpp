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

#include "storage/audio/audio_file_vector.h"
#include "storage/audio/audio_file.h"

#pragma GCC diagnostic push
// This is supported by GCC and other compilers should error (not warn), so turn off for this file
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

AudioFileVector::AudioFileVector() : NamedThingVector(__builtin_offsetof(AudioFile, filePath)) {
}

// Returns -1 if not found. All times this is called, it actually should get found - but some bugs remain, and the
// caller must deal with these.
int32_t AudioFileVector::searchForExactObject(AudioFile* audioFile) {
	bool foundExactName;
	int32_t i = search(audioFile->filePath.get(), GREATER_OR_EQUAL, &foundExactName);
	if (!foundExactName) {
		return -1;
	}

	AudioFile* foundAudioFile = (AudioFile*)getElement(i);

	// If object doesn't match, then we were looking for a Sample and got a Wavetable, or vice versa. So check
	// neighbours.
	if (foundAudioFile != audioFile) {
		if (i > 0) {
			i--;
			foundAudioFile = (AudioFile*)getElement(i);
			if (foundAudioFile == audioFile) {
				goto gotIt;
			}
			i++; // Put it back.
		}
		i++; // Increment it (again). It's gotta be there... Except, some bugs?

		if (i >= getNumElements()) {
			return -1;
		}
		foundAudioFile = (AudioFile*)getElement(i);
		if (foundAudioFile != audioFile) {
			return -1;
		}
	}

gotIt:
	return i;
}
#pragma GCC diagnostic pop
