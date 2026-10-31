/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "synth/engines/slot_engine.h"
#include "dsp-next/core/q31_ops.h"          // multiply_32x32_rshift32
#include "util/lookuptables/lookuptables.h" // noteFrequencyTable, noteIntervalTable
#include <optional>
#include <utility>

namespace deluge::synth {

namespace {

/// @brief A source's phase increment for @p transposed_note_code, before its cents and unison detune.
/// @return None when the note is too high to render (above 22.05 kHz).
std::optional<uint32_t> base_phase_increment(const SourceSetup& source, SynthMode synth_mode,
                                             int32_t transposed_note_code) {
	// A sample or live input plays at its own rate: its neutral increment is the root note's
	if (synth_mode != SynthMode::FM
	    && (source.osc_type == OscType::SAMPLE || source.osc_type == OscType::INPUT_L
	        || source.osc_type == OscType::INPUT_R || source.osc_type == OscType::INPUT_STEREO)) {
		int32_t note_within_octave = static_cast<uint16_t>(transposed_note_code + 240) % 12;
		int32_t octave = static_cast<uint16_t>(transposed_note_code + 120) / 12;

		uint32_t phase_increment =
		    multiply_32x32_rshift32(noteIntervalTable[note_within_octave], source.neutral_phase_increment);

		int32_t shift_right_amount = 13 - octave;
		if (shift_right_amount >= 0) {
			return phase_increment >> shift_right_amount;
		}

		int32_t shift_left_amount = 0 - shift_right_amount;
		// Too high a semitone below the limit, because osc cents and unison could push it up a semitone
		if (std::cmp_greater_equal(phase_increment, 2026954652 >> shift_left_amount)) {
			return std::nullopt;
		}
		return phase_increment << shift_left_amount;
	}

	// A wave oscillator
	int32_t note_within_octave = static_cast<uint16_t>(transposed_note_code + 240 - 4) % 12;
	int32_t octave = (transposed_note_code + 120 - 4) / 12;

	int32_t shift_right_amount = 20 - octave;
	if (shift_right_amount >= 0) {
		return noteFrequencyTable[note_within_octave] >> shift_right_amount;
	}
	return std::nullopt;
}

} // namespace

bool SlotEngine::note_on(const EngineNote& note) {
	if (!mark_active(note, /*refuse_silent=*/false)) {
		return false;
	}
	calculate_phase_increments(note);
	return start_sources(note);
}

bool SlotEngine::mark_active(const EngineNote& note, bool refuse_silent) {
	const NoteSetup& setup = *note.setup;
	for (int32_t s = 0; s < kNumSources; s++) {
		bool source_ever_active = setup.sources[s].ever_active;
		if (!source_ever_active || !setup.sources[s].playable) {
			if (refuse_silent) {
				return false;
			}
			source_ever_active = false;
		}
		for (int32_t u = 0; std::cmp_less(u, note.snapshot->num_unison); u++) {
			note.parts[u].slots[s].active = source_ever_active;
		}
	}
	return true;
}

void SlotEngine::calculate_phase_increments(const EngineNote& note) {
	const NoteSetup& setup = *note.setup;
	const SoundSnapshot& snapshot = *note.snapshot;
	const int32_t note_code_with_master_transpose = note.note_code + snapshot.transpose;

	for (int32_t s = 0; s < kNumSources; s++) {
		const SourceSetup& source = setup.sources[s];

		const std::optional<uint32_t> base =
		    source.ever_active
		        ? base_phase_increment(source, snapshot.synth_mode, note_code_with_master_transpose + source.transpose)
		        : std::nullopt;
		if (!base.has_value()) { // Not sounding, or too high to render (above 22.05 kHz)
			for (int32_t u = 0; std::cmp_less(u, snapshot.num_unison); u++) {
				note.parts[u].slots[s].active = false;
			}
			continue;
		}

		// Cents, then the master transpose's
		// The tuners work on signed increments; the bits are the same
		auto phase_increment = static_cast<uint32_t>(source.fine_tuner.detune(static_cast<int32_t>(*base)));
		phase_increment = static_cast<uint32_t>(snapshot.fine_tuner.detune(static_cast<int32_t>(phase_increment)));

		if (snapshot.num_unison == 1) {
			note.parts[0].slots[s].phase_increment_stored_value = phase_increment;
		}
		else {
			for (int32_t u = 0; std::cmp_less(u, snapshot.num_unison); u++) {
				note.parts[u].slots[s].phase_increment_stored_value =
				    static_cast<uint32_t>(snapshot.unison_detuners[u].detune(static_cast<int32_t>(phase_increment)));
			}
		}
	}
}

bool SlotEngine::start_sources(const EngineNote& note) {
	const NoteSetup& setup = *note.setup;
	const SoundSnapshot& snapshot = *note.snapshot;
	for (int32_t s = 0; s < kNumSources; s++) {
		if (!setup.sources[s].ever_active) [[unlikely]] {
			continue;
		}
		for (int32_t u = 0; std::cmp_less(u, snapshot.num_unison); u++) {
			// Only a part marked active: among other things, a SAMPLE source then has a sample loaded
			SourceSlot& slot = note.parts[u].slots[s];
			if (slot.active
			    && !slot.note_on(setup.sources[s], note.samples_late, snapshot.osc_retrigger_phase[s], note.reset,
			                     snapshot.synth_mode, note.note_code, note.velocity, note.priority_rating, *note.noise))
			    [[unlikely]] {
				return false;
			}
		}
	}
	return true;
}

} // namespace deluge::synth
