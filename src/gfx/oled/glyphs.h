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
/// @brief Code points the OLED fonts give to symbols outside ASCII.
namespace deluge::gfx::oled {

/// @brief The font's code point for the flat sign (♭). A byte, held as a `char` and compared only as one: its
///        numeric value differs between signed-char hosts and the device, so cast to `uint8_t` before an integer use.
inline constexpr char kFlatGlyph{'\x81'};

} // namespace deluge::gfx::oled
