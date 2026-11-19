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

/// @file
/// @brief The song's reverb against the presets (effects/reverb_presets.h) a gold-knob push cycles.

#include <array>
#include <cstddef>
#include <cstdint>

class Song;
namespace deluge::project {
class Song;
}

namespace deluge::model {

/// @brief The preset nearest @p song's reverb.
/// @return The preset's index.
[[nodiscard]] size_t reverbPresetOf(const project::Song& song);

/// @brief Set @p song's reverb to the preset after its nearest, as two song settings.
void cycleReverbPreset(::Song& song);

} // namespace deluge::model
