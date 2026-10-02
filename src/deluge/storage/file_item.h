/*
 * Copyright © 2022-2023 Synthstrom Audible Limited
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

#include "storage/storage_manager.h"
#include "util/c_string.h"
#include <cstdint>
#include <string>

class FileItem {
public:
	FileItem() = default;
	Error setupWithInstrument(Instrument* newInstrument, bool hibernating);
	[[nodiscard]] std::string getFilenameWithExtension() const;
	[[nodiscard]] std::string getFilenameWithoutExtension() const;

	/// @brief Text shown to the user for this entry, and the sort/search key. Always includes the extension.
	///
	/// Derived on every call rather than cached. It used to be a `char const*` initialised to
	/// `filename.c_str()`, which made it a raw alias into a std::string stored *inside* this object: every
	/// reallocation of the owning `Browser::fileItems` vector moved the FileItems, and the cached pointer
	/// went on referring to the old, freed buffer — which by then held another entry's text. Because
	/// `searchFileItems()` sorts and binary-searches on this key and
	/// `setEnteredTextFromCurrentFilename()` assigns from it, the browser sorted, matched and labelled
	/// rows using freed memory, so the highlighted entry showed a different file's name than the row it
	/// sat on (and `getCurrentFileItem()` — hence LOAD — could resolve to the wrong file).
	[[nodiscard]] const std::string& displayName() const { return filename; }

	std::string filename{}; // May or may not include file extension. (Or actually I think it always does now...)
	Instrument* instrument = nullptr;
	bool maybeExistsOnCard{true}; // only false when made through setupWithInstrument through an unsaved instrument
	bool isFolder{};
	bool instrumentAlreadyInSong = false; // Only valid if instrument is set to something.
	bool filenameIncludesExtension = true;
};
