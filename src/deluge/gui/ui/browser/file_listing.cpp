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

#include "gui/ui/browser/file_listing.h"

#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "util/exceptions.h"
#include "util/functions.h"
#include "util/name_compare.h"

#include <algorithm>
#include <cstring>

namespace deluge::gui::browser {

void FileListing::beginRead(std::string_view dir, std::string_view searchName, CatalogSearch direction,
                            int32_t maxItems, Collation collation) {
	clear();
	dir_ = dir;
	searchName_ = searchName;
	direction_ = direction;
	maxItems_ = maxItems;
	collation_ = collation;
	numDeletedAtStart_ = 0;
	numDeletedAtEnd_ = 0;
	firstRemaining_.clear();
	lastRemaining_.clear();
}

void FileListing::clear() {
	AudioEngine::logAction("emptyFileItems");

	// Erase in chunks from the end, keeping the audio routine fed in between - this can be thousands of items.
	while (!items_.empty()) {
		size_t chunk = std::min<size_t>(256, items_.size());
		items_.erase(items_.end() - chunk, items_.end());
		AudioEngine::logAction("emptyFileItems in loop");
		AudioEngine::routineWithClusterLoading();
	}
	items_.shrink_to_fit();

	AudioEngine::logAction("emptyFileItems 3");
}

void FileListing::adopt(deluge::vector<FileItem> items) {
	items_ = std::move(items);
}

void FileListing::eraseAt(int32_t index) {
	items_.erase(items_.begin() + index);
}

void FileListing::eraseRange(int32_t startAt, int32_t stopAt) {
	// Erase in chunks from the back of the range, keeping the audio routine fed in between.
	while (stopAt > startAt) {
		int32_t chunkStart = std::max(startAt, stopAt - 256);
		items_.erase(items_.begin() + chunkStart, items_.begin() + stopAt);
		stopAt = chunkStart;
		AudioEngine::routineWithClusterLoading();
	}
}

FileItem* FileListing::newItem() {
	// Was a `goto doCull` backwards jump. Exactly ONE cull happens before giving up, whether it was
	// the cap or a failed push that asked for it -- culling twice would trim a second window off the
	// listing on the way to reporting no memory.
	bool alreadyCulled = false;
	if (size() >= maxItems_) {
		cull();
		alreadyCulled = true;
	}
	while (true) {
		try {
			items_.push_back(FileItem());
			return &items_.back();
		} catch (deluge::exception&) {
			if (alreadyCulled) {
				return nullptr;
			}
			cull();
			alreadyCulled = true;
		}
	}
}

SearchHit FileListing::search(char const* name) const {
	// Project each item to a plain C string so the comparator is symmetric in its argument types, as
	// ranges::lower_bound requires of a strict weak order. The projected pointer never outlives the call.
	auto it = std::ranges::lower_bound(
	    items_, name, [](char const* a, char const* b) { return strcmpspecial(a, b) < 0; },
	    [](FileItem const& f) { return f.displayName().c_str(); });
	return {
	    .index = static_cast<int32_t>(it - items_.begin()),
	    .exact = (it != items_.end() && strcmpspecial(it->displayName().c_str(), name) == 0),
	};
}

void FileListing::sort() {
	// strcmpspecial reads these two globals, and this is what has always set them. A sort that left
	// them stale would compare by the previous browser's rules -- a different order, with nothing in
	// the code that looks wrong.
	shouldInterpretNoteNames = collation_.interpretNoteNames;
	octaveStartsFromA = collation_.octaveStartsFromA;

	std::ranges::sort(items_, [](FileItem const& a, FileItem const& b) {
		return strcmpspecial(a.displayName().c_str(), b.displayName().c_str()) < 0;
	});

	// A read that searches one way from a name keeps only that side of it.
	if (!searchName_.empty()) {
		char const* searchName = searchName_.c_str();
		if (direction_ == CatalogSearch::Left) {
			const SearchHit hit = search(searchName);
			int32_t searchIndex = hit.index;
			// Check for duplicates.
			if (hit.exact) {
				int32_t prevIndex = searchIndex - 1;
				if (prevIndex >= 0 && !strcmpspecial(items_[prevIndex].displayName().c_str(), searchName)) {
					searchIndex = prevIndex;
				}
			}
			int32_t numToDelete = size() - searchIndex;
			if (numToDelete > 0) {
				eraseRange(searchIndex, size());
				numDeletedAtEnd_ += numToDelete;
			}
		}
		else if (direction_ == CatalogSearch::Right) {
			const SearchHit hit = search(searchName);
			int32_t searchIndex = hit.index;
			// Check for duplicates.
			if (hit.exact) {
				int32_t nextIndex = searchIndex + 1;
				if (nextIndex < size() && !strcmpspecial(items_[nextIndex].displayName().c_str(), searchName)) {
					searchIndex = nextIndex;
				}
			}
			int32_t numToDelete = searchIndex + static_cast<int32_t>(hit.exact);
			if (numToDelete > 0) {
				eraseRange(0, numToDelete);
				numDeletedAtStart_ += numToDelete;
			}
		}
	}

	// A cull earlier in this read deleted entries from one end or both. An entry read after that cull which sorts into
	// a deleted region is dropped here too, or the window has a hole in it: a 25-song folder read from its start
	// would hold SONG000-009 and then SONG020-023, because 010-019 were culled before 020-024 arrived.
	if (!lastRemaining_.empty()) {
		int32_t searchIndex = search(lastRemaining_.c_str()).index;
		int32_t itemsToDeleteAtEnd = size() - searchIndex - 1;
		if (itemsToDeleteAtEnd > 0) {
			eraseRange(searchIndex + 1, size());
			numDeletedAtEnd_ += itemsToDeleteAtEnd;
		}
	}

	if (!firstRemaining_.empty()) {
		int32_t itemsToDeleteAtStart = search(firstRemaining_.c_str()).index;
		if (itemsToDeleteAtStart > 0) {
			eraseRange(0, itemsToDeleteAtStart);
			numDeletedAtStart_ += itemsToDeleteAtStart;
		}
	}
}

void FileListing::cull() {
	sort();

	int32_t numDeletingNow = size() - (maxItems_ >> 1); // May get modified below.
	if (numDeletingNow <= 0) {
		return;
	}

	// Which end to trim. The search direction answers it outright; only a both-ways search has to
	// work it out, by keeping roughly equal amounts either side of the search name.
	enum class Trim : uint8_t { Start, End };
	Trim trim{};

	if (direction_ == CatalogSearch::Left) {
		trim = Trim::Start;
	}
	else if (direction_ == CatalogSearch::Right) {
		trim = Trim::End;
	}
	else {
		shouldInterpretNoteNames = collation_.interpretNoteNames;
		octaveStartsFromA = collation_.octaveStartsFromA;
		const int32_t foundIndex = search(searchName_.c_str()).index;

		// If the search item is in the second half, delete from the start, and vice versa. Either way
		// take half of what lies on that side, never more than we came here to delete.
		const bool searchItemInSecondHalf = (foundIndex << 1) >= size();
		const int32_t newNumDeleting = searchItemInSecondHalf ? (foundIndex >> 1) : ((size() - foundIndex) >> 1);
		if (newNumDeleting <= 0) {
			return;
		}
		numDeletingNow = std::min(numDeletingNow, newNumDeleting);
		trim = searchItemInSecondHalf ? Trim::Start : Trim::End;
	}

	int32_t startAt = 0;
	int32_t stopAt = 0;
	if (trim == Trim::Start) {
		numDeletedAtStart_ += numDeletingNow;
		startAt = 0;
		stopAt = numDeletingNow;
		firstRemaining_ = items_[numDeletingNow].displayName();
	}
	else {
		numDeletedAtEnd_ += numDeletingNow;
		stopAt = size();
		startAt = stopAt - numDeletingNow;
		lastRemaining_ = items_[startAt - 1].displayName();
	}

	if (startAt != stopAt) {
		eraseRange(startAt, stopAt); // Check might not be necessary?
	}
}

void FileListing::dropFoldersAndDuplicates(Availability requirement) {
	int32_t writeI = 0;
	FileItem* nextItem = items_.data();

	for (int32_t readI = 0; readI < size(); readI++) {
		FileItem* readItem = nextItem;

		// If there's a next item after "this" item, to compare it to...
		if (readI < size() - 1) {
			nextItem = &items_[readI + 1];

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
						readI++; // Skip the next item; it'll be overwritten by compaction or erased below.
						nextItem = items_.data() + (readI + 1);
						// That may be an out-of-range address, but in that case, it won't get read.
					}
				}

checkAvailabilityRequirement:
				// Check Instrument's availabilityRequirement
				if (readItem->instrumentAlreadyInSong) {
					if (requirement == Availability::INSTRUMENT_UNUSED) {
deleteThisItem: // Just skip it; it'll be overwritten by compaction or erased below.
						continue;
					}
					else if (requirement == Availability::INSTRUMENT_AVAILABLE_IN_SESSION) {
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

		if (&items_[writeI] != readItem) {
			items_[writeI] = std::move(*readItem);
		}
		writeI++;
	}

	items_.erase(items_.begin() + writeI, items_.end());

	// Our system of keeping FileItems from getting too full by deleting elements from its ends as we go could have
	// caused bad results at the edges of the above, so delete a further one element at each end as needed.
	//
	// Deliberately NOT counted in numDeletedAtStart_/End_: those record what the READ's cull dropped,
	// which is what paging resumes from. This extra edge trim is a different thing, and adding it to
	// the counters would make a caller page from the wrong name.
	if (!firstRemaining_.empty()) {
		items_.erase(items_.begin());
	}
	if (!lastRemaining_.empty()) {
		items_.pop_back();
	}
}

} // namespace deluge::gui::browser
