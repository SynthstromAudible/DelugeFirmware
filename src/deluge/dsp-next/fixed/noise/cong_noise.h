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

#include "dsp-next/core/types.h"
#include "util/waves.h" // CONG, the synth-wide generator

/// @file
/// @brief CongNoise: the synth's shared noise stream as a kernel's noise policy.
namespace deluge::dsp::fixed {

/// @brief A noise policy drawing from the synth-wide generator (util/waves.h's `CONG`, the single `jcong` state every
/// voice shares).
/// @details A kernel templated on its noise calls `Noise::render()` once per draw. Every draw advances the one shared
/// state, so the order of draws across voices and channels is part of the output: a kernel under `Stereo` draws for
/// the left channel before the right in every frame.
struct CongNoise {
	/// @brief Advances the shared state exactly as `CONG` does.
	/// @return The new state, reinterpreted as a q31 sample.
	[[nodiscard]] [[gnu::always_inline]] static q31 render() { return static_cast<q31>(CONG); }
};

} // namespace deluge::dsp::fixed
