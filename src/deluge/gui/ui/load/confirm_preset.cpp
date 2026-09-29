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

#include "gui/ui/load/confirm_preset.h"

#include "gui/ui/browser/browser.h"
#include "gui/ui/load/confirm_step.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "model/song/song.h"
#include "storage/file_item.h"
#include "util/functions.h" // getThingName, getInstrumentFolder

#include <string_view>
#include <variant>
#include <vector>

namespace deluge::gui {

void ConfirmPreset::arm(LoadInstrumentPresetUI& ui, const deluge::storage::AcquireWalkSpec& spec) {
	ui_ = &ui;
	dir_ = spec.dirPath;
	searchName_ = spec.name; // no extension
	outputType_ = spec.outputType;
	availability_ = spec.availability;
	grabbingLeftmost_ = false;
	walking_ = false;
	reads_ = 0;
	listing_.clear();
	result_ = std::unexpected{Error::UNSPECIFIED};
	phase_ = Phase::Read;
}

void ConfirmPreset::finish(std::expected<FileItem*, Error> result) {
	result_ = result;
	phase_ = Phase::Done;
}

void ConfirmPreset::startAnyPreset() {
	// Searches the type's root folder across the whole song, with the gesture's availability.
	listing_.clear();
	walking_ = true;
	walk_.arm(*ui_, getInstrumentFolder(outputType_), currentSong, outputType_, availability_);
	phase_ = Phase::Walk;
}

bool ConfirmPreset::tick() {
	for (;;) {
		if (phase_ == Phase::Walk) {
			if (walk_.tick()) {
				return true;
			}
			finish(walk_.result());
			return false;
		}
		if (phase_ != Phase::Read) {
			return false;
		}
		if (reads_ >= kMaxConfirmReads) {
			giveUps_++;
			listing_.clear();
			finish(std::unexpected{Error::NO_FURTHER_FILES_THIS_DIRECTION});
			return false;
		}
		// The extension filter is browser-wide: borrowed for the one call that reads it, and put back.
		char const** const browserExtensions = LoadInstrumentPresetUI::allowedFileExtensions;
		LoadInstrumentPresetUI::allowedFileExtensions = allowedFileExtensionsXML;
		LoadInstrumentPresetUI::ListingRead read{
		    .dir = dir_,
		    .outputType = outputType_,
		    .filePrefix = getThingName(outputType_),
		    .filenameToStartAt = searchName_,
		    .allowFolders = false,
		    .includeSongInstruments = currentSong != nullptr,
		};
		const Error error = ui_->readFolder(listing_, read);
		LoadInstrumentPresetUI::allowedFileExtensions = browserExtensions;
		if (error == Error::LISTING_DEFERRED) {
			return true;
		}
		reads_++;
		if (error == Error::FOLDER_DOESNT_EXIST) {
			startAnyPreset(); // the fallback's result is returned, not discarded
			continue;
		}
		if (error != Error::NONE) {
			listing_.clear();
			finish(std::unexpected{error});
			return false;
		}
		listing_.sort();

		// The read's names before filtering, owned here: filtering erases entries, and the decision needs the one
		// before any it pops.
		std::vector<std::string> names;
		names.reserve(listing_.size());
		for (const FileItem& item : listing_.items()) {
			names.push_back(item.displayName());
		}
		std::vector<std::string_view> readNames(names.begin(), names.end());
		const bool poppedLast = !listing_.lastRemaining().empty();
		listing_.dropFoldersAndDuplicates(availability_);

		const ConfirmDecision decision = decideConfirm({.readNames = readNames,
		                                                .poppedLast = poppedLast,
		                                                .shownCount = listing_.size(),
		                                                .deletedAtStart = listing_.numDeletedAtStart(),
		                                                .deletedAtEnd = listing_.numDeletedAtEnd()},
		                                               grabbingLeftmost_);
		if (std::holds_alternative<ConfirmLand>(decision)) {
			finish(&listing_.item(0));
			return false;
		}
		if (const auto* page = std::get_if<ConfirmPageOn>(&decision)) {
			searchName_ = page->from;
		}
		else if (std::holds_alternative<ConfirmFromStart>(decision)) {
			searchName_.clear();
			grabbingLeftmost_ = true;
		}
		else {
			startAnyPreset();
		}
	}
}

} // namespace deluge::gui
