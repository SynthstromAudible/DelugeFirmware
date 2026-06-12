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

#pragma once

#include "definitions_cxx.hpp"
#include "util/containers.h"

class AudioFile;

/// Pointers to all loaded AudioFiles, sorted case-insensitively by filePath. A Sample and a WaveTable may share
/// the same path, so equal keys can appear as neighbours. Lives on the external (non-stealable) region so that
/// growing it can never steal an AudioFile, which would erase from this same vector mid-insertion.
class AudioFileVector final : public deluge::vector<AudioFile*> {
public:
	/// Mirrors NamedThingVector::search(..., GREATER_OR_EQUAL): returns the index of a name match, or the
	/// insertion point if there is none.
	int32_t search(char const* searchString, bool* foundExact = nullptr) const;

	/// Returns -1 if not found. All times this is called, it actually should get found - but some bugs remain, and
	/// the caller must deal with these.
	int32_t searchForExactObject(AudioFile* audioFile) const;

	/// Inserts at the sorted position for the file's path.
	Error insertElement(AudioFile* audioFile);
};
