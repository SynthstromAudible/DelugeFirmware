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
#include "dsp-next/core/fixed_point.h"
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

		// The neutral increment times the Q30 interval: the high word, a quarter of it, which the octave's run-time
		// shift places
		const auto phase_increment =
		    static_cast<uint32_t>(mul(dsp::Q<30>::from_raw(noteIntervalTable[note_within_octave]),
		                              dsp::Q<31>::from_raw(static_cast<int32_t>(source.neutral_phase_increment)))
		                              .raw());

		int32_t shift_right_amount = 13 - octave;
		if (shift_right_amount >= 0) {
			return phase_increment >> shift_right_amount;
		}

		int32_t shift_left_amount = 0 - shift_right_amount;
		// The limit is the top semitone's interval: for a 44.1 kHz sample it leaves two semitones under 2^31, more than
		// osc cents, master cents and the unison detune (up to 125 cents) add; a sample at another rate keeps less
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
		for (int32_t u = 0; u < note.snapshot->num_unison; u++) {
			note.parts[u].slots[s].active = source_ever_active;
		}
	}
	return true;
}

std::optional<uint32_t> detuned_increment(uint32_t phase_increment, const PhaseIncrementFineTuner& detuner) {
	// The tuner works on signed increments: below 2^31 the bits are the same
	const auto detuned = static_cast<uint32_t>(detuner.detune(static_cast<int32_t>(phase_increment)));
	if (detuned >= (uint32_t{1} << 31)) {
		return std::nullopt;
	}
	return detuned;
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
			for (int32_t u = 0; u < snapshot.num_unison; u++) {
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
			for (int32_t u = 0; u < snapshot.num_unison; u++) {
				const std::optional<uint32_t> part = detuned_increment(phase_increment, snapshot.unison_detuners[u]);
				if (part.has_value()) {
					note.parts[u].slots[s].phase_increment_stored_value = *part;
				}
				else {
					note.parts[u].slots[s].active = false;
				}
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
		for (int32_t u = 0; u < snapshot.num_unison; u++) {
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
