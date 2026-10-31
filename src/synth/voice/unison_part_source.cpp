/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "synth/voice/unison_part_source.h"
#include "dsp-next/core/phasor.h"
#include "synth/sources/va/va_shapes.h" // va_shape::is_virtual_analog / wave_for / initial_phase_for_zero

namespace deluge::synth {

VoiceUnisonPartSource::~VoiceUnisonPartSource() {
	unassign(false);
}

void VoiceUnisonPartSource::bind(deluge::synth::SamplePlaybackPort* sample, deluge::synth::DxVoicePort* dx,
                                 deluge::synth::AudioInputPort* input) {
	sample_source.bind_port(sample);
	dx7_source.bind_port(dx);
	input_source.bind_port(input);
}

bool VoiceUnisonPartSource::note_on(const deluge::synth::SourceSetup& source, uint32_t samples_late,
                                    uint32_t osc_retrigger_phase, bool reset_everything, SynthMode synth_mode,
                                    int32_t note_code, uint8_t velocity, int32_t priority_rating,
                                    deluge::dsp::fixed::Cong& noise) {

	if (synth_mode != SynthMode::FM && source.osc_type == OscType::SAMPLE) {
		return sample_source.port()->note_on(samples_late, priority_rating);
	}

	if (synth_mode != SynthMode::FM
	    && (source.osc_type == OscType::INPUT_L || source.osc_type == OscType::INPUT_R
	        || source.osc_type == OscType::INPUT_STEREO)) {
		input_source.prepare_for_note_on(source.osc_type); // latch the live-input channel
	}
	else if (synth_mode != SynthMode::FM && source.osc_type == OscType::DX7) [[unlikely]] {
		if (!dx7_source.note_on(source.dx_patch, note_code, velocity)) { // opaque patch handle → the port
			return false;
		}
	}
	else {
		const OscType osc_type = source.osc_type;
		// Virtual-analog, wavetable and supersaw waves (subtractive and ring-mod) hold their phase in a persistent
		// engine rather than osc_pos. A retrigger seeds it at the wave's zero crossing plus the retrigger phase; a
		// free-running oscillator on a new voice takes the random phase randomize_osc_phases left in osc_pos; a
		// free-running oscillator on a re-triggered voice keeps the engine's continuing phase. The engines are
		// mutually exclusive, so the others are reset: the sound's type may have changed since the voice's last note.
		const bool engine_mode = (synth_mode == SynthMode::SUBTRACTIVE || synth_mode == SynthMode::RINGMOD);
		const uint32_t seed_phase = deluge::synth::va_shape::initial_phase_for_zero(osc_type) + osc_retrigger_phase;
		if (engine_mode && deluge::synth::va_shape::is_virtual_analog(osc_type)) {
			wavetable_source.reset();
			supersaw_source.reset();
			const deluge::synth::va_shape::Wave wave = deluge::synth::va_shape::wave_for(osc_type);
			if (osc_retrigger_phase != deluge::synth::kNoRetrigger) {
				va_source.emplace(wave, deluge::dsp::Phasor{seed_phase, 0});
			}
			else if (reset_everything || !va_source.has_value()) {
				va_source.emplace(wave, deluge::dsp::Phasor{osc_pos, 0});
			}
			// else (free-running, re-triggered voice): keep va_source's phase.
		}
		else if (engine_mode && osc_type == OscType::WAVETABLE) {
			va_source.reset();
			supersaw_source.reset();
			if (osc_retrigger_phase != deluge::synth::kNoRetrigger) {
				wavetable_source.emplace(deluge::dsp::Phasor{seed_phase, 0});
			}
			else if (reset_everything || !wavetable_source.has_value()) {
				wavetable_source.emplace(deluge::dsp::Phasor{osc_pos, 0});
			}
			// else (free-running, re-triggered voice): keep wavetable_source's phase.
		}
		else if (engine_mode && osc_type == OscType::SUPERSAW) {
			va_source.reset();
			wavetable_source.reset();
			using deluge::dsp::fixed::Supersaw;
			if (osc_retrigger_phase != deluge::synth::kNoRetrigger) {
				// The side saws start at fixed offsets from the centre one, so every note starts the same way
				supersaw_source.emplace(seed_phase, Supersaw::side_phases_from(seed_phase));
			}
			else if (reset_everything || !supersaw_source.has_value()) {
				// Free-running on a new voice: random side saws like the centre one, otherwise every note's side saws
				// would start from the same place and sweep the same way
				Supersaw::SidePhases side_phases;
				for (uint32_t& phase : side_phases) {
					phase = noise.next();
				}
				supersaw_source.emplace(osc_pos, side_phases);
			}
			// else (free-running, re-triggered voice): keep supersaw_source's phases.
		}
		else {
			va_source.reset(); // no engine (noise, FM, …) keeps osc_pos; drop any engine left from a prior note.
			wavetable_source.reset();
			supersaw_source.reset();
			if (osc_retrigger_phase != deluge::synth::kNoRetrigger) {
				osc_pos = seed_phase;
			}
		}
	}

	if (reset_everything) {
		carrier_feedback = 0;
	}

	return true;
}

void VoiceUnisonPartSource::unassign(bool deleting_song) {
	active = false;
	sample_source.unassign(deleting_song);
	dx7_source.unassign();
	input_source.unassign();
}

} // namespace deluge::synth
