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

#include "synth/matrix/program.h"
#include "synth/modulation/patch_routing.h"

#include <cstddef>
#include <span>

namespace deluge::synth::matrix {

/// The bytes @ref compile_program needs for @p routing.
[[nodiscard]] std::size_t program_size(const RoutingBuild& routing);

/// Compiles @p routing (today's cable order: `compile_routing`'s) into a program laid out in @p storage, which must
/// hold @ref program_size bytes, aligned for MatrixProgram, and outlive the program.
const MatrixProgram* compile_program(const RoutingBuild& routing, std::span<std::byte> storage);

/// Copies @p from into @p storage (at least `from.bytes`, aligned for MatrixProgram), its tables pointing into the
/// copy.
const MatrixProgram* copy_program(const MatrixProgram& from, std::span<std::byte> storage);

} // namespace deluge::synth::matrix
