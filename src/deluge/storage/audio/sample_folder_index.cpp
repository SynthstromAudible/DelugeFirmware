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

#include "storage/audio/sample_folder_index.h"
#include "fatfs/fatfs.hpp"
#include "memory/general_memory_allocator.h"
#include <cstring>
#include <strings.h>

extern "C" {
// Bumped by every write to the card, and whenever the card is removed, inserted or initialised
extern volatile uint32_t sdCardChangeCount;
}

SampleFolderIndex sampleFolderIndex{};

namespace {
bool samePath(char const* a, char const* b, size_t bLength) {
	return strlen(a) == bLength && !strncasecmp(a, b, bLength);
}
} // namespace

bool SampleFolderIndex::find(char const* filePath, FilePointer* filePointer) {
	char const* slash = strrchr(filePath, '/');
	if (!slash || slash == filePath || !slash[1]) {
		return false;
	}
	size_t pathLength = slash - filePath;
	if (pathLength >= kMaxPathLength) {
		return false;
	}

	Folder* folder = findFolder(filePath, pathLength);
	if (!folder) {
		if (!samePath(lastFolderLookedUp_, filePath, pathLength)) {
			memcpy(lastFolderLookedUp_, filePath, pathLength);
			lastFolderLookedUp_[pathLength] = 0;
			return false;
		}
		folder = indexFolder(filePath, pathLength);
		if (!folder) {
			return false;
		}
	}

	folder->lastUsed = ++useCounter_;
	// Compared the way FAT compares names, ignoring case, by either the long or the short (8.3) name. Names that only
	// differ in the case of non-ASCII letters won't match here, so they're just opened the normal way.
	char const* name = slash + 1;
	for (int32_t e = 0; e < folder->numEntries; e++) {
		Entry const& entry = folder->entries()[e];
		if (!strcasecmp(name, folder->names() + entry.longName)
		    || !strcasecmp(name, folder->names() + entry.shortName)) {
			*filePointer = entry.filePointer;
			return true;
		}
	}
	return false;
}

SampleFolderIndex::Folder* SampleFolderIndex::findFolder(char const* path, size_t pathLength) {
	for (Folder& folder : folders_) {
		if (!folder.memory) {
			continue;
		}
		if (folder.cardChangeCount != sdCardChangeCount) {
			forget(folder); // Out of date
			continue;
		}
		if (samePath(folder.path, path, pathLength)) {
			return &folder;
		}
	}
	return nullptr;
}

SampleFolderIndex::Folder* SampleFolderIndex::indexFolder(char const* path, size_t pathLength) {
	// Use an unused Folder, or else the least recently used one
	Folder* folder = &folders_[0];
	for (Folder& f : folders_) {
		if (!f.memory) {
			folder = &f;
			break;
		}
		if (f.lastUsed < folder->lastUsed) {
			folder = &f;
		}
	}
	forget(*folder);

	folder->memory =
	    static_cast<uint8_t*>(GeneralMemoryAllocator::get().allocLowSpeed(kMaxFiles * sizeof(Entry) + kNamesSize));
	if (!folder->memory) {
		return nullptr;
	}
	memcpy(folder->path, path, pathLength);
	folder->path[pathLength] = 0;
	uint32_t cardChangeCountBefore = sdCardChangeCount;

	auto directory = FatFS::Directory::open(folder->path);
	if (!directory) {
		forget(*folder);
		return nullptr;
	}
	size_t namesUsed = 0;
	auto addName = [&](char const* name) -> uint16_t {
		size_t size = strlen(name) + 1;
		if (namesUsed + size > kNamesSize) {
			return UINT16_MAX;
		}
		memcpy(folder->names() + namesUsed, name, size);
		uint16_t offset = namesUsed;
		namesUsed += size;
		return offset;
	};

	// A folder too big to fit is indexed in part: files past that just get opened the normal way
	while (folder->numEntries < kMaxFiles) {
		auto item = directory->read_and_get_filepointer();
		if (!item) {
			forget(*folder);
			return nullptr;
		}
		auto& [info, filePointer] = *item;
		if (!info.fname[0]) {
			break; // End of folder
		}
		if (info.fattrib & AM_DIR) {
			continue;
		}
		uint16_t longName = addName(info.fname);
		uint16_t shortName = addName(info.altname);
		if (longName == UINT16_MAX || shortName == UINT16_MAX) {
			break;
		}
		folder->entries()[folder->numEntries++] = {filePointer, longName, shortName};
	}

	// If the card was written to or swapped while we read, what we read may not match it any more
	if (sdCardChangeCount != cardChangeCountBefore) {
		forget(*folder);
		return nullptr;
	}
	folder->cardChangeCount = cardChangeCountBefore;
	return folder;
}

void SampleFolderIndex::forget(Folder& folder) {
	if (folder.memory) {
		delugeDealloc(folder.memory);
	}
	folder = Folder{};
}

void SampleFolderIndex::clear() {
	for (Folder& folder : folders_) {
		forget(folder);
	}
	lastFolderLookedUp_[0] = 0;
}
