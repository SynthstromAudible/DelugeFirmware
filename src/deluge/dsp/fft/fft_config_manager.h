/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#include "dsp-next/fixed/fft/real_fft.h"

#include <cstdint>

namespace FFTConfigManager {

/// @brief The transform of 2^@p magnitude points, built on first use and kept for the rest of the run.
/// @return nullptr outside [fft::kMinMagnitude, fft::kMaxMagnitude], or when its block cannot be allocated. Building
///         one yields to the cluster loader between the twiddle stages.
const deluge::dsp::fixed::fft::RealFft* getConfig(int32_t magnitude);

} // namespace FFTConfigManager
