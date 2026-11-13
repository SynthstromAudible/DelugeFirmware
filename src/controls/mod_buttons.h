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
/// @brief The Deluge's mod buttons the controls' rules name: the section a page belongs to is its mod button.

#include <cstdint>

namespace deluge::controls::mod_button {

/// @brief The filter button: a filter knob's press cycles low-pass, high-pass and EQ.
inline constexpr uint8_t kFilter = 1;
/// @brief The reverb button: its upper knob is a clip's sidechain, or the song's compressor.
inline constexpr uint8_t kCompressor = 4;
/// @brief The mod FX button: a chain's pages are the views its mod FX type has.
inline constexpr uint8_t kModFx = 5;
/// @brief The stutter button: on the song its lower knob moves nothing.
inline constexpr uint8_t kStutter = 6;
/// @brief The last mod button: a mode past it is this one.
inline constexpr uint8_t kLast = 7;

} // namespace deluge::controls::mod_button
