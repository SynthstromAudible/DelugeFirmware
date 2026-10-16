#pragma once

/// @file
/// @brief What the file catalogue's specs share: the clean-catalogue guard and the catalogue as one browser drives
///        it. `file_catalogue_spec.cpp` and `file_catalogue_trio_spec.cpp` run on different cards, so they are two
///        files and this is their common part.
///
/// @warning **Include this BEFORE `<cppspec.hpp>`** (see `card_access.h`).

#include "definitions_cxx.hpp"
#include "storage/file_catalogue.h"
#include "storage/file_item.h"
#include "util/name_compare.h"

#include "support/card_access.h"

#include <cstdint>

namespace deluge::scenes::spec::catalogue {

using deluge::scenes::spec::QuietAudio;
using deluge::storage::FileCatalogue;

/// @brief Leaves the catalogue empty, its window counters zeroed and the name comparator's flags
///        back at their defaults, on both sides of an example.
///
/// The catalogue is process-global static state (that is exactly the shape this plan is moving),
/// and one spec binary runs many examples in one process, so an example that left entries behind
/// would decide the next one's result by link order.
///
/// `shouldInterpretNoteNames` (`util/name_compare.cpp:85`) and `octaveStartsFromA` (`:87`) are
/// process-global for the same reason and leak the same way -- and worse, silently: they change
/// what `strcmpspecial` *means*, so a stale `true` turns a later example's expected sort order into
/// a different one rather than into an obvious crash. Nearly every example here moves them (every
/// `sortFileItems` and every `CATALOG_SEARCH_BOTH` cull writes them), so they belong in the
/// fixture, not in an ad-hoc restore at the bottom of whichever example happened to notice.
struct CleanCatalogue {
	/// Declared first, so it is latched before this guard's own body erases anything and released
	/// only after the body has run on the way out.
	QuietAudio quiet;

	CleanCatalogue() { reset(); }
	~CleanCatalogue() { reset(); }
	static void reset() {
		FileCatalogue::emptyFileItems();
		FileCatalogue::numFileItemsDeletedAtStart = 0;
		FileCatalogue::numFileItemsDeletedAtEnd = 0;
		FileCatalogue::firstFileItemRemaining.clear();
		FileCatalogue::lastFileItemRemaining.clear();
		FileCatalogue::filenameToStartSearchAt = nullptr;
		FileCatalogue::currentDir.clear();
		// The catalogue's single-flight claim is process-global too, and a leaked one would refuse
		// every listing in every later example rather than failing this one.
		deluge::storage::FileCatalogue::release(deluge::storage::FileCatalogue::claimant());
		shouldInterpretNoteNames = false;
		octaveStartsFromA = false;
	}
	CleanCatalogue(const CleanCatalogue&) = delete;
	CleanCatalogue& operator=(const CleanCatalogue&) = delete;
};

/// @brief The catalogue as one browser drives it: `FileCatalogue`'s statics plus the note-name flag a
///        browser passes to each listing, sort and cull (`shouldInterpretNoteNamesForThisBrowser`).
struct SpecBrowser {
	bool note_names = false;

	void interpretNoteNames(bool on) { note_names = on; }
	Error readFileItemsForFolder(char const* prefix, bool allow_folders, char const** extensions, char const* start_at,
	                             int32_t max_items, int32_t direction = CATALOG_SEARCH_BOTH) const {
		return FileCatalogue::readFileItemsForFolder(prefix, allow_folders, extensions, start_at, max_items, direction,
		                                             note_names);
	}
	void sortFileItems() const { FileCatalogue::sortFileItems(note_names); }
	void cullSomeFileItems() const { FileCatalogue::cullSomeFileItems(note_names); }
	FileItem* getNewFileItem() const { return FileCatalogue::getNewFileItem(note_names); }
};

} // namespace deluge::scenes::spec::catalogue
