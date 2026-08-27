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

/// @file
/// @brief The C++-side helper shared by libdeluge/audio_load.h's implementation and its C++ callers.
///
/// Not part of the C ABI: audio_load.h is the contract everything crossing the boundary keeps. This
/// header exists only so the one numeric `Error` mapping that boundary defines has a single
/// definition, rather than a second copy in whichever translation unit collects a result.
#pragma once

#include "definitions_cxx.hpp" // Error
#include <cstdint>

namespace deluge::storage {

/// @brief Map a Deluge `Error` code as it crosses libdeluge/audio_load.h (a `uint8_t`) back to
///        `Error`, by number.
///
/// Never `static_cast<Error>(byte)`: `enum class Error` has no fixed underlying type, so casting an
/// out-of-range byte into it is undefined behaviour. The numeric identity this depends on is pinned
/// by static_asserts beside the definition (audio_load_abi.cpp), so an `Error` enumerator inserted
/// upstream breaks the build rather than silently shifting every later code.
///
/// @param v A byte received through one of audio_load.h's `error_out` / `error` fields.
/// @return The matching `Error`; `Error::UNSPECIFIED` for a byte that is not one of `Error`'s values
///         (a fail-safe degrade, matching the direction the ABI's other byte mappings take).
Error errorFromAbiByte(uint8_t v) noexcept;

} // namespace deluge::storage
