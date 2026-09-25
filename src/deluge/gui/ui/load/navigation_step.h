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
};

/// What one read of the folder produced, after song instruments were added, sorted and filtered.
struct NavigationWindow {
	std::span<const NavigationItem> items;
	int32_t deletedAtStart{};
	int32_t deletedAtEnd{};
	std::string_view firstRemaining; ///< FileListing::firstRemaining(): the first entry a start-cull kept
	std::string_view lastRemaining;  ///< FileListing::lastRemaining(): the last entry an end-cull kept
};

struct Land {
	int32_t index{};
	int32_t wraps{};
};
/// Read on past this window's trimmed edge, from that edge's name.
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
/// The gotos of the old LoadInstrumentPresetUI::doPresetNavigation, per window, with two changes: stepping off an
/// edge trimmed on that side pages on from the edge instead of re-reading from the old name (which re-read the same
/// window for ever), and the wrap cap counts across the walk (@p wrapsSoFar) rather than per window.
/// @param offset The turn: the sign is the direction, the magnitude the step.
/// @param skipInSong Step past presets already in the song (Availability::INSTRUMENT_UNUSED).
/// @param haveName Whether this window was read from a search name.
/// @param wrapsSoFar Wraps the walk spent on earlier windows.
[[nodiscard]] NavigationDecision decideNavigation(const NavigationWindow& window, int32_t offset, bool skipInSong,
                                                  bool haveName, int32_t wrapsSoFar);

} // namespace deluge::gui
