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

#include "gui/ui/load/slot_name_scan.h"

#include "gui/ui/browser/default_name.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "util/functions.h" // getThingName

namespace deluge::gui {

void SlotNameScan::arm(LoadInstrumentPresetUI& ui, std::string_view dir, OutputType outputType) {
	ui_ = &ui;
	dir_ = dir;
	outputType_ = outputType;
	listing_.clear();
	result_ = std::unexpected{Error::UNSPECIFIED};
	phase_ = ScanPhase::Read;
}

bool SlotNameScan::tick() {
	if (phase_ != ScanPhase::Read) {
		return false;
	}
	// The key and the prefix the walk this replaces used: search LEFT of "<thing>:", the colon sorting after every
	// digit, so the listing keeps the numbered entries and the last of them is the highest.
	const char* thingName = (outputType_ == OutputType::SYNTH) ? "SYNT" : "KIT";
	LoadInstrumentPresetUI::ListingRead read{
	    .dir = dir_,
	    .outputType = outputType_,
	    .filePrefix = getThingName(outputType_),
	    .filenameToStartAt = std::string{thingName} + ":",
	    .allowFolders = false,
	    .catalogSearchDirection = deluge::gui::browser::CatalogSearch::Left,
	};
	// The extension filter is browser-wide, and this scan spans ticks: borrowed for the one call that
	// reads it and put back, so a browser opened while the scan waits keeps its own.
	char const** const browserExtensions = LoadInstrumentPresetUI::allowedFileExtensions;
	LoadInstrumentPresetUI::allowedFileExtensions = allowedFileExtensionsXML;
	const Error error = ui_->readFolder(listing_, read);
	LoadInstrumentPresetUI::allowedFileExtensions = browserExtensions;
	if (error == Error::LISTING_DEFERRED) {
		return true; // not ready: this same open is asked for again next tick
	}
	if (error != Error::NONE) {
		listing_.clear();
		result_ = std::unexpected{error};
		phase_ = ScanPhase::Failed;
		return false;
	}
	listing_.sort();
	result_ = deluge::gui::browser::nextSlotName(
	    listing_.empty() ? std::string{} : listing_.item(listing_.size() - 1).getFilenameWithoutExtension(), thingName);
	listing_.clear();
	phase_ = ScanPhase::Done;
	return false;
}

} // namespace deluge::gui
