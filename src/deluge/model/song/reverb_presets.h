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
/// @brief The song reverb's presets, which a gold-knob push cycles: their room sizes and dampings, in the song menu's
///        units, and their names.

#include <array>
#include <cstddef>
#include <cstdint>

class Song;
namespace deluge::project {
class Song;
}

namespace deluge::model {

/// The number of reverb presets.
inline constexpr size_t kNumReverbPresets = 3;
/// Each preset's room size, in the song menu's units.
inline constexpr std::array<int32_t, kNumReverbPresets> kReverbPresetRoomSize{16, 30, 44};
/// Each preset's damping, in the song menu's units.
inline constexpr std::array<int32_t, kNumReverbPresets> kReverbPresetDamping{29, 36, 45};

/// @brief The preset nearest @p song's reverb.
/// @return The preset's index.
[[nodiscard]] size_t reverbPresetOf(const project::Song& song);

/// @brief Preset @p preset's name, as the knobs' popups show it.
[[nodiscard]] const char* reverbPresetName(size_t preset);

/// @brief Set @p song's reverb to the preset after its nearest, as two song settings.
void cycleReverbPreset(::Song& song);

} // namespace deluge::model
