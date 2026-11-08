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

#include "dsp-next/core/pack.h"
#include "dsp-next/core/pack_f32.h"

/// @file
/// @brief The phase step the float oscillators share.
namespace deluge::dsp::fp {

/// @brief One phase step: advance by @p inc and wrap into [0, 1).
/// @tparam V f32 for one copy, Pack<f32, N> for one per lane.
/// @details @p inc is below 1, so one subtraction wraps.
template <class V>
[[gnu::always_inline]] inline void step_phase(V& phase, V inc) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
	phase = phase + inc;
	phase = select(phase >= V{1.f}, phase - V{1.f}, phase);
}

} // namespace deluge::dsp::fp
