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

#include "model/voice/source_component_render.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/core/ramp.h"
#include "dsp-next/core/types.h"
#include "dsp/oscillators/oscillator.h"      // deluge::dsp::oscSyncRenderingBuffer
#include "processing/engines/audio_engine.h" // AudioEngine::cpuDireness
#include <optional>

using namespace deluge;

namespace deluge::voice {

synth::BlockControls makeBlockControls(int32_t amplitude, int32_t amplitudeIncrement, uint32_t pulseWidth,
                                       bool applyAmplitude, bool doOscSync, uint32_t resetterPhase,
                                       uint32_t resetterPhaseIncrement, uint32_t retriggerPhase) {
	return synth::BlockControls{
	    .amplitude = dsp::Ramp{amplitude, amplitudeIncrement},
	    .apply_amplitude = applyAmplitude,
	    .pulse_width = pulseWidth,
	    .retrigger_phase = retriggerPhase,
	    .sync =
	        doOscSync ? std::optional<dsp::Phasor>{dsp::Phasor{resetterPhase, resetterPhaseIncrement}} : std::nullopt,
	    .cpu_direness = AudioEngine::cpuDireness,
	};
}

synth::SourceRenderResult renderVaSource(synth::VirtualAnalogEngine& engine, int32_t amplitude,
                                         int32_t amplitudeIncrement, int32_t* output, int32_t numSamples,
                                         uint32_t phaseIncrement, uint32_t pulseWidth, bool applyAmplitude,
                                         bool doOscSync, uint32_t resetterPhase, uint32_t resetterPhaseIncrement,
                                         uint32_t retriggerPhase) {
	synth::BlockControls controls = makeBlockControls(amplitude, amplitudeIncrement, pulseWidth, applyAmplitude,
	                                                  doOscSync, resetterPhase, resetterPhaseIncrement, retriggerPhase);
	return engine.renderSource(controls, phaseIncrement, dsp::fixed::Buffer{output, static_cast<size_t>(numSamples)},
	                           dsp::fixed::Buffer{dsp::oscSyncRenderingBuffer, static_cast<size_t>(numSamples)});
}

void renderWavetableSource(dsp::WavetableEngine& engine, WaveTable* waveTable, int32_t sourceWaveIndexLastTime,
                           int32_t waveIndexIncrement, int32_t amplitude, int32_t amplitudeIncrement, int32_t* output,
                           int32_t numSamples, uint32_t phaseIncrement, uint32_t pulseWidth, bool applyAmplitude,
                           bool doOscSync, uint32_t resetterPhase, uint32_t resetterPhaseIncrement,
                           uint32_t retriggerPhase) {
	synth::BlockControls controls = makeBlockControls(amplitude, amplitudeIncrement, pulseWidth, applyAmplitude,
	                                                  doOscSync, resetterPhase, resetterPhaseIncrement, retriggerPhase);
	engine.setWaveTable(waveTable);
	engine.setIncrement(phaseIncrement);
	engine.setWaveIndex(sourceWaveIndexLastTime, waveIndexIncrement);
	engine.renderBlock(controls, dsp::fixed::Buffer{output, static_cast<size_t>(numSamples)},
	                   dsp::fixed::Buffer{dsp::oscSyncRenderingBuffer, static_cast<size_t>(numSamples)});
}

} // namespace deluge::voice
