/*
 * Copyright © 2020-2026 Synthstrom Audible Limited
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

#include "dsp-next/core/generator.h" // Buffer
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/wave_table_data.h"
#include <cstdint>

namespace deluge::dsp::fixed {

/// Duplicate samples appended to the end of each stored cycle so the windowed-sinc interpolation can
/// read its right-hand neighbours without per-sample wrap logic (the legacy
/// WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE).
inline constexpr int32_t kWavetableDuplicateSamplesAtCycleEnd = 7;

/// @brief Render one chunk of a multi-cycle wavetable into @p output.
///
/// Per sample: advance @p phasor, windowed-sinc-interpolate it (via @p kernel) out of the two
/// adjacent cycles @p firstCycleNumber and +1 of @p band, then linearly cross-blend those two by
/// @p crossCycleBlend (the wave-index morph), which advances by @p crossCycleBlendIncrement each
/// sample. Relocated from the legacy `WaveTable::doRenderingLoop` (the NEON body is verbatim;
/// argon-ifying it is the W-f pass).
void render_wavetable_cycle(Buffer output, int32_t firstCycleNumber, const WaveTableBand& band, Phasor phasor,
                            uint32_t crossCycleBlend, int32_t crossCycleBlendIncrement,
                            const int16_t* __restrict__ kernel);

/// @brief Render one chunk of a single-cycle wavetable into @p output.
///
/// Per sample: advance @p phasor and windowed-sinc-interpolate it (via @p kernel) out of @p band's
/// single cycle. Relocated from the legacy `WaveTable::doRenderingLoopSingleCycle`.
void render_wavetable_single_cycle(Buffer output, const WaveTableBand& band, Phasor phasor,
                                   const int16_t* __restrict__ kernel);

} // namespace deluge::dsp::fixed
