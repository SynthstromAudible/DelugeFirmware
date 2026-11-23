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
#include "memory/stealable.h"
#include <cstring>
#include <new>
#include <strings.h>

extern "C" {
// Bumped by every write to the card, and whenever the card is removed, inserted or initialised
extern volatile uint32_t sdCardChangeCount;
}

SampleFolderIndex sampleFolderIndex{};

/// One indexed folder, in a single block of stealable memory: this object, then its Entries, then their names
class SampleFolderIndex::Folder final : public Stealable {
public:
	/// Not while it's being filled in. Only once it's complete does indexFolder() make it stealable, so any way out of
	/// indexFolder() that forgets to leaves it merely unstealable, never stolen half-built.
	bool mayBeStolen(void* thingNotToStealFrom) override { return complete; }
	void steal(char const* errorCode) override { sampleFolderIndex.folderStolen(this); } // The allocator destructs us
	StealableQueue getAppropriateQueue() override { return StealableQueue::NO_SONG_SAMPLE_DATA; } // Taken first

	Entry* entries() { return reinterpret_cast<Entry*>(this + 1); }
	char* names() { return reinterpret_cast<char*>(this) + namesOffset; }

	char path[kMaxPathLength] = {};
	uint32_t cardChangeCount = 0;
	uint32_t lastUsed = 0;
	int32_t numEntries = 0;
	uint32_t namesOffset = sizeof(Folder) + kMaxFiles * sizeof(Entry); // Moved down once the size is known
	bool complete = false;
};

namespace {
bool samePath(char const* a, char const* b, size_t bLength) {
	return strlen(a) == bLength && !strncasecmp(a, b, bLength);
}
} // namespace

std::optional<FilePointer> SampleFolderIndex::find(char const* filePath) {
	char const* slash = strrchr(filePath, '/');
	if (!slash || slash == filePath || !slash[1]) {
		return std::nullopt;
	}
	size_t pathLength = slash - filePath;
	if (pathLength >= kMaxPathLength) {
		return std::nullopt;
	}

	Folder* folder = findFolder(filePath, pathLength);
	if (!folder) {
		if (!samePath(lastFolderLookedUp_, filePath, pathLength)) {
			memcpy(lastFolderLookedUp_, filePath, pathLength);
			lastFolderLookedUp_[pathLength] = 0;
			return std::nullopt;
		}
		folder = indexFolder(filePath, pathLength);
		if (!folder) {
			return std::nullopt;
		}
	}

	// Nothing below allocates memory, so the folder can't be stolen while we look through it
	folder->lastUsed = ++useCounter_;
	// Compared the way FAT compares names, ignoring case, by either the long or the short (8.3) name. Names that only
	// differ in the case of non-ASCII letters won't match here, so they're just opened the normal way.
	char const* name = slash + 1;
	for (int32_t e = 0; e < folder->numEntries; e++) {
		Entry const& entry = folder->entries()[e];
		if (!strcasecmp(name, folder->names() + entry.longName)
		    || !strcasecmp(name, folder->names() + entry.shortName)) {
			return entry.filePointer;
		}
	}
	return std::nullopt;
}

SampleFolderIndex::Folder* SampleFolderIndex::findFolder(char const* path, size_t pathLength) {
	for (size_t slot = 0; slot < kMaxFolders; slot++) {
		Folder* folder = folders_[slot];
		if (!folder) {
			continue;
		}
		if (folder->cardChangeCount != sdCardChangeCount) {
			forget(slot); // Out of date
			continue;
		}
		if (samePath(folder->path, path, pathLength)) {
			return folder;
		}
	}
	return nullptr;
}

SampleFolderIndex::Folder* SampleFolderIndex::indexFolder(char const* path, size_t pathLength) {
	// Use an empty slot, or else the least recently used folder's
	size_t slot = 0;
	for (size_t s = 0; s < kMaxFolders; s++) {
		if (!folders_[s]) {
			slot = s;
			break;
		}
		if (folders_[s]->lastUsed < folders_[slot]->lastUsed) {
			slot = s;
		}
	}
	forget(slot);

	// Room for the most we'll index. Cut down to what's used once done.
	constexpr size_t kMaxSize = sizeof(Folder) + kMaxFiles * sizeof(Entry) + kNamesSize;
	void* memory = GeneralMemoryAllocator::get().allocStealable<Folder>(kMaxSize);
	if (!memory) {
		return nullptr;
	}
	Folder* folder = new (memory) Folder();
	auto discard = [&]() -> Folder* {
		folder->~Folder();
		delugeDealloc(folder);
		return nullptr;
	};

	memcpy(folder->path, path, pathLength);
	folder->path[pathLength] = 0;
	uint32_t cardChangeCountBefore = sdCardChangeCount;

	auto directory = FatFS::Directory::open(folder->path);
	if (!directory) {
		return discard();
	}
	size_t namesUsed = 0;
	auto addName = [&](char const* name) -> std::optional<uint16_t> {
		size_t size = strlen(name) + 1;
		if (namesUsed + size > kNamesSize) {
			return std::nullopt;
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
			return discard();
		}
		auto& [info, filePointer] = *item;
		if (!info.fname[0]) {
			break; // End of folder
		}
		if (info.fattrib & AM_DIR) {
			continue;
		}
		auto longName = addName(info.fname);
		auto shortName = addName(info.altname);
		if (!longName || !shortName) {
			break;
		}
		folder->entries()[folder->numEntries++] = {filePointer, *longName, *shortName};
	}

	// If the card was written to or swapped while we read, what we read may not match it any more
	if (sdCardChangeCount != cardChangeCountBefore) {
		return discard();
	}

	// Move the names down to straight after the Entries, and give back the rest of the memory
	uint32_t compactNamesOffset = sizeof(Folder) + folder->numEntries * sizeof(Entry);
	memmove(reinterpret_cast<char*>(folder) + compactNamesOffset, folder->names(), namesUsed);
	folder->namesOffset = compactNamesOffset;
	GeneralMemoryAllocator::get().shortenRight(folder, compactNamesOffset + namesUsed);

	folder->cardChangeCount = cardChangeCountBefore;
	folder->complete = true;
	GeneralMemoryAllocator::get().putStealableInAppropriateQueue(folder);
	folders_[slot] = folder;
	return folder;
}

void SampleFolderIndex::forget(size_t slot) {
	Folder* folder = folders_[slot];
	if (folder) {
		folders_[slot] = nullptr;
		folder->~Folder(); // Also takes it out of the allocator's queue
		delugeDealloc(folder);
	}
}

void SampleFolderIndex::folderStolen(Folder* folder) {
	for (Folder*& f : folders_) {
		if (f == folder) {
			f = nullptr;
		}
	}
}

void SampleFolderIndex::clear() {
	for (size_t slot = 0; slot < kMaxFolders; slot++) {
		forget(slot);
	}
	lastFolderLookedUp_[0] = 0;
}
