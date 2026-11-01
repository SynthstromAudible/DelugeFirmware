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

#include "dsp-next/fixed/noise/cong.h"

#include <cstdint>

namespace deluge {
/// @brief The synth's one noise stream. Oscillator phases, noise sources, random modulation, the LFO's random waves,
/// the filters', mod FX's and grain's noise and DX7 detunes all draw from it, in call order: reordering or adding a
/// draw changes every later one. Render call sites pass it to the components that draw.
extern dsp::fixed::Cong synth_noise;
} // namespace deluge

/// @brief Reseeds @ref deluge::synth_noise: the host harness restarts it at each script's first note.
extern "C" void deluge_seed_noise(uint32_t seed);
