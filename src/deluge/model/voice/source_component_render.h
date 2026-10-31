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

/// @file The adapter that bundles a source's already-modulated per-block values into a synth-safe
/// @ref deluge::synth::BlockControls. Used by `Voice::renderSourceComponent` (to fill a @ref
/// deluge::synth::SourceRenderContext) and by
/// @ref RingModEngine. (The per-source render itself now lives on the engines as `synth::Source::renderSource` /
/// the engines' `renderInto`.)
namespace deluge::voice {

/// Build the already-modulated per-block controls a source component renders from (the raw values renderOsc's VA
/// branch used to build; the component derives pulse/sync/retrigger/band/scale internally).
synth::BlockControls make_block_controls(int32_t amplitude, int32_t amplitude_increment, uint32_t pulse_width,
                                         bool apply_amplitude, bool do_osc_sync, uint32_t resetter_phase,
                                         uint32_t resetter_phase_increment, uint32_t retrigger_phase,
                                         int32_t cpu_direness);

} // namespace deluge::voice
