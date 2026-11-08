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

#include "dsp-next/core/sample.h"

#include <cstddef>

/// @file
/// @brief The band-limited saw wavetable set the float table oscillator reads.
namespace deluge::dsp::fp {

/// @brief Samples in one cycle of a band, before its repeated first sample.
inline constexpr std::size_t kSawBandSize = 2048;
/// @brief Bands in the set: band b holds harmonics 1 … 2^(10 − b), so band 0 is the richest and band 10 a sine.
inline constexpr std::size_t kSawBands = 11;

/// @brief The saw bands, built on first call.
/// @return @ref kSawBands bands of `kSawBandSize + 1` samples each, one after another. Band b's sample i is
/// `−(2/π) Σ sin(2π h i / kSawBandSize) / h` over h = 1 … 2^(10 − b), the band-limited form of the naive saw `2x − 1`;
/// its last sample repeats its first so a linear interpolation at the top of the cycle reads inside the band.
[[nodiscard]] const f32* saw_bands();

/// @brief The richest band that cannot alias at phase increment @p inc.
/// @param inc The phase increment, f0/fs.
/// @return The smallest b in [0, 10] whose top harmonic h = 2^(10 − b) has `h · inc < 0.5`, or 10 when none does.
[[nodiscard]] std::size_t saw_band_for(f32 inc);

} // namespace deluge::dsp::fp
