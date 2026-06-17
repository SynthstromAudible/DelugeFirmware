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

#include "dsp/oscillators/wavetable_engine.h"   // deluge::dsp::WavetableEngine
#include "synth/source/source.h"                // deluge::synth::BlockControls
#include "synth/source/virtual_analog_engine.h" // deluge::synth::VirtualAnalogEngine
#include <cstdint>

class WaveTable;

/// @file Shared "render one block of a source component" helpers — the per-voice replacement for renderOsc on
/// the virtual-analog + wavetable source components. Extracted from voice.cpp so both the subtractive render path
/// (Voice::renderBasicSource) and the cross-source engines (e.g. @ref RingModEngine) drive the components through
/// the same seam. A small first step toward the L1 source-component render contract
/// (`docs/dev/synth_l1_engine_architecture.md`).
namespace deluge::voice {

/// Build the already-modulated per-block controls a source component renders from (the raw values renderOsc's VA
/// branch used to build; the component derives pulse/sync/retrigger/band/scale internally).
synth::BlockControls makeBlockControls(int32_t amplitude, int32_t amplitudeIncrement, uint32_t pulseWidth,
                                       bool applyAmplitude, bool doOscSync, uint32_t resetterPhase,
                                       uint32_t resetterPhaseIncrement, uint32_t retriggerPhase);

/// Render one block of a virtual-analog source component into @p output (the engine owns its phase across blocks,
/// so there is no oscPos to thread).
synth::SourceRenderResult renderVaSource(synth::VirtualAnalogEngine& engine, int32_t amplitude,
                                         int32_t amplitudeIncrement, int32_t* output, int32_t numSamples,
                                         uint32_t phaseIncrement, uint32_t pulseWidth, bool applyAmplitude,
                                         bool doOscSync, uint32_t resetterPhase, uint32_t resetterPhaseIncrement,
                                         uint32_t retriggerPhase);

/// Render one block of the wavetable source component into @p output. The wavetable-specific inputs (table +
/// morph index) are set on the engine before the render.
void renderWavetableSource(dsp::WavetableEngine& engine, WaveTable* waveTable, int32_t sourceWaveIndexLastTime,
                           int32_t waveIndexIncrement, int32_t amplitude, int32_t amplitudeIncrement, int32_t* output,
                           int32_t numSamples, uint32_t phaseIncrement, uint32_t pulseWidth, bool applyAmplitude,
                           bool doOscSync, uint32_t resetterPhase, uint32_t resetterPhaseIncrement,
                           uint32_t retriggerPhase);

} // namespace deluge::voice
