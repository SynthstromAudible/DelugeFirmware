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

#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h"

namespace deluge::render {

/// @brief The scratch every compressor on the audio tier shares (the sounds', the clip chains' and the song master's):
/// a stereo block of SSI_TX_BUFFER_NUM_SAMPLES frames. A compressor holds it only inside its own render, which calls
/// no other compressor, and they render one at a time on that tier, so one backing serves them all.
[[nodiscard]] dsp::fixed::CompressorScratch compressor_scratch();

} // namespace deluge::render
