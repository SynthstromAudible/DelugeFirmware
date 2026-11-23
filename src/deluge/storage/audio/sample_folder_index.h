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

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

extern "C" {
#include "fatfs/ff.h"
}

/// Finds sample files quickly when many are loaded from one folder, as for a multisample preset with a hundred zones.
///
/// FatFs finds a file by searching its folder from the start, a sector at a time, and for such a preset that search is
/// most of the loading time. This reads the folder through once instead, and remembers where each file starts and how
/// big it is.
///
/// It's only ever a shortcut: when find() returns nothing, the caller opens the file the normal way. It's only used to
/// find sample files, never for writing. And it forgets everything whenever anything is written to the card or the card
/// is removed or inserted (sdCardChangeCount, in diskio.c), so it can't point to where a file used to be.
///
/// Nothing ever needs freeing by hand. Each indexed folder lives in stealable memory, sized to fit, and the memory
/// allocator takes it back whenever it needs the space - like it does with sample data. That's safe at any time
/// between calls, because nothing outside this class ever points into a folder: find() returns a copy.
class SampleFolderIndex {
public:
	/// Where the file at filePath starts and how big it is, if its folder has been indexed. The folder gets indexed
	/// when files keep being looked up in it - reading a whole folder isn't worth it for one file.
	std::optional<FilePointer> find(char const* filePath);

	/// Forgets every folder, and frees the memory
	void clear();

	/// Whether find() can look up this file name. It only does for names it can match just the way FatFs does:
	/// printable ASCII characters that FatFs allows in names, not ending in a dot or space - nearly every sample's
	/// name. FatFs matches other names in ways not copied here, like ignoring the case of accented letters or a dot at
	/// the end, so those are always left to it.
	static constexpr bool canLookUp(char const* fileName) {
		char const* c = fileName;
		for (; *c; c++) {
			unsigned char ch = *c;
			if (ch < ' ' || ch > '~') {
				return false; // Control characters, and anything not ASCII
			}
			switch (ch) {
			case '\\': // FatFs takes this as a folder separator
			case '*':
			case ':':
			case '<':
			case '>':
			case '|':
			case '"':
			case '?': // Also what f_readdir() gives for a name it can't read
				return false;
			default:
				break;
			}
		}
		return c != fileName && c[-1] != '.' && c[-1] != ' ';
	}

	static constexpr size_t kMaxFolders = 4;
	static constexpr size_t kMaxPathLength = 256;
	static constexpr int32_t kMaxFiles = 512;       ///< A bigger folder is only indexed in part
	static constexpr size_t kNamesSize = 24 * 1024; ///< Room for long and short file names while indexing
	/// A folder is read up to this many directory entries (32 bytes each, a long name taking several), however few
	/// files they hold, and only indexed that far
	static constexpr uint32_t kMaxEntriesToRead = 4096;

	/// Reading a whole folder costs about as much as finding two of its files the normal way, so an index has paid for
	/// itself once it's found two files. Until indexes start getting thrown away before that - because the card keeps
	/// getting written to, or memory is short - a folder is indexed on the second lookup in a row. Each index thrown
	/// away too early doubles the lookups in a row needed for the next, up to the maximum, and one that pays for
	/// itself brings it back down. That way indexing never costs much more than it saves.
	static constexpr uint32_t kHitsToPayOff = 2;
	static constexpr uint32_t kMinLookupsBeforeIndexing = 2;
	static constexpr uint32_t kMaxLookupsBeforeIndexing = 64;

private:
	struct Entry {
		FilePointer filePointer;
		uint16_t longName; ///< Offsets into the names
		uint16_t shortName;
	};
	class Folder;

	Folder* findFolder(char const* path, size_t pathLength);
	Folder* indexFolder(char const* path);
	void forget(size_t slot);
	void folderStolen(Folder* folder);
	void folderLost(Folder const& folder);
	void indexingDidNotPay();

	std::array<Folder*, kMaxFolders> folders_{};
	char lastFolderLookedUp_[kMaxPathLength] = {};
	uint32_t lookupsInARow_ = 0; ///< Of files in lastFolderLookedUp_
	uint32_t lookupsBeforeIndexing_ = kMinLookupsBeforeIndexing;
	uint32_t useCounter_ = 0;
};

extern SampleFolderIndex sampleFolderIndex;
