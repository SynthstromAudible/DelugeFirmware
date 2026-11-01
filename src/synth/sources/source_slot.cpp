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

#include "synth/sources/source_slot.h"
#include "dsp-next/core/phasor.h"
#include "synth/sources/va/va_shapes.h" // va_shape::is_virtual_analog / wave_for / initial_phase_for_zero
#include <type_traits>

namespace deluge::synth {

SourceSlot::~SourceSlot() {
	unassign(false);
}

void SourceSlot::bind(const SourcePorts& ports) {
	sample_.bind_port(ports.sample);
	dx7_.bind_port(ports.dx);
	input_.bind_port(ports.input);
}

bool SourceSlot::note_on(const SourceSetup& source, uint32_t samples_late, uint32_t osc_retrigger_phase,
                         bool reset_everything, SynthMode synth_mode, int32_t note_code, uint8_t velocity,
                         int32_t priority_rating, dsp::fixed::Cong& noise) {
	const SourceNote note{
	    .samples_late = samples_late,
	    .note_code = note_code,
	    .velocity = velocity,
	    .priority_rating = priority_rating,
	    .dx_patch = source.dx_patch,
	    .osc_type = source.osc_type,
	};

	if (synth_mode != SynthMode::FM && source.osc_type == OscType::SAMPLE) {
		return sample_.note_on(note);
	}

	if (synth_mode != SynthMode::FM
	    && (source.osc_type == OscType::INPUT_L || source.osc_type == OscType::INPUT_R
	        || source.osc_type == OscType::INPUT_STEREO)) {
		input_.note_on(note); // latches the live-input channel
	}
	else if (synth_mode != SynthMode::FM && source.osc_type == OscType::DX7) [[unlikely]] {
		if (!dx7_.note_on(note)) {
			return false;
		}
	}
	else {
		const bool engine_mode = (synth_mode == SynthMode::SUBTRACTIVE || synth_mode == SynthMode::RINGMOD);
		build_oscillator(source.osc_type, engine_mode, osc_retrigger_phase, reset_everything, noise);
	}

	if (reset_everything) {
		carrier_feedback = 0;
	}

	return true;
}

// Virtual-analog, wavetable and supersaw waves (subtractive and ring-mod) hold their phase in the oscillator rather
// than osc_pos. A retrigger seeds it at the wave's zero crossing plus the retrigger phase; a free-running oscillator
// on a new voice takes the random phase randomize_osc_phases left in osc_pos; a free-running oscillator on a
// re-triggered voice keeps the held oscillator's continuing phase, and its built wave when only the VA wave changed.
// Building one class drops whichever the slot held: the sound's type may have changed since the voice's last note.
void SourceSlot::build_oscillator(OscType osc_type, bool engine_mode, uint32_t osc_retrigger_phase,
                                  bool reset_everything, dsp::fixed::Cong& noise) {
	const uint32_t seed_phase = va_shape::initial_phase_for_zero(osc_type) + osc_retrigger_phase;
	const bool retrigger = osc_retrigger_phase != kNoRetrigger;
	if (engine_mode && va_shape::is_virtual_analog(osc_type)) {
		const va_shape::Wave wave = va_shape::wave_for(osc_type);
		if (retrigger) {
			oscillator_.emplace<VirtualAnalogSource>(wave, dsp::Phasor{seed_phase, 0});
		}
		else if (reset_everything || !std::holds_alternative<VirtualAnalogSource>(oscillator_)) {
			oscillator_.emplace<VirtualAnalogSource>(wave, dsp::Phasor{osc_pos, 0});
		}
	}
	else if (engine_mode && osc_type == OscType::WAVETABLE) {
		if (retrigger) {
			oscillator_.emplace<WavetableSource>(dsp::Phasor{seed_phase, 0});
		}
		else if (reset_everything || !std::holds_alternative<WavetableSource>(oscillator_)) {
			oscillator_.emplace<WavetableSource>(dsp::Phasor{osc_pos, 0});
		}
	}
	else if (engine_mode && osc_type == OscType::SUPERSAW) {
		using dsp::fixed::Supersaw;
		if (retrigger) {
			// The side saws start at fixed offsets from the centre one, so every note starts the same way
			oscillator_.emplace<SupersawSource>(seed_phase, Supersaw::side_phases_from(seed_phase));
		}
		else if (reset_everything || !std::holds_alternative<SupersawSource>(oscillator_)) {
			// Free-running on a new voice: random side saws like the centre one, otherwise every note's side saws
			// would start from the same place and sweep the same way
			Supersaw::SidePhases side_phases;
			for (uint32_t& phase : side_phases) {
				phase = noise.next();
			}
			oscillator_.emplace<SupersawSource>(osc_pos, side_phases);
		}
	}
	else {
		oscillator_.emplace<std::monostate>(); // FM, noise and the rest keep osc_pos
		if (retrigger) {
			osc_pos = seed_phase;
		}
	}
}

void SourceSlot::unassign(bool deleting_song) {
	active = false;
	sample_.unassign(deleting_song);
	dx7_.unassign(deleting_song);
	input_.unassign(deleting_song);
}

Source* SourceSlot::source_for(OscType type) {
	switch (type) {
	case OscType::SAMPLE:
		return &sample_;
	case OscType::DX7:
		return &dx7_;
	case OscType::INPUT_L:
	case OscType::INPUT_R:
	case OscType::INPUT_STEREO:
		return &input_;
	default:
		return std::visit(
		    []<class Held>(Held& held) -> Source* {
			    if constexpr (std::is_same_v<Held, std::monostate>) {
				    return nullptr;
			    }
			    else {
				    return &held;
			    }
		    },
		    oscillator_);
	}
}

uint32_t SourceSlot::current_osc_phase() const {
	return std::visit(
	    [this]<class Held>(const Held& held) -> uint32_t {
		    if constexpr (std::is_same_v<Held, std::monostate>) {
			    return osc_pos;
		    }
		    else {
			    return held.phase();
		    }
	    },
	    oscillator_);
}

void SourceSlot::advance_osc_phase(uint32_t delta) {
	std::visit(
	    [this, delta]<class Held>(Held& held) {
		    if constexpr (std::is_same_v<Held, std::monostate>) {
			    osc_pos += delta;
		    }
		    else {
			    held.advance_phase(delta);
		    }
	    },
	    oscillator_);
}

} // namespace deluge::synth
