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

namespace deluge::gui {

/// @brief Whether a load that can replace a clip's output, or a sound's sample, is in flight: an instrument, pattern,
///        song, drum-randomizer, sample-claim, folder-import or reversion load. A recording does not start then: what
///        it would attach to could be swapped out from under it.
[[nodiscard]] bool anOutputMayBeReplaced();

} // namespace deluge::gui
