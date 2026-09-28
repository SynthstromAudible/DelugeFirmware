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

#include "gui/ui/load/confirm_step.h"

namespace deluge::gui {

ConfirmDecision decideConfirm(const ConfirmWindow& window, bool grabbingLeftmost) {
	// Shared by the empty-read and nothing-available cases.
	const auto fromStartOrAnywhere = [&]() -> ConfirmDecision {
		if (grabbingLeftmost) {
			return ConfirmAnyPreset{};
		}
		if (window.deletedAtStart > 0) {
			return ConfirmFromStart{};
		}
		return ConfirmAnyPreset{};
	};

	if (window.readNames.empty()) {
		return fromStartOrAnywhere();
	}
	if (window.shownCount == 0) {
		if (window.deletedAtEnd > 0) {
			// From the entry before the popped one, so the next read includes the popped one. An end-cull keeps ten
			// entries, so there is always one before it.
			const size_t size = window.readNames.size();
			const std::string_view from =
			    (window.poppedLast && size >= 2) ? window.readNames[size - 2] : window.readNames[size - 1];
			return ConfirmPageOn{.from = std::string{from}};
		}
		return fromStartOrAnywhere();
	}
	return ConfirmLand{};
}

} // namespace deluge::gui
