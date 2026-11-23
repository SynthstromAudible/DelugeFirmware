/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include <cstddef>
#include <cstdint>

extern "C" {
#include "fatfs/ff.h"
}

/// Finds sample files quickly when many are loaded from one folder, as for a multisample preset with a hundred zones.
///
/// FatFs finds a file by searching its folder from the start, a sector at a time, and for such a preset that search is
/// most of the loading time. This reads the folder through once instead, and remembers where each file starts and how
/// big it is.
///
/// It's only ever a shortcut: when find() returns false, the caller opens the file the normal way. It's only used to
/// find sample files, never for writing. And it forgets everything whenever anything is written to the card or the card
/// is removed or inserted (sdCardChangeCount, in diskio.c), so it can't point to where a file used to be.
class SampleFolderIndex {
public:
	/// Gets where the file at filePath starts and how big it is, if its folder has been indexed. The folder gets
	/// indexed when a second file in a row is looked up in it - reading a whole folder isn't worth it for one file.
	bool find(char const* filePath, FilePointer* filePointer);

	/// Forgets every folder, and frees the memory
	void clear();

	static constexpr int32_t kMaxFolders = 4;
	static constexpr size_t kMaxPathLength = 256;
	static constexpr int32_t kMaxFiles = 512;       ///< A bigger folder is only indexed in part
	static constexpr size_t kNamesSize = 24 * 1024; ///< For long and short file names

private:
	struct Entry {
		FilePointer filePointer;
		uint16_t longName; ///< Offsets into the names
		uint16_t shortName;
	};
	struct Folder {
		uint8_t* memory = nullptr; ///< kMaxFiles Entries, then the names. Null if this Folder is unused.
		int32_t numEntries = 0;
		uint32_t cardChangeCount = 0;
		uint32_t lastUsed = 0;
		char path[kMaxPathLength] = {};

		Entry* entries() { return reinterpret_cast<Entry*>(memory); }
		char* names() { return reinterpret_cast<char*>(memory + kMaxFiles * sizeof(Entry)); }
	};

	Folder* findFolder(char const* path, size_t pathLength);
	Folder* indexFolder(char const* path, size_t pathLength);
	void forget(Folder& folder);

	Folder folders_[kMaxFolders];
	char lastFolderLookedUp_[kMaxPathLength] = {};
	uint32_t useCounter_ = 0;
};

extern SampleFolderIndex sampleFolderIndex;
