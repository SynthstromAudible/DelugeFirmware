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
#include "storage/audio/audio_file_manager.h"

#include "hid/display/display.h"
#include "storage/audio/audio_file.h"

AudioFileHolder::AudioFileHolder() {
	audioFile = NULL;
}

AudioFileHolder::~AudioFileHolder() {
}

// Loads file from filePath, which would already be set.
// Returns error, but NO_ERROR doesn't necessarily mean there's now a file loaded - it might be that filePath was NULL, but that's not a problem. Or that the SD card would need to be accessed but we didn't have permission for that (!mayActuallyReadFile).
int32_t AudioFileHolder::loadFile(bool reversed, bool manuallySelected, bool mayActuallyReadFile,
                                  int32_t clusterLoadInstruction, FilePointer* filePointer,
                                  bool makeWaveTableWorkAtAllCosts) {

	// See if this AudioFile object already all loaded up
	if (audioFile) {
		return NO_ERROR;
	}

	if (filePath.isEmpty()) {
		return NO_ERROR; // This could happen if the filename tag wasn't present in the file
	}

	uint8_t error;
	AudioFile* newAudioFile = audioFileManager.getAudioFileFromFilename(
	    &filePath, mayActuallyReadFile, &error, filePointer, audioFileType, makeWaveTableWorkAtAllCosts);

	// If we found it...
	if (newAudioFile) {

		// We only actually set it after already setting it up, processing the wavetable, etc. - so there's no risk of the audio routine trying to
		// sound it before it's all set up.
		setAudioFile(newAudioFile, reversed, manuallySelected, clusterLoadInstruction);
	}

	return error;
}

// For if we've already got a pointer to the AudioFile in memory.
void AudioFileHolder::setAudioFile(AudioFile* newAudioFile, bool reversed, bool manuallySelected,
                                   int32_t clusterLoadInstruction) {
	if (audioFile) {
		unassignAllClusterReasons();
#if ALPHA_OR_BETA_VERSION
		if (audioFile->numReasonsToBeLoaded <= 0) {
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
