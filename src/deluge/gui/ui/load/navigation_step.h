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

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace deluge::gui {

/// One entry of a navigation window, reduced to the two facts the walk decides on.
struct NavigationItem {
	bool isOld{};  ///< the instrument being navigated away from
	bool inSong{}; ///< a preset already loaded in the song (FileItem::instrument && instrumentAlreadyInSong)
	/// Its display name, extension included: what a page on from this entry reads from.
	std::string_view name;
};

/// What one read of the folder produced, after song instruments were added, sorted and filtered.
struct NavigationWindow {
	std::span<const NavigationItem> items;
	int32_t deletedAtStart{};
	int32_t deletedAtEnd{};
};

struct Land {
	int32_t index{};
	int32_t wraps{};
};
/// Read on past this window's trimmed edge, from the name of the last entry it shows on that side.
struct PageOn {
	std::string from;
	int32_t wraps{};
};
/// Read again from the folder's far end, with no search name.
struct FromEnd {
	int32_t wraps{};
};
/// Nothing to navigate to: NO_ERROR_BUT_GET_OUT.
struct GetOut {};

using NavigationDecision = std::variant<Land, PageOn, FromEnd, GetOut>;

/// @brief What a preset navigate does with one window: land, read on, read from the far end, or give up.
///
/// Stepping off an edge trimmed on that side pages on from the edge entry the window shows, rather than re-reading
/// from the old name (which would just re-read the same window forever). The wrap cap counts across the whole walk
/// via @p wrapsSoFar, not per window.
/// @param offset The turn: the sign is the direction, the magnitude the step.
/// @param skipInSong Step past presets already in the song (Availability::INSTRUMENT_UNUSED).
/// @param haveName Whether this window was read from a search name.
/// @param wrapsSoFar Wraps the walk spent on earlier windows.
[[nodiscard]] NavigationDecision decideNavigation(const NavigationWindow& window, int32_t offset, bool skipInSong,
                                                  bool haveName, int32_t wrapsSoFar);

/// Read again from the walk's search name, the position reset to the window's near end (main's `readAgain`).
struct ReadAgain {};

using PileDecision = std::variant<Land, ReadAgain, FromEnd, GetOut>;

/// @brief Main's `moveAgain` loop in `LoadInstrumentPresetUI::doPresetNavigation`, over one window: the detents that
///        piled up while a preset loaded, moved by all at once from where it landed.
///
/// Past a trimmed edge on the side it moves to, it reads again from the walk's name (`ReadAgain`), which starts over
/// at that window's near end and drops what is left of the pile. Past the folder's end it wraps: to the window's far
/// end when the window holds the whole folder, else to a read from the folder's far end (`FromEnd`), which drops the
/// rest of the pile too. A preset in the song, when those are skipped, moves it on by the whole pile again, until it
/// has wrapped twice.
/// @param window The window the position is in.
/// @param i The position: the landed entry's index plus the pile, or a near end after a read.
/// @param offset The pile, signed.
/// @param skipInSong Step past presets already in the song (Availability::INSTRUMENT_UNUSED).
/// @param wrapped The wraps already made in this window (main's `wrapped`, which a pending move keeps and a read
///                resets): what the landing being moved from had made.
[[nodiscard]] PileDecision movePile(const NavigationWindow& window, int32_t i, int32_t offset, bool skipInSong,
                                    int32_t wrapped);

/// @brief Main's `doPresetNavigation` after a read that a pile's move asked for: an empty window, or one holding only
///        the instrument moved on from, reads from the folder's far end, or gets out when it was already that read;
///        anything else moves from the window's near end (@ref movePile).
/// @param haveName Whether this window was read from a search name.
[[nodiscard]] PileDecision startPileWindow(const NavigationWindow& window, int32_t offset, bool skipInSong,
                                           bool haveName);

} // namespace deluge::gui
