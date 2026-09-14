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
#include "storage/audio/audio_load_context.h"
#include "util/c_string.h"

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
	/// @brief Acquisition: find the file at `filePath` (already set by the caller), in memory or on the card,
	///        without binding it.
	///
	/// @param mayActuallyReadFile         false confines the lookup to files already resident in memory.
	/// @param error                       Set to Error::NONE, then to the lookup's failure if it fails.
	/// @param makeWaveTableWorkAtAllCosts Interpret the file as a wavetable even without its usual markers.
	/// @param mayLoadAsync                As for loadFile().
	/// @param context                     As for loadFile().
	/// @return The file, not yet bound; or nullptr: this holder already has a file or names none, the file is not
	///         in memory and the card may not be read, or the lookup failed (see @p error).
	AudioFile* acquire(bool mayActuallyReadFile, Error* error, bool makeWaveTableWorkAtAllCosts = false,
	                   bool mayLoadAsync = false, deluge::storage::AudioLoadContext* context = nullptr);
	/// @brief Load the file at `filePath` (already set by the caller): acquire() it, then bind it with
	///        setAudioFile() if one was found.
	///
	/// @param mayActuallyReadFile false confines the lookup to files already resident in memory -- see
	///                            getAudioFileFromFilename's `mayReadCard`.
	/// @param mayLoadAsync        Opt in to AudioFileManager::getAudioFileFromFilename's async loader for
	///                            a card miss, instead of the blocking load. Forwarded straight through,
	///                            same contract: only honoured for a Sample, and only where the async
	///                            backing is actually active. A caller opting in MUST handle
	///                            Error::LOAD_IN_PROGRESS -- returning to its own loop and asking again
	///                            on a later tick, without re-requesting or reporting a failure. Defaulted
	///                            off: every caller but AudioEngine::previewSample keeps the synchronous
	///                            behaviour it was written against.
	/// @param context             The load this file belongs to, as AudioFileManager::getAudioFileFromFilename
	///                            takes it. Null for a file loaded on its own.
	/// @return Error::NONE does not necessarily mean a file is now loaded -- see the two silent-miss
	///         cases in the .cpp. Otherwise the failure, or Error::LOAD_IN_PROGRESS (not a failure --
	///         see mayLoadAsync).
	Error loadFile(bool reversed, bool manuallySelected, bool mayActuallyReadFile,
	               int32_t clusterLoadInstruction = CLUSTER_ENQUEUE, bool makeWaveTableWorkAtAllCosts = false,
	               bool mayLoadAsync = false, deluge::storage::AudioLoadContext* context = nullptr);
	virtual void unassignAllClusterReasons(bool beingDestructed = false) {}

	std::string filePath{};
	AudioFile* audioFile{};
	AudioFileType audioFileType;
};
