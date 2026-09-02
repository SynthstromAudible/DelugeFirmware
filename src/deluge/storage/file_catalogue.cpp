/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "storage/file_catalogue.h"
#include "definitions_cxx.hpp"
#include "io/debug/log.h"
#include "io/file.hpp"
#include "model/instrument/instrument.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/try.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>

using namespace deluge;

char const* allowedFileExtensionsXML[] = {"XML", "Json", NULL};

namespace deluge::storage {

std::string FileCatalogue::currentDir{};
deluge::vector<FileItem> FileCatalogue::fileItems;
int32_t FileCatalogue::numFileItemsDeletedAtStart;
int32_t FileCatalogue::numFileItemsDeletedAtEnd;
std::string FileCatalogue::firstFileItemRemaining;
std::string FileCatalogue::lastFileItemRemaining;
char const* FileCatalogue::filenameToStartSearchAt;

namespace {
/// Who currently owns the catalogue; see `FileCatalogue::claim`. Atomic because the claim is
/// released from the storage owner's context on one of the two dispatch paths (the scenes seam's
/// op) and read from the UI tick on both.
std::atomic<const void*> g_claimant{nullptr};
} // namespace

bool FileCatalogue::claim(const void* owner) {
	const void* expected = nullptr;
	// A single RMW from nullptr: there is no partial claim on failure, and no window in which two
	// callers both believe they took it.
	return g_claimant.compare_exchange_strong(expected, owner, std::memory_order_acq_rel, std::memory_order_acquire);
}

void FileCatalogue::release(const void* owner) {
	const void* expected = owner;
	// Only the owner may release, so a caller that never claimed (or already released) can call
	// this unconditionally -- which is what lets `Listing`'s destructor hand the claim back without
	// first working out whether it was ever holding it.
	(void)g_claimant.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed);
}

const void* FileCatalogue::claimant() {
	return g_claimant.load(std::memory_order_acquire);
}

} // namespace deluge::storage

// The cull window's two knobs. Namespace-scope globals with external linkage, exactly as they were
// beside `Browser`'s listing before the extraction -- `sim/scenes_spec/file_catalogue_spec.cpp`
// declares them `extern` to drive a cull deterministically, so their linkage is part of what the
// move had to preserve.
int32_t maxNumFileItemsNow;
int32_t catalogSearchDirection;

namespace deluge::storage {

void FileCatalogue::emptyFileItems() {

	AudioEngine::logAction("emptyFileItems");

	// Erase in chunks from the end, keeping the audio routine fed in between - this can be thousands of items.
	while (!fileItems.empty()) {
		size_t chunk = std::min<size_t>(256, fileItems.size());
		fileItems.erase(fileItems.end() - chunk, fileItems.end());
		AudioEngine::logAction("emptyFileItems in loop");
		AudioEngine::routineWithClusterLoading();
	}
	fileItems.shrink_to_fit();

	AudioEngine::logAction("emptyFileItems 3");
}

void FileCatalogue::deleteSomeFileItems(int32_t startAt, int32_t stopAt) {

	// Erase in chunks from the back of the range, keeping the audio routine fed in between.
	while (stopAt > startAt) {
		int32_t chunkStart = std::max(startAt, stopAt - 256);
		fileItems.erase(fileItems.begin() + chunkStart, fileItems.begin() + stopAt);
		stopAt = chunkStart;
		AudioEngine::routineWithClusterLoading();
	}
}

FileItem* FileCatalogue::getNewFileItem(bool interpretNoteNames) {
	bool alreadyCulled = false;

	if (static_cast<int32_t>(fileItems.size()) >= maxNumFileItemsNow) {
doCull:
		cullSomeFileItems(interpretNoteNames);
		alreadyCulled = true;
	}

	try {
		fileItems.push_back(FileItem());
	} catch (deluge::exception&) {
		if (alreadyCulled) {
			return nullptr;
		}
		goto doCull;
	}

	return &fileItems.back();
}

void FileCatalogue::cullSomeFileItems(bool interpretNoteNames) {
	sortFileItems(interpretNoteNames);

	int32_t startAt, stopAt;

	int32_t numFileItemsDeletingNow =
	    static_cast<int32_t>(fileItems.size()) - (maxNumFileItemsNow >> 1); // May get modified below.
	if (numFileItemsDeletingNow <= 0) {
		return;
	}

	// If we already know what side we want to be deleting on...
	if (catalogSearchDirection == CATALOG_SEARCH_LEFT) {
deleteFromLeftSide:
		numFileItemsDeletedAtStart += numFileItemsDeletingNow;
		startAt = 0;
		stopAt = numFileItemsDeletingNow;
		// An owning copy, deliberately: `displayName()` returns a reference to a std::string held
		// *inside* a FileItem in `fileItems`, and the erase below destroys that very element.
		firstFileItemRemaining = (&fileItems[numFileItemsDeletingNow])->displayName();
	}
	else if (catalogSearchDirection == CATALOG_SEARCH_RIGHT) {
deleteFromRightSide:
		numFileItemsDeletedAtEnd += numFileItemsDeletingNow;
		stopAt = static_cast<int32_t>(fileItems.size());
		startAt = stopAt - numFileItemsDeletingNow;
		lastFileItemRemaining = (&fileItems[startAt - 1])->displayName();
	}

	// Or if we've been using a search term *and* searching both directions, try to tend towards keeping equal amounts
	// of FileIems either side.
	else {

		shouldInterpretNoteNames = interpretNoteNames;
		octaveStartsFromA = false;
		int32_t foundIndex = searchFileItems(filenameToStartSearchAt);

		// If search-item is in second half, delete from start.
		if ((foundIndex << 1) >= static_cast<int32_t>(fileItems.size())) {
			int32_t newNumFilesDeleting =
			    foundIndex >> 1; // Delete half the existing items to the left of the search-item.
			if (newNumFilesDeleting <= 0) {
				return;
			}
			if (numFileItemsDeletingNow > newNumFilesDeleting) {
				numFileItemsDeletingNow = newNumFilesDeleting;
			}
			goto deleteFromLeftSide;
		}

		// Or, vice versa.
		else {
			int32_t newNumFilesDeleting = (static_cast<int32_t>(fileItems.size()) - foundIndex)
			                              >> 1; // Delete half the existing items to the right of the search-item.
			if (newNumFilesDeleting <= 0) {
				return;
			}
			if (numFileItemsDeletingNow > newNumFilesDeleting) {
				numFileItemsDeletingNow = newNumFilesDeleting;
			}
			goto deleteFromRightSide;
		}
	}

	if (startAt != stopAt) {
		deleteSomeFileItems(startAt, stopAt); // Check might not be necessary?
	}
}

Error FileCatalogue::readFileItemsForFolder(char const* filePrefixHere, bool allowFolders,
                                            char const** allowedFileExtensionsHere, char const* filenameToStartAt,
                                            int32_t newMaxNumFileItems, int32_t newCatalogSearchDirection,
                                            bool interpretNoteNames) {

	AudioEngine::logAction("readFileItemsForFolder");

	emptyFileItems();

	Error error = StorageManager::initSD();
	if (error != Error::NONE) {
		return error;
	}

	// Local RAII handle (not the shared FatFS::Directory staticDIR global) — the port selector picks
	// efatfs or C-FatFS underneath; the destructor closes it on every return path below, so there's no
	// manual close() to forget or double up on.
	auto dir = D_TRY_CATCH_MOVE(deluge::io::Directory::open(currentDir), error,
	                            { return delugeStatusToError(deluge::io::to_deluge_status(error)); });

	numFileItemsDeletedAtStart = 0;
	numFileItemsDeletedAtEnd = 0;
	firstFileItemRemaining.clear();
	lastFileItemRemaining.clear();
	catalogSearchDirection = newCatalogSearchDirection;
	maxNumFileItemsNow = newMaxNumFileItems;
	filenameToStartSearchAt = filenameToStartAt;

	while (true) {
		AudioEngine::logAction("while loop");

		std::optional<DelugeDirEntry> entry = D_TRY_CATCH(dir.read(), error, {
			break; // Break on error
		});

		if (!entry.has_value()) {
			break; /* Break on end of dir */
		}
		if (entry->name[0] == '.') {
			continue; /* Ignore dot entry */
		}
		bool isFolder = entry->is_directory;
		if (isFolder) {
			if (!allowFolders) {
				continue;
			}
		}
		else {
			char const* dotPos = strrchr(entry->name, '.');
			if (!dotPos) {
extensionNotSupported:
				continue;
			}
			char const* fileExtension = dotPos + 1;
			char const** thisExtension = allowedFileExtensionsHere;
			while (strcasecmp(fileExtension, *thisExtension)) {
				thisExtension++;
				if (!*thisExtension) {
					goto extensionNotSupported; // If reached end of list
				}
			}
		}

		FileItem* thisItem = getNewFileItem(interpretNoteNames);
		if (!thisItem) {
			error = Error::INSUFFICIENT_RAM;
			break;
		}
		thisItem->filename = entry->name;
		thisItem->isFolder = isFolder;

		// displayName is the sort key, and must equal the real on-card name. The 7SEG short form ("185")
		// is produced at render time, not stored here - storing it made enteredText display-dependent, which is what
		// broke default naming on 7SEG (#1069).
	}

	if (error != Error::NONE) {
		emptyFileItems();
	}

	return error;
}

void FileCatalogue::deleteFolderAndDuplicateItems(Availability instrumentAvailabilityRequirement) {
	int32_t writeI = 0;
	FileItem* nextItem = fileItems.data();

	for (int32_t readI = 0; readI < static_cast<int32_t>(fileItems.size()); readI++) {
		FileItem* readItem = nextItem;

		// If there's a next item after "this" item, to compare it to...
		if (readI < static_cast<int32_t>(fileItems.size()) - 1) {
			nextItem = &fileItems[readI + 1];

			// If we're a folder, and the next item is a file of the same name, delete this item.
			if (readItem->isFolder) {
				if (!nextItem->isFolder) {
					int32_t nameLength = readItem->filename.size();
					char const* nextItemFilename = nextItem->filename.c_str();
					if (!memcasecmp(readItem->filename.c_str(), nextItemFilename, nameLength)) {
						if (nextItemFilename[nameLength] == '.' && !strchr(&nextItemFilename[nameLength + 1], '.')) {
							goto deleteThisItem;
						}
					}
				}
			}

			// Or if we have an Instrument, and the next item is a file of the same name, delete the next item.
			else if (readItem->instrument) {
				if (!nextItem->instrument && !nextItem->isFolder) {
					if (!strcasecmp(readItem->displayName().c_str(), nextItem->displayName().c_str())) {
						// if (readItem->filename.equalsCaseIrrespective(&nextItem->filename)) {
						readI++; // Skip the next item; it'll be overwritten by compaction or erased below.
						nextItem = fileItems.data() + (readI + 1);
						// That may be an out-of-range address, but in that case, it won't get read.
					}
				}

checkAvailabilityRequirement:
				// Check Instrument's availabilityRequirement
				if (readItem->instrumentAlreadyInSong) {
					if (instrumentAvailabilityRequirement == Availability::INSTRUMENT_UNUSED) {
deleteThisItem: // Just skip it; it'll be overwritten by compaction or erased below.
						continue;
					}
					else if (instrumentAvailabilityRequirement == Availability::INSTRUMENT_AVAILABLE_IN_SESSION) {
						if (currentSong->doesOutputHaveActiveClipInSession(readItem->instrument)) {
							goto deleteThisItem;
						}
					}
				}
			}

			// Or if next item has an Instrument, and we're just a file...
			else if (nextItem->instrument) {
				if (!strcasecmp(readItem->displayName().c_str(),
				                nextItem->displayName().c_str())) { // And if same name...
					goto deleteThisItem;
				}
			}
		}
		else {
			if (readItem->instrument) {
				goto checkAvailabilityRequirement;
			}
		}

		if (&fileItems[writeI] != readItem) {
			fileItems[writeI] = std::move(*readItem);
		}
		writeI++;
	}

	fileItems.erase(fileItems.begin() + writeI, fileItems.end());

	// Our system of keeping FileItems from getting too full by deleting elements from its ends as we go could have
	// caused bad results at the edges of the above, so delete a further one element at each end as needed.
	if (!firstFileItemRemaining.empty()) {
		fileItems.erase(fileItems.begin());
	}
	if (!lastFileItemRemaining.empty()) {
		fileItems.pop_back();
	}
}

namespace {
/// @brief What the last `sortFileItems` pinned into `util/name_compare.h`'s globals.
///
/// `fileItems`' order is only meaningful under the comparator that produced it, and
/// `searchFileItems` is a binary search over that order -- so the two have to agree. Recording the
/// pinning here is what lets `repinComparator()` restore it for a caller that cannot name those
/// globals, and what lets a spec assert the pairing through `sortedComparator()`. Deliberately NOT
/// an assert inside `searchFileItems`: see the note below -- sorting through this class is not a
/// precondition of searching, and a caller may legitimately search an order this class never made.
/// @{
bool sortedInterpretNoteNames = false;
bool sortedOctaveStartsFromA = false;
bool everSorted = false;
/// @}
} // namespace

void FileCatalogue::repinComparator() {
	if (!everSorted) {
		return; // nothing sorted yet, so no order to be wrong about
	}
	shouldInterpretNoteNames = sortedInterpretNoteNames;
	octaveStartsFromA = sortedOctaveStartsFromA;
}

bool FileCatalogue::sortedComparator(bool& interpretNoteNames) {
	if (everSorted) {
		interpretNoteNames = sortedInterpretNoteNames;
	}
	return everSorted;
}

void FileCatalogue::sortFileItems(bool interpretNoteNames) {
	shouldInterpretNoteNames = interpretNoteNames;
	octaveStartsFromA = false;
	sortedInterpretNoteNames = shouldInterpretNoteNames;
	sortedOctaveStartsFromA = octaveStartsFromA;
	everSorted = true;

	std::sort(fileItems.begin(), fileItems.end(), [](FileItem const& a, FileItem const& b) {
		return strcmpspecial(a.displayName().c_str(), b.displayName().c_str()) < 0;
	});
}

int32_t FileCatalogue::searchFileItems(char const* searchString, bool* foundExact) {
	// NO ASSERT HERE that the comparator's globals still match what `sortFileItems` pinned, and the
	// reason is worth writing down because it is the obvious thing to add (E3 task 3 fix round 1
	// proposed exactly that, and it was tried): **sorting through this class is not a precondition
	// of searching.** This function's contract is "set the globals first", and `fileItems` is a
	// public vector anything may order by hand -- `sim/scenes_spec/file_catalogue_spec.cpp`'s
	// `sort_under` does, precisely because `sortFileItems` forces `octaveStartsFromA = false` and
	// the octave-from-A characterisation needs a list sorted under the comparator it will then
	// search. An assert would have failed that legitimate, legacy-characterising pattern. What is
	// enforceable is the *mechanism* rather than the check: `repinComparator()` above, for the
	// callers that cannot spell the globals themselves.
	// Project each item to a plain C string so the comparator is symmetric in its argument types, as
	// ranges::lower_bound requires of a strict weak order. The projected pointer never outlives the call.
	auto it = std::ranges::lower_bound(
	    fileItems, searchString, [](char const* a, char const* b) { return strcmpspecial(a, b) < 0; },
	    [](FileItem const& f) { return f.displayName().c_str(); });
	if (foundExact != nullptr) {
		*foundExact = (it != fileItems.end() && strcmpspecial(it->displayName().c_str(), searchString) == 0);
	}
	return static_cast<int32_t>(it - fileItems.begin());
}

} // namespace deluge::storage
