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
#include <strings.h>

int32_t AudioFileVector::search(char const* searchString, bool* foundExact) const {
	int32_t rangeBegin = 0;
	int32_t rangeEnd = static_cast<int32_t>(size());

	while (rangeBegin != rangeEnd) {
		int32_t proposedIndex = rangeBegin + ((rangeEnd - rangeBegin) >> 1);

		int32_t result = strcasecmp((*this)[proposedIndex]->filePath.c_str(), searchString);

		if (result == 0) {
			if (foundExact != nullptr) {
				*foundExact = true;
			}
			return proposedIndex;
		}
		if (result < 0) {
			rangeBegin = proposedIndex + 1;
		}
		else {
			rangeEnd = proposedIndex;
		}
	}

	if (foundExact != nullptr) {
		*foundExact = false;
	}
	return rangeBegin;
}

int32_t AudioFileVector::searchForExactObject(AudioFile* audioFile) const {
	bool foundExactName;
	int32_t i = search(audioFile->filePath.c_str(), &foundExactName);
	if (!foundExactName) {
		return -1;
	}

	AudioFile* foundAudioFile = (*this)[i];

	// If object doesn't match, then we were looking for a Sample and got a Wavetable, or vice versa. So check
	// neighbours.
	if (foundAudioFile != audioFile) {
		if (i > 0) {
			i--;
			foundAudioFile = (*this)[i];
			if (foundAudioFile == audioFile) {
				goto gotIt;
			}
			i++; // Put it back.
		}
		i++; // Increment it (again). It's gotta be there... Except, some bugs?

		if (i >= static_cast<int32_t>(size())) {
			return -1;
		}
		foundAudioFile = (*this)[i];
		if (foundAudioFile != audioFile) {
			return -1;
		}
	}

gotIt:
	return i;
}

Error AudioFileVector::insertElement(AudioFile* audioFile) {
	int32_t i = search(audioFile->filePath.c_str());
	try {
		insert(begin() + i, audioFile);
	} catch (deluge::exception&) {
		return Error::INSUFFICIENT_RAM;
	}
	return Error::NONE;
}
