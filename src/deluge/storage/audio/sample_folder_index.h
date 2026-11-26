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
/// Nothing ever needs freeing by hand. An indexed folder's files are listed in a chain of blocks of stealable memory,
/// and the memory allocator takes any of them back whenever it needs the space - like it does with sample data. Losing
/// one block loses that folder's index. That's safe at any time between calls, because nothing outside this class ever
/// points into a block: find() returns a copy. Each block is exactly as big as a Cluster of sample data, so taking one
/// back makes room for exactly one Cluster - a smaller block wouldn't, and taking it would mean taking its neighbours
/// too. How big a Cluster is depends on how the card was formatted, so a folder takes as many blocks as it needs.
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
	/// A folder is read up to this many directory entries (32 bytes each, a long name taking several), however few
	/// files they hold, and only indexed that far
	static constexpr uint32_t kMaxEntriesToRead = 4096;
	/// A folder takes as many blocks as it needs, up to the fewest that make at least this much memory, and is indexed
	/// as far as that goes. That's over a thousand files with typical sample names, whatever the size of a Cluster.
	static constexpr size_t kMaxMemoryPerFolder = 64 * 1024;

	/// Reading a whole folder costs about as much as finding two of its files the normal way, so an index has paid for
	/// itself once it's found two files. Until indexes start getting thrown away before that - because the card keeps
	/// getting written to, or memory is short - a folder is indexed on the second lookup in a row. Each index thrown
	/// away too early doubles the lookups in a row needed for the next, up to the maximum, and one that pays for
	/// itself brings it back down. That way indexing never costs much more than it saves.
	static constexpr uint32_t kHitsToPayOff = 2;
	static constexpr uint32_t kMinLookupsBeforeIndexing = 2;
	static constexpr uint32_t kMaxLookupsBeforeIndexing = 64;

	/// The room left in a block. Its files' Entries fill it from the front and their names from the back, so it holds
	/// as many files as their names leave room for.
	class Space {
	public:
		/// For a block of blockSize bytes whose first start bytes are taken
		constexpr Space(size_t start, size_t blockSize)
		    : entriesEnd_{start}, namesStart_{blockSize < start ? start : blockSize}, blockSize_{namesStart_} {}

		/// Takes room for one more Entry and for namesSize bytes of names, if there's that much left. Returns where the
		/// names go, as a distance back from the end of the block, which always fits in 16 bits.
		constexpr std::optional<uint16_t> take(size_t entrySize, size_t namesSize) {
			if (namesStart_ - entriesEnd_ < entrySize + namesSize
			    || blockSize_ - namesStart_ + namesSize > UINT16_MAX) {
				return std::nullopt;
			}
			entriesEnd_ += entrySize;
			namesStart_ -= namesSize;
			return blockSize_ - namesStart_;
		}

	private:
		size_t entriesEnd_;
		size_t namesStart_;
		size_t blockSize_;
	};

private:
	struct Entry {
		FilePointer filePointer;
		uint16_t longName; ///< Distances back from the end of the Entry's block to the names
		uint16_t shortName;
	};
	struct Folder;
	class Block;

	Folder* findFolder(char const* path, size_t pathLength);
	Folder* indexFolder(char const* path);
	void forget(size_t slot);
	void blockStolen(Block* block);
	void folderLost(Folder const& folder);
	void indexingDidNotPay();

	std::array<Folder*, kMaxFolders> folders_{};
	char lastFolderLookedUp_[kMaxPathLength] = {};
	uint32_t lookupsInARow_ = 0; ///< Of files in lastFolderLookedUp_
	uint32_t lookupsBeforeIndexing_ = kMinLookupsBeforeIndexing;
	uint32_t useCounter_ = 0;
};

extern SampleFolderIndex sampleFolderIndex;
