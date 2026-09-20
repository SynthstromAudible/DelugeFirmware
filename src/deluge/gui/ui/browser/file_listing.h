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

#pragma once

#include "definitions_cxx.hpp"
#include "storage/file_item.h"
#include "util/containers.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

class Browser;

namespace deluge::gui::browser {

/// @brief Where a name sits in a sorted listing, and whether it is actually there.
///
/// @warning `index` is an INSERTION POINT when `exact` is false, so it can equal the list's size.
///          A caller that indexes straight into the list must check the bound -- reading past the
///          end on a name that was not found has been a real defect here before.
struct SearchHit {
	int32_t index{}; ///< the match, or where the name would be inserted
	bool exact{};    ///< whether @ref index names a real entry rather than a gap
};

/// @brief Which way a read searches from its search name, and so which side of it is kept.
///
/// Left and Right keep one side of the search name; Both keeps a window around it, and is the only
/// mode that consults the search name during a cull.
enum class CatalogSearch : uint8_t {
	Left,  ///< keep what sorts before the search name
	Right, ///< keep what sorts after it
	Both,  ///< keep a window around it, tending towards equal counts either side
};

/// @brief How names are collated when a listing is sorted and searched.
///
/// Display policy rather than listing state -- it comes from the browser showing the list, not from
/// the folder -- so it is handed to the listing at the start of a read rather than living in another
/// global beside the two it already feeds.
struct Collation {
	bool interpretNoteNames{};
	bool octaveStartsFromA{};
};

/// @brief One directory's contents, as read and trimmed for display or for a walk.
///
/// ## Why this is a type and not seven statics
///
/// It was seven statics, and a walk that borrowed them could leave the browser's list half-walked
/// between ticks. The recursive preset walk got away with it by being ATOMIC -- one storage-owner
/// operation, start to finish -- and nothing recorded that its atomicity was load-bearing. The
/// first machine to span ticks over the same buffer emptied the list under an open browser and a
/// delete's completion callback indexed off the end of it.
///
/// ## The invariants
///
/// @ref numDeletedAtStart, @ref numDeletedAtEnd, @ref firstRemaining and @ref lastRemaining are
/// invariants OF the items, not commentary on them: @ref cull is what sets the counters, @ref sort
/// is what maintains the bracket names. They are private for that reason -- a caller that mutated
/// the items and left them stale is the defect this type exists to prevent. Outside the mutators
/// below, nothing adds, removes or reorders items.
///
/// @note Copyable and movable on purpose: `FileItem` owns nothing, so the browser's
///       save-and-restore of a listing it might replace is a plain assignment.
class FileListing {
public:
	/// @name Observers
	/// @{
	/// The folder these items came from. Set only by the read that filled the listing: there is no
	/// setter, so the items and the directory they came from cannot disagree.
	/// @note Returns `const std::string&`, not a `std::string_view`: callers hand it to C APIs as a
	///       `.c_str()` (`AudioLoadContext::forSong`) and concatenate it, and a view carries no
	///       terminator guarantee. Same reasoning as @ref search's parameter.
	[[nodiscard]] const std::string& dir() const noexcept { return dir_; }
	[[nodiscard]] std::span<const FileItem> items() const noexcept { return items_; }
	[[nodiscard]] bool empty() const noexcept { return items_.empty(); }
	[[nodiscard]] int32_t size() const noexcept { return static_cast<int32_t>(items_.size()); }
	/// How many entries the cull dropped from each end of this read.
	[[nodiscard]] int32_t numDeletedAtStart() const noexcept { return numDeletedAtStart_; }
	[[nodiscard]] int32_t numDeletedAtEnd() const noexcept { return numDeletedAtEnd_; }
	/// Names of the entries that bracket the culled window, remembered ACROSS the cull that erases
	/// them. Owning copies deliberately: these were once aliases into a `FileItem::filename` that
	/// this very code then erased.
	[[nodiscard]] std::string_view firstRemaining() const noexcept { return firstRemaining_; }
	[[nodiscard]] std::string_view lastRemaining() const noexcept { return lastRemaining_; }
	/// The search name this read started from, empty for a read with none.
	[[nodiscard]] std::string_view searchName() const noexcept { return searchName_; }

	/// @brief Binary search by displayName, using strcmpspecial.
	///
	/// @warning Reads the collation globals that @ref sort sets. A search on a listing that has not
	///          been sorted since the collation changed answers by the PREVIOUS rules. That coupling
	///          is inherited, not introduced.
	/// @note `name` stays a `char const*`: the comparator hands it straight to
	///       `strcmpspecial(char const*, char const*)`, so a `std::string_view` -- carrying no
	///       terminator guarantee -- would be undefined behaviour for a caller that built one from a
	///       substring, and a `const std::string&` would make the callers that already hold a
	///       `char const*` allocate inside a folder walk.
	[[nodiscard]] SearchHit search(char const* name) const;
	/// @}

	/// @brief Mutable access to ONE item.
	///
	/// For the callers that bind an Instrument into the entry they landed on. Deliberately per-item
	/// rather than a mutable span: nothing outside this class may add, remove or reorder items,
	/// because that is exactly what invalidates the counters and the bracket names.
	[[nodiscard]] FileItem& item(int32_t index) { return items_[index]; }

	/// @name Mutators -- each maintains the invariants above
	/// @{
	/// @brief Start a read: drop what was here and take the new read's parameters.
	///
	/// @param dir The folder about to be read; becomes this listing's provenance.
	/// @param searchName Where the read starts, empty for none.
	/// @param direction Which side of @p searchName the read keeps. Moves WITH @p searchName: a
	///        listing with no search name but a `Both` direction null-derefs in the cull.
	/// @param maxItems The cap this read culls against.
	/// @param collation How names compare for the duration of this read.
	void beginRead(std::string_view dir, std::string_view searchName, CatalogSearch direction, int32_t maxItems,
	               Collation collation);

	void sort();
	/// Trim towards @ref maxItems_, from whichever end the search direction says, recording what was
	/// dropped and which entries now bracket the window.
	void cull();
	/// @brief Make room for one more entry and return it, culling if the cap is reached.
	/// @return The new entry, or null if there was no memory for it even after a cull.
	[[nodiscard]] FileItem* newItem();
	/// Drop folders that share a name with a file, files that share a name with a loaded Instrument,
	/// and Instruments that fail @p requirement.
	void dropFoldersAndDuplicates(Availability requirement = Availability::ANY);
	void eraseAt(int32_t index);
	void eraseRange(int32_t startAt, int32_t stopAt);
	void clear();

	/// @brief Stand a listing up without reading a card: the items, and the folder they are of.
	///
	/// The one legitimate way to build a listing by hand, for harness fixtures. Exists so that doing
	/// so is a named, searchable act rather than a reason to expose the vector.
	///
	/// @warning The directory is NOT optional. A listing whose items have no provenance produces
	///          paths like "/SONG.XML" from every caller that pairs the two, which is a
	///          file-not-found a long way from its cause -- measured, not imagined.
	void adopt(std::string dir, deluge::vector<FileItem> items);
	/// @}

private:
	/// Only the browser's read may fill a listing. Making the fields public instead would be the
	/// global again with extra steps.
	friend class ::Browser;

	std::string dir_;                ///< provenance: the folder these items came from
	deluge::vector<FileItem> items_; ///< sorted by displayName per strcmpspecial (see sort())

	/// @name Read parameters, set by @ref beginRead and consumed by @ref sort and @ref cull
	/// @{
	std::string searchName_;
	/// Paired with @ref searchName_, always. Both are cleared together when a read drops its search
	/// string, because the cull consults the name only under `Both` and hands it to `strcmpspecial`.
	CatalogSearch direction_{CatalogSearch::Right};
	int32_t maxItems_{};
	Collation collation_{};
	/// @}

	std::string firstRemaining_;
	std::string lastRemaining_;
	int32_t numDeletedAtStart_{};
	int32_t numDeletedAtEnd_{};
};

} // namespace deluge::gui::browser
