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

/// What one read of a change-type's folder produced.
struct ConfirmWindow {
	std::span<const std::string_view> readNames; ///< the read's entries, in order, BEFORE availability filtering
	bool poppedLast{};                           ///< dropFoldersAndDuplicates popped the last of them (an end-cull)
	int32_t shownCount{};                        ///< entries left after dropFoldersAndDuplicates(availability)
	int32_t deletedAtStart{};
	int32_t deletedAtEnd{};
};

/// Land on the first entry the window shows.
struct ConfirmLand {};
/// Read on past the trimmed end, from this name.
struct ConfirmPageOn {
	std::string from;
};
/// Clear the name, grab the leftmost, read again.
struct ConfirmFromStart {};
/// Nothing here: the whole-type Unlaunched search.
struct ConfirmAnyPreset {};

using ConfirmDecision = std::variant<ConfirmLand, ConfirmPageOn, ConfirmFromStart, ConfirmAnyPreset>;

/// @brief What a change-type confirm does with one window.
///
/// A page goes on from the entry BEFORE the one dropFoldersAndDuplicates popped, so the next read -- which excludes
/// its own search name -- includes it.
/// @param grabbingLeftmost Whether this window was read from the start after a FromStart.
[[nodiscard]] ConfirmDecision decideConfirm(const ConfirmWindow& window, bool grabbingLeftmost);

} // namespace deluge::gui
