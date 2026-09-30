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

/// libdeluge/abi.h — what every libdeluge header shares.
#ifndef LIBDELUGE_ABI_H
#define LIBDELUGE_ABI_H

/// Marks a declaration `noexcept` when the header is read as C++, and nothing when it is read as C. Every function
/// declaration in these headers carries it. No function behind this ABI may throw or unwind: the Rust side aborts on
/// panic, and a C++ definition catches at the boundary and returns its failure value.
#ifdef __cplusplus
#define DELUGE_NOEXCEPT noexcept
#else
#define DELUGE_NOEXCEPT
#endif

#endif // LIBDELUGE_ABI_H
