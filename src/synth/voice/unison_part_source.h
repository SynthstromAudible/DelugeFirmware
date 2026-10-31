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

#pragma once

#include "definitions_cxx.hpp"
#include "dsp-next/fixed/noise/cong.h"
#include "synth/sources/dx7/dx7_source.h"
#include "synth/sources/input/input_source.h"
#include "synth/sources/sample/sample_source.h"
#include "synth/sources/supersaw/supersaw_source.h"
#include "synth/sources/va/virtual_analog_source.h"
#include "synth/sources/wavetable/wavetable_source.h"
#include "synth/voice/voice_inputs.h"
#include <optional>

namespace deluge::synth {

class VoiceUnisonPartSource {
public:
	VoiceUnisonPartSource() = default;
	~VoiceUnisonPartSource();
	/// Starts this part of the source for a note. @p priority_rating is the voice's in the cull order (a sample's
	/// cluster loads are ranked by it). @return false if the note cannot play.
	bool note_on(const deluge::synth::SourceSetup& source, uint32_t samples_late, uint32_t osc_retrigger_phase,
	             bool reset_everything, SynthMode synth_mode, int32_t note_code, uint8_t velocity,
	             int32_t priority_rating, deluge::dsp::fixed::Cong& noise);
	void unassign(bool deleting_song);
	/// Plays this part's sources through these ports, which must outlive it.
	void bind(deluge::synth::SamplePlaybackPort* sample, deluge::synth::DxVoicePort* dx,
	          deluge::synth::AudioInputPort* input);

	// The phase of a source no persistent engine plays (FM's carriers, noise); a free-running engine's first phase.
	uint32_t osc_pos{};
	// The persistent oscillator engine for this source, when its type is one the engines cover (at most one is set).
	// Each owns the phase across render blocks, built at note-on and kept across notes for free-running oscillators
	// (so none is reset in unassign). va_source: the six virtual-analog waves (subtractive and ring-mod).
	// wavetable_source: WAVETABLE. FM, noise, sample and input keep osc_pos.
	std::optional<deluge::synth::VirtualAnalogSource> va_source;
	std::optional<deluge::synth::WavetableSource> wavetable_source;
	// supersaw_source: SUPERSAW. Holds all seven saw phases; its phase is the centre saw's.
	std::optional<deluge::synth::SupersawSource> supersaw_source;

	/// The source's current oscillator phase — from whichever engine owns it, else osc_pos. (Read by osc-sync to
	/// seed the slave from the master at block start.)
	[[nodiscard]] uint32_t current_osc_phase() const {
		if (va_source.has_value()) {
			return va_source->phase();
		}
		if (wavetable_source.has_value()) {
			return wavetable_source->phase();
		}
		if (supersaw_source.has_value()) {
			return supersaw_source->phase();
		}
		return osc_pos;
	}
	/// Advance the source's oscillator phase by @p delta (a skipped osc-sync master still progresses).
	void advance_osc_phase(uint32_t delta) {
		if (va_source.has_value()) {
			va_source->advance_phase(delta);
		}
		else if (wavetable_source.has_value()) {
			wavetable_source->advance_phase(delta);
		}
		else if (supersaw_source.has_value()) {
			supersaw_source->advance_phase(delta);
		}
		else {
			osc_pos += delta;
		}
	}

	uint32_t phase_increment_stored_value{};
	int32_t carrier_feedback{};
	bool active{};
	deluge::synth::SampleSource sample_source{nullptr}; ///< the SAMPLE source (drives the reader through its port)
	deluge::synth::DX7Source dx7_source{nullptr};       ///< the DX7 source (forwards FM to its port)
	deluge::synth::InputSource input_source{nullptr};   ///< the INPUT_L/R/STEREO source (reads input through its port)
};

} // namespace deluge::synth
