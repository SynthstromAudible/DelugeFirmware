/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "effects/state.h"

namespace deluge::modulation {

/// @brief The settings an owner's sidechain starts with.
///
/// Attack and release are the menus' defaults; the sync level is an eighth note at the tick magnitude of the song
/// being loaded (or the current one).
/// @return The default sidechain settings.
[[nodiscard]] effects::SidechainState default_sidechain_state();

/// @brief A sidechain's volume from a preset number: 50 is 2^30, less the truncation of its step.
/// @param number The preset number, −50..50.
/// @return The volume word.
[[nodiscard]] constexpr int32_t sidechain_volume_from_preset(int32_t number) {
	return number * 21474836;
}

} // namespace deluge::modulation
