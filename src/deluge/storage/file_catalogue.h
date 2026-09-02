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

#pragma once

#include "definitions_cxx.hpp"
#include "storage/file_item.h"
#include "util/containers.h"
#include <string>

#define CATALOG_SEARCH_LEFT 0
#define CATALOG_SEARCH_RIGHT 1
#define CATALOG_SEARCH_BOTH 2

#define FILE_ITEMS_MAX_NUM_ELEMENTS 20
#define FILE_ITEMS_MAX_NUM_ELEMENTS_FOR_NAVIGATION 20 // It "should" be able to be way less than this.

/// The extensions a preset/song/definition listing accepts, NULL-terminated.
extern char const* allowedFileExtensionsXML[];

namespace deluge::storage {

/// @brief The browser's file listing, with no browser attached: the sorted `FileItem` vector, the
///        culling window that keeps it inside RAM, and the directory it was read from.
///
/// Every member here was a member of `Browser` (`gui/ui/browser/browser.h`) and moved out verbatim,
/// keeping its name, so a reader maps the two one-to-one. None of it draws, takes input or knows
/// what a UI is; what it needs is the SD card, `FileItem`, and the name comparator. It lives in
/// `storage/` because the scenes layer (`src/scenes/`) may include `storage/` and may not include
/// `gui/`, and the file browser, the sample marker editor and the audio recorder all need a
/// listing.
///
/// `Browser`'s own members are now references and one-line forwards onto this class
/// (`browser.cpp`), so no `gui/` call site changed: `Browser::fileItems` and
/// `FileCatalogue::fileItems` name the same object, not two that agree.
///
/// **Lifetime.** Static, process-wide, exactly as `Browser`'s statics were: one listing at a time,
/// shared by whichever browser is open. Not a Meyers singleton -- these are namespace-scope statics
/// in `file_catalogue.cpp`, so `Browser`'s reference members bind to their addresses at load time
/// (constant initialisation) rather than racing a first-use guard.
///
/// **Note names.** `sortFileItems`, `cullSomeFileItems`, `getNewFileItem` and
/// `readFileItemsForFolder` take `interpretNoteNames` explicitly. In `Browser` this came from the
/// per-browser `shouldInterpretNoteNamesForThisBrowser` flag (`browser.h`), which is the only
/// instance state the listing ever read -- and the only reason these were instance members at all.
/// Passing it makes them static, and makes the dependency visible at the call.
class FileCatalogue {
public:
	/// @brief Fill the catalogue from `currentDir`, replacing whatever was in it.
	///
	/// @param filePrefixHere               Unused; kept from the legacy signature.
	/// @param allowFolders                 Whether directory entries are listed at all.
	/// @param allowedFileExtensionsHere    NULL-terminated list of extensions to accept.
	/// @param filenameToStartAt            The name a `CATALOG_SEARCH_BOTH` cull centres on.
	/// @param newMaxNumFileItems           Entry count at which `getNewFileItem` starts culling.
	/// @param newCatalogSearchDirection    One of the `CATALOG_SEARCH_*` values.
	/// @param interpretNoteNames           Sort/search note names as notes rather than letters.
	static Error readFileItemsForFolder(char const* filePrefixHere, bool allowFolders,
	                                    char const** allowedFileExtensionsHere, char const* filenameToStartAt,
	                                    int32_t newMaxNumFileItems, int32_t newCatalogSearchDirection,
	                                    bool interpretNoteNames);

	/// @brief Sort `fileItems` by `displayName()` per `strcmpspecial`. Pins `octaveStartsFromA`
	///        false, as legacy did.
	static void sortFileItems(bool interpretNoteNames);

	/// Mirrors CStringArray::search(): binary search by displayName using strcmpspecial; returns the matching
	/// index, or the insertion point if not found. Set shouldInterpretNoteNames and octaveStartsFromA first.
	static int32_t searchFileItems(char const* searchString, bool* foundExact = nullptr);

	/// @brief Append one entry, culling first if the catalogue is at its cap.
	/// @return The new entry, or nullptr if even culling could not make room.
	static FileItem* getNewFileItem(bool interpretNoteNames);

	/// @brief Erase everything and release the vector's storage.
	static void emptyFileItems();

	/// @brief Erase `[startAt, stopAt)`.
	static void deleteSomeFileItems(int32_t startAt, int32_t stopAt);

	/// @brief Drop folders shadowed by a same-named file, duplicate presets, and instruments the
	///        caller's availability requirement rules out.
	static void deleteFolderAndDuplicateItems(Availability instrumentAvailabilityRequirement = Availability::ANY);

	/// @brief Halve the catalogue from whichever end the search direction points away from.
	static void cullSomeFileItems(bool interpretNoteNames);

	static std::string currentDir;
	static deluge::vector<FileItem> fileItems; // Sorted by displayName per strcmpspecial (see sortFileItems())
	static int32_t numFileItemsDeletedAtStart;
	static int32_t numFileItemsDeletedAtEnd;
	/// Names of the entries that bracket the culled window, remembered ACROSS the cull that erases them.
	/// Owning copies, deliberately: these used to be `char const*` aliases into FileItem::filename, i.e.
	/// pointers into vector elements this very code then erased.
	static std::string firstFileItemRemaining;
	static std::string lastFileItemRemaining;

	static char const* filenameToStartSearchAt;
};

} // namespace deluge::storage
