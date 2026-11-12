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

#include "gui/ui/load/preset_navigation.h"

#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/load/navigation_step.h"
#include "model/song/song.h"
#include "storage/file_item.h"
#include "util/functions.h" // getThingName

#include <algorithm>
#include <iterator>
#include <variant>
#include <vector>

namespace deluge::gui {

void PresetNavigation::arm(LoadInstrumentPresetUI& ui, const deluge::storage::AcquireWalkSpec& spec) {
	ui_ = &ui;
	offset_ = spec.offset;
	oldInstrument_ = spec.oldInstrument;
	outputType_ = spec.outputType;
	skipInSong_ = spec.availability == Availability::INSTRUMENT_UNUSED;
	if (spec.pile.has_value()) {
		pile_ = *spec.pile;
		dir_ = pile_.dirPath;
		searchName_ = pile_.listedFrom;
		searchRight_ = pile_.listedRight;
		mode_ = Mode::FindLanding;
	}
	else {
		dir_ = spec.dirPath;
		searchName_ = spec.name + ".XML";
		searchRight_ = offset_ >= 0;
		mode_ = Mode::Step;
	}
	wraps_ = 0;
	reads_ = 0;
	listing_.clear();
	result_ = std::unexpected{Error::UNSPECIFIED};
	phase_ = Phase::Read;
}

void PresetNavigation::finish(std::expected<FileItem*, Error> result) {
	if (!result.has_value()) {
		listing_.clear();
	}
	result_ = result;
	phase_ = Phase::Done;
}

void PresetNavigation::land(int32_t index) {
	FileItem& item = listing_.item(index);
	landing_ = {.dirPath = dir_,
	            .listedFrom = searchName_,
	            .listedRight = searchRight_,
	            .landed = std::string{item.displayName()},
	            .landedInstrument = item.instrument,
	            .oldName = item.instrument != nullptr ? searchName_ : std::string{item.displayName()}};
	finish(&item);
}

void PresetNavigation::followPile(const PileDecision& decision) {
	if (const Land* landed = std::get_if<Land>(&decision)) {
		land(landed->index);
	}
	else if (std::holds_alternative<ReadAgain>(decision)) {
		searchName_ = pile_.oldName;
		searchRight_ = offset_ >= 0;
		mode_ = Mode::MovePile;
	}
	else if (std::holds_alternative<FromEnd>(decision)) {
		// Main's `searchFromOneEnd`: the name is cleared for good, so a later read past an edge starts there too.
		pile_.oldName.clear();
		searchName_.clear();
		searchRight_ = offset_ >= 0;
		mode_ = Mode::MovePile;
	}
	else {
		finish(std::unexpected{Error::NO_ERROR_BUT_GET_OUT});
	}
}

bool PresetNavigation::tick() {
	while (phase_ == Phase::Read) {
		if (reads_ >= kMaxNavigationReads) {
			giveUps_++;
			finish(std::unexpected{Error::NO_ERROR_BUT_GET_OUT});
			return false;
		}
		// XML passed as the parameter, not through the browser-wide filter, which this never touches.
		const Error error = ui_->readFileItemsForFolder(
		    listing_, dir_, getThingName(outputType_), false, allowedFileExtensionsXML,
		    searchName_.empty() ? nullptr : searchName_.c_str(), FILE_ITEMS_MAX_NUM_ELEMENTS_FOR_NAVIGATION,
		    searchRight_ ? deluge::gui::browser::CatalogSearch::Right : deluge::gui::browser::CatalogSearch::Left);
		if (error == Error::LISTING_DEFERRED) {
			return true;
		}
		reads_++;
		if (error != Error::NONE) {
			finish(std::unexpected{error});
			return false;
		}
		const Error songError = currentSong->addInstrumentsToFileItems(listing_, outputType_);
		if (songError != Error::NONE) {
			finish(std::unexpected{songError});
			return false;
		}
		listing_.sort();
		listing_.dropFoldersAndDuplicates(Availability::INSTRUMENT_AVAILABLE_IN_SESSION);

		std::vector<NavigationItem> items;
		items.reserve(listing_.size());
		for (const FileItem& item : listing_.items()) {
			// The name is a view of the item's own filename (displayName() returns a reference), and the listing is not
			// touched again before the decision is read.
			items.push_back({.isOld = item.instrument != nullptr && item.instrument == oldInstrument_,
			                 .inSong = item.instrument != nullptr && item.instrumentAlreadyInSong,
			                 .name = item.displayName()});
		}
		const NavigationWindow window{
		    .items = items, .deletedAtStart = listing_.numDeletedAtStart(), .deletedAtEnd = listing_.numDeletedAtEnd()};

		if (mode_ == Mode::FindLanding) {
			const auto found = std::ranges::find_if(listing_.items(), [&](const FileItem& item) {
				return pile_.landedInstrument != nullptr
				           ? item.instrument == pile_.landedInstrument
				           : item.instrument == nullptr && item.displayName() == pile_.landed;
			});
			if (found == listing_.items().end()) {
				// The earlier landing is gone from its window (a file removed meanwhile): one preset on from it.
				searchName_ = pile_.landed;
				searchRight_ = offset_ >= 0;
				mode_ = Mode::Step;
				continue;
			}
			const auto index = static_cast<int32_t>(std::distance(listing_.items().begin(), found));
			followPile(movePile(window, index + offset_, offset_, skipInSong_));
			continue;
		}
		if (mode_ == Mode::MovePile) {
			followPile(startPileWindow(window, offset_, skipInSong_, !searchName_.empty()));
			continue;
		}

		const NavigationDecision decision =
		    decideNavigation(window, offset_, skipInSong_, !searchName_.empty(), wraps_);
		if (const Land* landed = std::get_if<Land>(&decision)) {
			wraps_ += landed->wraps;
			land(landed->index);
		}
		else if (const PageOn* page = std::get_if<PageOn>(&decision)) {
			wraps_ += page->wraps;
			searchName_ = page->from; // continue past the trimmed edge; the next read defers on its open
		}
		else if (const FromEnd* end = std::get_if<FromEnd>(&decision)) {
			wraps_ += end->wraps;
			searchName_.clear();
		}
		else {
			finish(std::unexpected{Error::NO_ERROR_BUT_GET_OUT});
		}
	}
	return false;
}

} // namespace deluge::gui
