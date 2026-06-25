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
#include "storage/audio/audio_file_format.h" // RawDataFormat + AudioFileFormat (the format-decode boundary)
#include "util/c_string.h"
#include <string>

class AudioByteSource;
class WaveTableReader;

// An AudioFile (Sample / WaveTable) is a resource-manager *adopted* object: the owner allocates +
// builds it, the manager owns only its eviction (value-scored). `addReason`/`removeReason` route to
// manager leases (add_lease/release); eviction runs `audioFileEvict` (erase from `audioFiles` +
// destruct). See AudioFileManager::adoptAudioFileObject.
class AudioFile {
public:
	AudioFile(AudioFileType newType) : type(newType) {}
	virtual ~AudioFile() = default;

	// Parse the header off `source` and apply it to this object (Sample: take the fields; WaveTable: hand
	// them to WaveTable::setup). `wtReader` is the WaveTable file reader for setup's data read — only the
	// WAVETABLE path uses it (transitional, until a DeserializerByteSource replaces it).
	Error loadFile(AudioByteSource& source, bool isAiff, bool makeWaveTableWorkAtAllCosts,
	               WaveTableReader* wtReader = nullptr);
	virtual void finalizeAfterLoad(uint32_t fileSize) {}

	void addReason();
	void removeReason(char const* errorCode);

	std::string filePath{};

	const AudioFileType type;
	uint8_t numChannels{};
	std::string loadedFromAlternatePath{}; // We now need to store this, since "alternate" files can now just have the
	                                       // same filename (in special folder) as the original. So we need to remember
	                                       // which format the name took.
	// Handle to this object's chunk slot in the resource manager (an AudioFile is an *adopted* chunk;
	// set at adoption via deluge_resource_slot_of). The object's hard-lease ("reason") count lives in
	// that slot — read O(1) via leaseCount(). 0xFFFFFFFF == DELUGE_RESOURCE_NO_SLOT (literal here so
	// this widely-included header needn't pull in deluge_resource.h).
	uint32_t resourceSlot{0xFFFFFFFFu};

	// Hard-lease ("reason") count for this object — the single source of truth lives in the manager's
	// chunk slot (read via resourceSlot). Replaces the old numReasonsToBeLoaded mirror field.
	[[nodiscard]] uint32_t leaseCount() const;

	// "Is the project/song using this right now" — true while any hard lease is held (a clip/voice/
	// preview/recorder holds the object). This is the soft-reference / relevance signal (the old
	// CURRENT_SONG vs NO_SONG distinction, which getAppropriateQueue derived the same way): a Sample
	// soft-references its resource assets while this holds, so current-song data outlives no-song data.
	[[nodiscard]] bool isProjectReferenced() const;

	// Size in bytes of the block this object was allocated in (sizeof the concrete subclass). Passed to
	// deluge_resource_adopt so the manager's cost-per-byte eviction knows how little memory freeing the
	// object reclaims — keeping the tiny descriptor resident over the fat clusters it owns.
	[[nodiscard]] virtual size_t allocatedSize() const = 0;

	constexpr static bool isSample(const AudioFile* file) { return file->type == AudioFileType::SAMPLE; }
	constexpr static bool isWaveTable(const AudioFile* file) { return file->type == AudioFileType::WAVETABLE; }

protected:
	virtual void numReasonsIncreasedFromZero() {}
	virtual void numReasonsDecreasedToZero(char const* errorCode) {}
};

constexpr size_t afhs = sizeof(AudioFile);
