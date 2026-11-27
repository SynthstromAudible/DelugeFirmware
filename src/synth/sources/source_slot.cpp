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
#include "dsp/core/phasor.h"
#include "synth/sources/va/va_shapes.h" // va_shape::initial_phase_for_zero
#include <type_traits>

namespace deluge::synth {

SourceSlot::~SourceSlot() {
	unassign(false);
}

void SourceSlot::bind(const SourcePorts& ports) {
	for_each_row<PortedKinds>([&]<class Kind>(Kind) { Kind::bind(std::get<typename Kind::Source>(ported_), ports); });
}

template <class Fn>
bool SourceSlot::with_ported(OscType type, Fn fn) {
	return find_row<PortedKinds>([&]<class Kind>(Kind) {
		if (!Kind::plays(type)) {
			return false;
		}
		fn(Kind{}, std::get<typename Kind::Source>(ported_));
		return true;
	});
}

bool SourceSlot::note_on(const SourceSetup& source, uint32_t samples_late, uint32_t osc_retrigger_phase,
                         bool reset_everything, SynthMode synth_mode, int32_t note_code, uint8_t velocity,
                         int32_t priority_rating, dsp::fixed::Cong& noise) {
	const SourceNote note{
	    .samples_late = samples_late,
	    .note_code = note_code,
	    .velocity = velocity,
	    .priority_rating = priority_rating,
	    .osc_type = source.osc_type,
	};

	// Where the engine plays the slots, a port-backed source starts through its port (a sample's note-on is the whole
	// of the slot's) and any other type builds its held wave
	bool started = true;
	bool owned = false;
	const bool plays_slots = holds_oscillators(synth_mode);
	const bool ported = plays_slots && with_ported(source.osc_type, [&]<class Kind>(Kind, auto& src) {
		                    started = Kind::note_on(src, note);
		                    owned = Kind::kOwnsNote;
	                    });
	if (ported && (!started || owned)) {
		return started;
	}
	if (!ported) {
		build_oscillator(source.osc_type, plays_slots, osc_retrigger_phase, reset_everything, noise);
	}

	if (reset_everything) {
		carrier_feedback = dsp::Q<31>{};
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
	// The retrigger phase's word is the phase it adds; kFreeRunningPhase marks none
	const auto seed_phase = dsp::UQ<32>::from_raw(va_shape::initial_phase_for_zero(osc_type) + osc_retrigger_phase);
	const bool retrigger = osc_retrigger_phase != kFreeRunningPhase;
	const HeldBuild build{.type = osc_type,
	                      .seed = seed_phase,
	                      .retrigger = retrigger,
	                      .reset_everything = reset_everything,
	                      .osc_pos = osc_pos,
	                      .noise = &noise};
	const bool held = engine_mode && find_row<HeldKinds>([&]<class Kind>(Kind) {
		                  if (!Kind::plays(osc_type)) {
			                  return false;
		                  }
		                  Kind::build(oscillator_, build);
		                  return true;
	                  });
	if (!held) {
		oscillator_.emplace<std::monostate>(); // FM, noise and the rest keep osc_pos
		if (retrigger) {
			osc_pos = seed_phase;
		}
	}
}

void SourceSlot::unassign(bool deleting_song) {
	active = false;
	for_each_row<PortedKinds>(
	    [&]<class Kind>(Kind) { std::get<typename Kind::Source>(ported_).unassign(deleting_song); });
}

void SourceSlot::release(OscType type, const SourceRelease& release) {
	bool keeps = true;
	(void)with_ported(type, [&]<class Kind>(Kind, auto& src) { keeps = Kind::release(src, release, active); });
	if (!keeps) {
		unassign(false);
	}
}

Source* SourceSlot::source_for(OscType type) {
	Source* ported = nullptr;
	if (with_ported(type, [&]<class Kind>(Kind, auto& src) { ported = &src; })) {
		return ported;
	}
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

dsp::UQ<32> SourceSlot::current_osc_phase() const {
	return std::visit(
	    [this]<class Held>(const Held& held) -> dsp::UQ<32> {
		    if constexpr (std::is_same_v<Held, std::monostate>) {
			    return osc_pos;
		    }
		    else {
			    return held.phase();
		    }
	    },
	    oscillator_);
}

void SourceSlot::advance_osc_phase(dsp::UQ<32> delta) {
	std::visit(
	    [this, delta]<class Held>(Held& held) {
		    if constexpr (std::is_same_v<Held, std::monostate>) {
			    osc_pos = add_wrap(osc_pos, delta);
		    }
		    else {
			    held.advance_phase(delta);
		    }
	    },
	    oscillator_);
}

} // namespace deluge::synth
