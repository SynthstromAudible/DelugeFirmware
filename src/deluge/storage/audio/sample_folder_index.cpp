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
#include "storage/cluster/cluster.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <strings.h>

extern "C" {
// Bumped by every write to the card, and whenever the card is removed, inserted or initialised
extern volatile uint32_t sdCardChangeCount;
}

SampleFolderIndex sampleFolderIndex{};

/// One indexed folder. Its files are listed in its chain of Blocks, in the order they're in the folder.
struct SampleFolderIndex::Folder {
	/// Frees it and all its Blocks
	void destroy();

	char path[kMaxPathLength] = {};
	Block* blocks = nullptr;
	uint32_t cardChangeCount = 0;
	uint32_t lastUsed = 0;
	uint32_t hits = 0;     ///< Files found in it
	bool complete = false; ///< Until it is, its Blocks can't be stolen
	bool lost = false;     ///< One of its Blocks was stolen, so it can't be used any more
};

/// Part of a Folder's list of files, in exactly as much memory as a Cluster: this object, then its Entries, then free
/// space, then the Entries' names, which are filled in from the end of the block back
class SampleFolderIndex::Block final : public Stealable {
public:
	Block(Folder& folder, uint32_t size) : folder{&folder}, size{size} {}

	/// Not while its folder's being read. Only once that's complete does indexFolder() make its Blocks stealable, so
	/// any way out of indexFolder() that forgets to leaves them merely unstealable, never stolen half-built.
	bool mayBeStolen(void* thingNotToStealFrom) override { return folder->complete; }
	void steal(char const* errorCode) override { sampleFolderIndex.blockStolen(this); } // The allocator destructs us
	/// With the sample data of the song being loaded, not ahead of it with unused data. A Block is just what a Cluster
	/// being loaded needs, so it would otherwise be taken first - often by the very song whose samples it's finding,
	/// which then has to read the folder again, or search it for each file.
	StealableQueue getAppropriateQueue() override { return StealableQueue::CURRENT_SONG_SAMPLE_DATA; }

	Entry* entries() { return reinterpret_cast<Entry*>(this + 1); }
	char* name(uint16_t fromEnd) { return reinterpret_cast<char*>(this) + size - fromEnd; }

	Folder* folder;
	Block* next = nullptr;
	uint32_t size; ///< Not Cluster::size, which changes when a card with other formatting goes in
	int32_t numEntries = 0;
};

void SampleFolderIndex::Folder::destroy() {
	while (Block* block = blocks) {
		blocks = block->next;
		block->~Block(); // Also takes it out of the allocator's queue
		delugeDealloc(block);
	}
	this->~Folder();
	delugeDealloc(this);
}

namespace {
bool samePath(char const* a, char const* b, size_t bLength) {
	return strlen(a) == bLength && !strncasecmp(a, b, bLength);
}
} // namespace

std::optional<FilePointer> SampleFolderIndex::find(char const* filePath) {
	char const* slash = strrchr(filePath, '/');
	if (!slash || slash == filePath || !canLookUp(slash + 1)) {
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
			lookupsInARow_ = 0;
		}
		if (++lookupsInARow_ < lookupsBeforeIndexing_) {
			return std::nullopt;
		}
		lookupsInARow_ = 0; // Whether this works or not, the next try takes as many lookups in a row again
		folder = indexFolder(lastFolderLookedUp_);
		if (!folder) {
			indexingDidNotPay();
			return std::nullopt;
		}
	}

	// Nothing below allocates memory, so no Block can be stolen while we look through them
	folder->lastUsed = ++useCounter_;
	// FatFs takes the first file in the folder whose long name matches, ignoring case, or whose short (8.3) name does.
	// For a name canLookUp() allows, that's just what this does - for folders written by ordinary systems, anyway,
	// which keep short names upper case. Where only the start of a folder is indexed, a file found there is the first
	// match in the whole folder too.
	char const* name = slash + 1;
	for (Block* block = folder->blocks; block; block = block->next) {
		for (int32_t e = 0; e < block->numEntries; e++) {
			Entry const& entry = block->entries()[e];
			if (!strcasecmp(name, block->name(entry.longName)) || !strcasecmp(name, block->name(entry.shortName))) {
				if (++folder->hits == kHitsToPayOff) {
					lookupsBeforeIndexing_ = kMinLookupsBeforeIndexing;
				}
				return entry.filePointer;
			}
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
		if (folder->lost) {
			forget(slot); // blockStolen() has already counted it
			continue;
		}
		if (folder->cardChangeCount != sdCardChangeCount) {
			folderLost(*folder);
			forget(slot); // Out of date
			continue;
		}
		if (samePath(folder->path, path, pathLength)) {
			return folder;
		}
	}
	return nullptr;
}

SampleFolderIndex::Folder* SampleFolderIndex::indexFolder(char const* path) {
	uint32_t cardChangeCountBefore = sdCardChangeCount;

	// Open the folder before taking any memory for it, so a folder that isn't there costs none
	auto directory = FatFS::Directory::open(path);
	if (!directory) {
		return nullptr;
	}

	void* memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(Folder));
	if (!memory) {
		return nullptr;
	}
	Folder* folder = new (memory) Folder();
	auto discard = [&]() -> Folder* {
		folder->destroy();
		return nullptr;
	};
	memcpy(folder->path, path, strlen(path) + 1); // find() has checked it fits

	// Each Block exactly as big as a Cluster, all of it kept, never shortened: see the class comment
	uint32_t blockSize = Cluster::allocationSize();
	size_t blocksLeft = (kMaxMemoryPerFolder + blockSize - 1) / blockSize;
	Block** link = &folder->blocks;
	Block* block = nullptr;
	Space space{0, 0};

	// A folder with more files than fit is indexed in part, and so is one that takes too much reading, counting entries
	// that hold no samples, like subfolders' and deleted files'. FatFs skips a run of deleted files' entries in one go
	// though, so the last read can go past the limit by one such run.
	constexpr uint32_t kDirectoryEntrySize = 32;
	while (directory->inner().dptr < kMaxEntriesToRead * kDirectoryEntrySize) {
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
		size_t longNameSize = strlen(info.fname) + 1;
		size_t shortNameSize = strlen(info.altname) + 1;
		std::optional<uint16_t> longName;
		if (block) {
			longName = space.take(sizeof(Entry), longNameSize + shortNameSize);
		}
		if (!longName) {
			if (!blocksLeft) {
				break;
			}
			void* blockMemory = GeneralMemoryAllocator::get().allocStealable<Block>(blockSize);
			if (!blockMemory) {
				break;
			}
			block = new (blockMemory) Block(*folder, blockSize);
			*link = block;
			link = &block->next;
			blocksLeft--;
			space = Space{sizeof(Block), blockSize};
			longName = space.take(sizeof(Entry), longNameSize + shortNameSize);
			if (!longName) {
				break; // Can't happen: FatFs's longest names fit in the smallest Cluster
			}
		}
		auto shortName = static_cast<uint16_t>(*longName - longNameSize);
		memcpy(block->name(*longName), info.fname, longNameSize);
		memcpy(block->name(shortName), info.altname, shortNameSize);
		block->entries()[block->numEntries++] = {filePointer, *longName, shortName};
	}

	// If the card was written to or swapped while we read, what we read may not match it any more. And a folder with
	// nothing in it, or that we got no memory for, has nothing worth remembering.
	if (sdCardChangeCount != cardChangeCountBefore || !folder->blocks) {
		return discard();
	}

	folder->cardChangeCount = cardChangeCountBefore;
	folder->complete = true;
	for (Block* b = folder->blocks; b; b = b->next) {
		GeneralMemoryAllocator::get().putStealableInAppropriateQueue(b);
	}

	// Only now make room for it, in an empty slot or else the least recently used folder's. Not before: getting the
	// memory may have stolen from one of the other folders, and a folder that couldn't be indexed shouldn't push one
	// out.
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
	folders_[slot] = folder;
	return folder;
}

void SampleFolderIndex::forget(size_t slot) {
	Folder* folder = folders_[slot];
	if (folder) {
		folders_[slot] = nullptr;
		folder->destroy();
	}
}

/// The allocator is taking a Block back. Its folder's other Blocks are freed the next time anything is looked up -
/// not now, from inside the allocator.
void SampleFolderIndex::blockStolen(Block* block) {
	Folder& folder = *block->folder;
	for (Block** link = &folder.blocks; *link; link = &(*link)->next) {
		if (*link == block) {
			*link = block->next;
			break;
		}
	}
	if (!folder.lost) {
		folder.lost = true;
		folderLost(folder);
	}
}

/// A folder's index is being thrown away, before it would have been otherwise
void SampleFolderIndex::folderLost(Folder const& folder) {
	if (folder.hits < kHitsToPayOff) {
		indexingDidNotPay();
	}
}

void SampleFolderIndex::indexingDidNotPay() {
	lookupsBeforeIndexing_ = std::min(lookupsBeforeIndexing_ * 2, kMaxLookupsBeforeIndexing);
}

void SampleFolderIndex::clear() {
	for (size_t slot = 0; slot < kMaxFolders; slot++) {
		forget(slot);
	}
	lastFolderLookedUp_[0] = 0;
	lookupsInARow_ = 0;
	lookupsBeforeIndexing_ = kMinLookupsBeforeIndexing;
}
