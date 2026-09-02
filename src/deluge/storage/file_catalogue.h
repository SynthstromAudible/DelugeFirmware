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

	/// @name The single-flight claim -- the catalogue's own guard, not any one caller's
	///
	/// `fileItems`, `currentDir` and the cull window are one shared set of statics, so two listings
	/// running over them do not merely race for a request slot: they interleave two directory reads
	/// into one vector. Both dispatch paths in this tree therefore claim *here* rather than each
	/// keeping its own flag:
	///
	/// - `Browser::beginListing` (`gui/ui/browser/browser.cpp:97`), which previously guarded with
	///   its own static `listingInProgress_` (`browser.h:170`), and
	/// - `deluge::scenes::browser::Listing::request` (`src/scenes/browser/listing.cpp`), the
	///   tick-driven seam the scenes layer uses.
	///
	/// Before this, the two were mutually invisible: a legacy browser listing and a scenes listing
	/// could both believe they were the only one, over one shared vector. That is a live hazard for
	/// as long as the legacy UI and the scenes stack coexist, which is the whole point of this
	/// branch stack. Putting the guard with the resource makes it structural rather than a
	/// convention two files have to keep agreeing on.
	///
	/// @par The contract: hold it until the CONSUMER has finished reading, not until the read ends
	///
	/// A listing is not done when `readFileItemsForFolder` returns -- it is done when whoever asked
	/// for it has finished looking at `fileItems`. The two callers reach that point in different
	/// places, and both are correct:
	///
	/// - **Legacy** consumes inside the op. `runPendingListing` (`browser.cpp:126`) runs
	///   `arrivedInNewFolder`, the selection search, `folderContentsReady()` and the render
	///   *before* it returns, so releasing where it clears `listingInProgress_` (`browser.cpp:163`)
	///   is releasing after its consumer has read.
	/// - **The scenes seam** cannot consume inside the op -- that is the entire reason it exists --
	///   so it holds the claim until `take_result()`, one or more ticks later. `ListingResult`
	///   counts the entries rather than copying them, so the entries the caller will read are still
	///   these statics.
	///
	/// @warning **Advisory at the leaf, structural at the dispatchers.** `readFileItemsForFolder`
	///          and friends do NOT check the claim -- several legacy paths call them directly
	///          without dispatching a listing at all (`Browser::predictExtendedText`,
	///          `Browser::getUnusedSlot`, the save UIs' default-name search), and making the leaf
	///          enforce would break every one of them. The claim binds the two *dispatch* paths.
	///          Nor does it cover a browsing *session*: a browser sitting open between listings
	///          holds no claim, so a listing dispatched from elsewhere still replaces the vector
	///          under it. That is exactly the exposure legacy always had; closing it means claiming
	///          on open and releasing on close, which changes `Browser`'s lifecycle and belongs to
	///          whoever wires the scenes browser up for real.
	/// @{
	/// @brief Claim exclusive use of the catalogue for @p owner.
	/// @param owner An address identifying the claimant; only this value can release the claim.
	/// @return True if the claim was taken; false if anyone (including @p owner) already holds it.
	static bool claim(const void* owner);
	/// @brief Release a claim held by @p owner. A no-op if @p owner is not the current claimant,
	///        so an owner that never claimed, or was already released, may call it unconditionally.
	/// @param owner The address passed to `claim`.
	static void release(const void* owner);
	/// @brief Who currently holds the claim.
	/// @return The claimant's address, or nullptr if the catalogue is free.
	static const void* claimant();
	/// @}

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
