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

namespace deluge::gui {

/// @brief How a load's cancel answered: InstrumentLoad::cancel() and LoadSongUI::cancelLoad().
enum class CancelResult : uint8_t {
	Cancelled, ///< the load was dropped: nothing it staged or parsed is kept
	Refused,   ///< the load always completes from where it is: this kind of load, or a load past where it can be undone
	Stale,     ///< the load named has finished or was replaced, or no load is in flight
};

} // namespace deluge::gui
