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

#include "gui/ui/load/navigation_step.h"

namespace deluge::gui {

NavigationDecision decideNavigation(const NavigationWindow& window, int32_t offset, bool skipInSong, bool haveName,
                                    int32_t wrapsSoFar) {
	const auto size = static_cast<int32_t>(window.items.size());
	// reachedEnd: nothing on this side of the old name, or only the old instrument itself.
	if (size == 0 || (size == 1 && window.items[0].isOld)) {
		if (haveName) {
			return FromEnd{.wraps = 1}; // the old code's "clear the name and read again": a wrap, counted as one
		}
		return GetOut{};
	}
	int32_t wraps = 0;
	int32_t i = (offset >= 0) ? 0 : size - 1; // the read already put the old name behind the window
	for (;;) {
		if (i < 0) {
			if (window.deletedAtStart > 0) {
				return PageOn{.from = std::string{window.firstRemaining}, .wraps = wraps};
			}
			wraps++;
			if (window.deletedAtEnd > 0) {
				return FromEnd{.wraps = wraps};
			}
			i = size - 1;
		}
		else if (i >= size) {
			if (window.deletedAtEnd > 0) {
				return PageOn{.from = std::string{window.lastRemaining}, .wraps = wraps};
			}
			wraps++;
			if (window.deletedAtStart > 0) {
				return FromEnd{.wraps = wraps};
			}
			i = 0;
		}
		if (skipInSong && window.items[i].inSong && wrapsSoFar + wraps < 2) {
			i += offset;
			continue;
		}
		return Land{.index = i, .wraps = wraps};
	}
}

} // namespace deluge::gui
