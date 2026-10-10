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

#include "definitions_cxx.hpp" // SSI_TX_BUFFER_NUM_SAMPLES
#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h"
#include "dsp-next/fixed/filter/filter_set.h"
#include "dsp/stereo_sample.h"

#include <cstddef>
#include <cstdint>
#include <span>

/// @file
/// @brief The per-block scratch the render hands its dsp-next components.
///
/// A render runs on whichever tier is rendering: the audio tier in real time, or the routine that renders an offline
/// stem export, which the audio tier never renders alongside (it stops rendering while an export runs offline). Each
/// scratch is held only inside one component's render, which calls no other holder of the same scratch, and the
/// renders run one at a time, so one backing per scratch serves every component of its kind.

namespace deluge::render {

/// @brief The rows of the spare rendering buffer, each SSI_TX_BUFFER_NUM_SAMPLES int32s: the voices' working rows.
inline constexpr std::size_t kSpareRenderingRows = 4;

/// @brief The scratch every filter set shares (the voices' and the host chains'): room for a stereo block of
/// SSI_TX_BUFFER_NUM_SAMPLES frames each for the fades and for the parallel route's copy.
[[nodiscard]] dsp::fixed::FilterScratch filter_scratch();

/// @brief The scratch every compressor shares (the sounds', the clip chains' and the song master's): a stereo block of
/// SSI_TX_BUFFER_NUM_SAMPLES frames.
[[nodiscard]] dsp::fixed::CompressorScratch compressor_scratch();

} // namespace deluge::render
