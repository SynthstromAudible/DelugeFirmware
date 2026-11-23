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

#include "dsp/core/fixed_point.h"
#include <cstdint>

/// @file
/// @brief What the host's engine tells each sound's render about one block.

namespace deluge::synth {

/// @brief The engine's facts for one block, the same for every sound it renders. The render's shared scratch is not
/// here: it is the render's own (VoiceRenderArgs), and its types need the DSP build.
struct HostBlock {
	/// How far behind the engine's render is running, 0 when on time: the render takes cheaper paths as it grows.
	int32_t cpu_direness{};
	bool engine_in_stereo{};                    ///< The engine renders in stereo (headphones), so voices may pan
	dsp::Q<24> pitch_adjust{dsp::Q<24>::one()}; ///< The sound's pitch adjust, from its output
};

} // namespace deluge::synth
