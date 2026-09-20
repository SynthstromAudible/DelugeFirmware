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

#include "gui/ui/load/preset_walk.h"

#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "storage/file_item.h"
#include "util/functions.h"

namespace deluge::gui {

void PresetWalk::arm(LoadInstrumentPresetUI& ui, Song* song, OutputType outputType,
                     Availability availabilityRequirement) {
	ui_ = &ui;
	song_ = song;
	outputType_ = outputType;
	availabilityRequirement_ = availabilityRequirement;

	result_ = std::unexpected{Error::NO_FURTHER_FILES_THIS_DIRECTION};
	maxDepthReached_ = 0;
	pagedReads_ = 0;
	depthGiveUps_ = 0;
	lastNameBeforeFiltering_.clear();
	descendIndex_ = 0;

	// The caller set currentDir; that is the root of the search, as it was for the recursive walk.
	frames_[0] = Frame{.dir = Browser::currentDir, .searchName = {}, .doingSubfolders = false};
	depth_ = 1;
	maxDepthReached_ = 1;
	phase_ = WalkPhase::ReadFolder;
}

bool PresetWalk::tick() {
	// The pure phases cost nothing and touch no card, so they run through in one pass -- the walk
	// they replace ran them inline too. Only a read that defers ends the pass, which keeps the "at
	// most one open per pass" property the listing machine needs.
	while (true) {
		switch (phase_) {
		case WalkPhase::ReadFolder:
			if (!stepReadFolder()) {
				return true; // deferred: the phase has not advanced, so the next pass re-asks
			}
			break;

		case WalkPhase::ScanFiles:
			stepScanFiles();
			break;

		case WalkPhase::ScanFolders:
			stepScanFolders();
			break;

		case WalkPhase::Descend:
			stepDescend();
			break;

		case WalkPhase::Idle:
		case WalkPhase::Done:
		case WalkPhase::Failed:
			return false;
		}
	}
}

bool PresetWalk::stepReadFolder() {
	Frame& frame = top();

	// Only assign when it actually differs: a resumed read must find the directory exactly as the
	// deferred one left it. Nothing rewrites it here -- the walk passes no `defaultDirToAlsoTry`, so
	// the missing-folder fallback that does the rewriting can never fire -- but re-assigning a string
	// every tick for nothing is worth skipping anyway.
	if (Browser::currentDir != frame.dir) {
		Browser::currentDir = frame.dir;
	}
	LoadInstrumentPresetUI::allowedFileExtensions = allowedFileExtensionsXML;

	// Named through LoadInstrumentPresetUI, not Browser: the type is PROTECTED, and this class is a
	// friend of the derived UI rather than of Browser itself. Clang rejects Browser::ListingRead here;
	// gcc accepts it, so the host build says nothing and only the device build catches it.
	LoadInstrumentPresetUI::ListingRead read{
	    .dir = frame.dir,
	    .outputType = outputType_,
	    .filePrefix = getThingName(outputType_),
	    .filenameToStartAt = frame.searchName,
	    .includeSongInstruments = song_ != nullptr,
	};
	const Error error = ui_->readFolder(Browser::listing, read);
	if (error == Error::LISTING_DEFERRED) {
		return false;
	}
	if (error != Error::NONE) {
		Browser::emptyFileItems();
		fail(error);
		return true;
	}

	ui_->sortFileItems();

	// No files at all means the end of the folder.
	if (Browser::listing.empty()) {
		if (!frame.doingSubfolders) {
			startDoingFolders();
		}
		else {
			frameExhausted();
		}
		return true;
	}

	// Taken BEFORE filtering, because filtering is what creates the gap that paging has to resume
	// across.
	lastNameBeforeFiltering_ = Browser::listing.items()[Browser::listing.size() - 1].displayName();

	Browser::listing.dropFoldersAndDuplicates(availabilityRequirement_);

	phase_ = frame.doingSubfolders ? WalkPhase::ScanFolders : WalkPhase::ScanFiles;
	return true;
}

void PresetWalk::stepScanFiles() {
	for (int32_t i = 0; i < static_cast<int32_t>(Browser::listing.size()); i++) {
		auto* fileItem = &Browser::listing.item(i);
		if (!fileItem->isFolder) {
			succeed(fileItem);
			return;
		}
	}

	// None here. Is there more of this folder to read?
	if (Browser::listing.numDeletedAtEnd()) {
		pageFrom(lastNameBeforeFiltering_);
		return;
	}

	// Every file seen and none usable, so look in subfolders. If the start of the folder was culled
	// away, the subfolders there have to be read back in first.
	if (Browser::listing.numDeletedAtStart()) {
		startDoingFolders();
		return;
	}

	// The start is still in memory, so the same list serves the subfolder pass -- no re-read.
	top().doingSubfolders = true;
	phase_ = WalkPhase::ScanFolders;
}

void PresetWalk::stepScanFolders() {
	for (int32_t i = 0; i < static_cast<int32_t>(Browser::listing.size()); i++) {
		if (Browser::listing.items()[i].isFolder) {
			descendIndex_ = i;
			phase_ = WalkPhase::Descend;
			return;
		}
	}

	if (Browser::listing.numDeletedAtEnd()) {
		pageFrom(lastNameBeforeFiltering_);
		return;
	}
	frameExhausted();
}

void PresetWalk::stepDescend() {
	auto* fileItem = &Browser::listing.items()[descendIndex_];
	Frame& frame = top();

	// Computed here, before the descent, exactly as the original did: once the child has read its own
	// folder this level's list is gone, and whether there was more of it is no longer discoverable.
	frame.anyMoreForLater =
	    Browser::listing.numDeletedAtEnd() || (descendIndex_ < static_cast<int32_t>(Browser::listing.size()) - 1);
	// Where this level resumes if the child finds nothing: at the subfolder that just failed.
	frame.searchName = fileItem->displayName();

	if (depth_ >= kMaxDepth) {
		// Counted, not silent. The search carries on past this subfolder rather than ending, so a
		// card nested deeper than the bound still yields every preset above it.
		depthGiveUps_++;
		childYieldedNothing();
		return;
	}

	std::string childDir = frame.dir;
	childDir.append("/");
	childDir.append(fileItem->filename);

	frames_[depth_] = Frame{.dir = std::move(childDir), .searchName = {}, .doingSubfolders = false};
	depth_++;
	if (depth_ > maxDepthReached_) {
		maxDepthReached_ = depth_;
	}
	phase_ = WalkPhase::ReadFolder;
}

void PresetWalk::pageFrom(const std::string& searchName) {
	top().searchName = searchName;
	pagedReads_++;
	phase_ = WalkPhase::ReadFolder;
}

void PresetWalk::startDoingFolders() {
	Frame& frame = top();
	frame.doingSubfolders = true;
	frame.searchName.clear();
	phase_ = WalkPhase::ReadFolder;
}

void PresetWalk::childYieldedNothing() {
	if (top().anyMoreForLater) {
		// Resume THIS level through the paging path, from the subfolder that just failed. Not by
		// advancing an index -- see the warning on the class.
		pagedReads_++;
		phase_ = WalkPhase::ReadFolder;
		return;
	}
	frameExhausted();
}

void PresetWalk::frameExhausted() {
	while (true) {
		frames_[depth_ - 1] = Frame{};
		depth_--;
		if (depth_ == 0) {
			fail(Error::NO_FURTHER_FILES_THIS_DIRECTION);
			return;
		}
		if (top().anyMoreForLater) {
			pagedReads_++;
			phase_ = WalkPhase::ReadFolder;
			return;
		}
	}
}

void PresetWalk::succeed(FileItem* found) {
	result_ = found;
	phase_ = WalkPhase::Done;
}

void PresetWalk::fail(Error error) {
	result_ = std::unexpected{error};
	phase_ = WalkPhase::Failed;
}

} // namespace deluge::gui
