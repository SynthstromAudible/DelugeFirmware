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
#include "dsp-next/core/phasor.h"            // deluge::dsp::Phasor
#include "dsp-next/core/ramp.h"              // deluge::dsp::Ramp
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

} // namespace deluge::voice
