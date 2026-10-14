/*
 * Copyright © 2024 Mark Adams and Alter-Alter
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

namespace deluge::dsp::test {

/// @brief The granular ring's length in frames, as the reference and the workloads size it: 65,536, a power of two.
constexpr int32_t kModFXGrainBufferSize = 65536;
/// @brief The mask that wraps a ring index; the ring's length minus one.
constexpr int32_t kModFXGrainBufferIndexMask = (kModFXGrainBufferSize - 1);

} // namespace deluge::dsp::test
