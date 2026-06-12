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
#include <algorithm>
#include <strings.h>

int32_t AudioFileVector::search(char const* searchString, bool* foundExact) const {
	auto it = std::ranges::lower_bound(
	    *this, searchString, [](char const* a, char const* b) { return strcasecmp(a, b) < 0; },
	    [](AudioFile const* file) { return file->filePath.c_str(); });
	if (foundExact != nullptr) {
		*foundExact = (it != end() && strcasecmp((*it)->filePath.c_str(), searchString) == 0);
	}
	return static_cast<int32_t>(it - begin());
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
