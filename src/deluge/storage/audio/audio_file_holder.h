/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include <utility>

#include "definitions_cxx.hpp"
#include "util/c_string.h"

extern "C" {
#include "fatfs/ff.h"
}

class Sample;
class AudioFile;

class AudioFileHolder {
public:
	AudioFileHolder();
	virtual ~AudioFileHolder();

	// Moves transfer the audioFile pointer (and any reasons subclasses hold on it), so the moved-from holder's
	// destructor releases nothing. Copying must stay explicit, as reason counting makes it non-trivial.
	AudioFileHolder(AudioFileHolder&& other) noexcept
	    : filePath(other.filePath), audioFile(std::exchange(other.audioFile, nullptr)),
	      audioFileType(other.audioFileType) {}
	AudioFileHolder& operator=(AudioFileHolder&& other) noexcept {
		filePath = other.filePath;
		audioFile = std::exchange(other.audioFile, nullptr);
		audioFileType = other.audioFileType;
		return *this;
	}
	AudioFileHolder(AudioFileHolder const&) = delete;
	AudioFileHolder& operator=(AudioFileHolder const&) = delete;
	virtual void setAudioFile(AudioFile* newSample, bool reversed = false, bool manuallySelected = false,
	                          int32_t clusterLoadInstruction = CLUSTER_ENQUEUE);
	Error loadFile(bool reversed, bool manuallySelected, bool mayActuallyReadFile,
	               int32_t clusterLoadInstruction = CLUSTER_ENQUEUE, FilePointer* filePointer = nullptr,
	               bool makeWaveTableWorkAtAllCosts = false);
	virtual void unassignAllClusterReasons(bool beingDestructed = false) {}

	std::string filePath;
	AudioFile* audioFile;
	AudioFileType audioFileType;
};
