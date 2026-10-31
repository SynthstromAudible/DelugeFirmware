/*
 * Copyright © 2014-2023 Synthstrom Audible Limited, 2026 Synthstrom Audible Limited
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

#include "synth/sources/source.h" // deluge::synth::BlockControls
#include <cstdint>

/// @file Bundles a source's already-modulated per-block values into a @ref deluge::synth::BlockControls, for the
/// voice's per-source render (in a @ref deluge::synth::SourceBlock) and for @ref RingModEngine.
namespace deluge::synth {

/// Build the already-modulated per-block controls a source component renders from; the component derives its pulse,
/// sync, retrigger, band and scale from them.
synth::BlockControls make_block_controls(int32_t amplitude, int32_t amplitude_increment, uint32_t pulse_width,
                                         bool apply_amplitude, bool do_osc_sync, uint32_t resetter_phase,
                                         uint32_t resetter_phase_increment, uint32_t retrigger_phase,
                                         int32_t cpu_direness);

} // namespace deluge::synth
