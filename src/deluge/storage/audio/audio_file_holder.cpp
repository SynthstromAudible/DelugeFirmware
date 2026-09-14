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

#include "storage/audio/audio_file_holder.h"
#include "definitions_cxx.hpp"
#include "storage/audio/audio_file.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/audio/load_trace.h"

AudioFileHolder::AudioFileHolder() {
	audioFile = nullptr;
}

AudioFileHolder::~AudioFileHolder() {
}

AudioFile* AudioFileHolder::acquire(bool mayActuallyReadFile, Error* error, bool makeWaveTableWorkAtAllCosts,
                                    deluge::storage::AsyncLoadMode asyncMode,
                                    deluge::storage::AudioLoadContext* context) {
	*error = Error::NONE;

	// See if this AudioFile object already all loaded up
	if (audioFile != nullptr) {
		return nullptr;
	}

	if (filePath.empty()) {
		return nullptr; // This could happen if the filename tag wasn't present in the file
	}

	return audioFileManager.getAudioFileFromFilename(filePath, mayActuallyReadFile, error, audioFileType,
	                                                 makeWaveTableWorkAtAllCosts, asyncMode, context);
}

// Loads file from filePath, which would already be set.
// Returns error, but Error::NONE doesn't necessarily mean there's now a file loaded - it might be that filePath was
// NULL, but that's not a problem. Or that the SD card would need to be accessed but we didn't have permission for that
// (!mayActuallyReadFile).
// asyncMode is forwarded as-is to getAudioFileFromFilename (see its own doc comment and this
// function's declaration in the header): a caller that passes anything but Blocking must be prepared to see
// Error::LOAD_IN_PROGRESS come back out of this function and treat it as "ask again later", not a
// failure.
Error AudioFileHolder::loadFile(bool reversed, bool manuallySelected, bool mayActuallyReadFile,
                                int32_t clusterLoadInstruction, bool makeWaveTableWorkAtAllCosts,
                                deluge::storage::AsyncLoadMode asyncMode, deluge::storage::AudioLoadContext* context) {
	Error error = Error::NONE;
	// We only actually set it after already setting it up, processing the wavetable, etc. - so there's no risk of
	// the audio routine trying to sound it before it's all set up.
	if (AudioFile* file = acquire(mayActuallyReadFile, &error, makeWaveTableWorkAtAllCosts, asyncMode, context)) {
		setAudioFile(file, reversed, manuallySelected, clusterLoadInstruction);
	}
	return error;
}

// For if we've already got a pointer to the AudioFile in memory.
void AudioFileHolder::setAudioFile(AudioFile* newAudioFile, bool reversed, bool manuallySelected,
                                   int32_t clusterLoadInstruction) {
	DELUGE_LOAD_TRACE("bind path=%s file=%d reversed=%d manual=%d cluster=%d", filePath.c_str(),
	                  newAudioFile != nullptr ? 1 : 0, reversed ? 1 : 0, manuallySelected ? 1 : 0,
	                  static_cast<int>(clusterLoadInstruction));
	if (audioFile) {
		unassignAllClusterReasons();
#if ALPHA_OR_BETA_VERSION
		if (!audioFile->isProjectReferenced()) {
			FREEZE_WITH_ERROR("E220"); // I put this here to try and catch an E004 Luc got
		}
#endif
		audioFile->removeReason("E391");
	}

	audioFile = newAudioFile;

	if (audioFile) {
		audioFile->addReason();
	}
}
