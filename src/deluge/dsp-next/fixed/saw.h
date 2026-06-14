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

#include "dsp-next/fixed/band_limited_table.h"

namespace deluge::dsp::fixed {

/// @brief The band-limited sawtooth wave kernel: its anti-aliased bands (the harmonic-limited
/// saw tables, one per pitch range above the crude region), owning their own LUTs and exposing
/// only the band set. Pair with a @ref BandLimitedTableOscillator.
[[nodiscard]] const BandlimitedTable& saw_bandlimited_table();

} // namespace deluge::dsp::fixed
