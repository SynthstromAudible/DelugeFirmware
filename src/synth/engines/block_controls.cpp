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

#include "synth/engines/block_controls.h"
#include "dsp-next/core/phasor.h" // deluge::dsp::Phasor
#include "dsp-next/core/ramp.h"   // deluge::dsp::Ramp
#include <optional>

namespace deluge::synth {

synth::BlockControls make_block_controls(int32_t amplitude, int32_t amplitude_increment, uint32_t pulse_width,
                                         bool apply_amplitude, bool do_osc_sync, uint32_t resetter_phase,
                                         uint32_t resetter_phase_increment, uint32_t retrigger_phase,
                                         int32_t cpu_direness) {
	return synth::BlockControls{
	    .amplitude = dsp::Ramp{amplitude, amplitude_increment},
	    .apply_amplitude = apply_amplitude,
	    .pulse_width = pulse_width,
	    .retrigger_phase = retrigger_phase,
	    .sync = do_osc_sync ? std::optional<dsp::Phasor>{dsp::Phasor{resetter_phase, resetter_phase_increment}}
	                        : std::nullopt,
	    .cpu_direness = cpu_direness,
	};
}

} // namespace deluge::synth
