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

#include "definitions_cxx.hpp"
#include "memory/stealable.h"
#include "util/d_string.h"

class AudioFileReader;

class AudioFile : public Stealable {
public:
	AudioFile(AudioFileType newType) : type(newType) {}
	~AudioFile() override = default;

	Error loadFile(AudioFileReader* reader, bool isAiff, bool makeWaveTableWorkAtAllCosts);
	virtual void finalizeAfterLoad(uint32_t fileSize) {}

	void addReason();
	void removeReason(char const* errorCode);

	// Stealable implementation
	bool mayBeStolen(void* thingNotToStealFrom = NULL);
	void steal(char const* errorCode);
	StealableQueue getAppropriateQueue();

	String filePath;

	const AudioFileType type;
	uint8_t numChannels{};
	String loadedFromAlternatePath; // We now need to store this, since "alternate" files can now just have the same
	                                // filename (in special folder) as the original. So we need to remember which format
	                                // the name took.
	int32_t numReasonsToBeLoaded{}; // This functionality should probably be merged between AudioFile and Cluster.

protected:
	virtual void numReasonsIncreasedFromZero() {}
	virtual void numReasonsDecreasedToZero(char const* errorCode) {}
};

constexpr size_t afhs = sizeof(AudioFile);
