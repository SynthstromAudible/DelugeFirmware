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

#pragma once

#include "instruments/deluge/sound_state.h"
#include "instruments/deluge/state_access.h"
#include "instruments/deluge/state_rules.h"

#include <array>
#include <cstddef>
#include <string>

namespace deluge::test {

/// @brief A record read in the file's flat layout, live: each access flattens the record as it is then, so a spec
/// that reads it after an Edit sees the Edit. Empty where there is no record.
class FlatView {
public:
	/// @brief The record flattened for one expression.
	struct Snapshot {
		instrument::FlatSoundState flat;
		const instrument::FlatSoundState* operator->() const { return &flat; }
	};
	explicit FlatView(const instrument::SoundState* record) : record_(record) {}
	Snapshot operator->() const { return {instrument::FlatSoundState(*record_)}; }
	instrument::FlatSoundState operator*() const { return *record_; }
	[[nodiscard]] bool has_value() const { return record_ != nullptr; }
	explicit operator bool() const { return has_value(); }

private:
	const instrument::SoundState* record_;
};

/// @brief @p record read live in the file's flat layout.
[[nodiscard]] inline FlatView flat_of(const instrument::SoundState* record) {
	return FlatView{record};
}

/// @brief Change @p record through its flat layout, as a spec that builds a state field by field does.
template <class Change>
void change_flat(instrument::SoundState& record, Change&& change) {
	instrument::FlatSoundState flat = record;
	change(flat);
	record = flat;
}

/// @brief The first state_access reader on which @p record and its flat layout disagree; empty where none does.
[[nodiscard]] inline std::string reader_difference(const instrument::SoundState& record) {
	namespace instrument = deluge::instrument;
	const instrument::FlatSoundState flat = record;
	if (instrument::synth_mode(record) != flat.synth_mode) {
		return "synth_mode";
	}
	for (size_t s = 0; s < flat.sources.size(); s++) {
		const auto id = static_cast<instrument::SourceId>(s);
		const instrument::SourceState& src = flat.sources[s];
		if (instrument::source_type(record, id) != src.osc_type) {
			return "source_type";
		}
		if (!(instrument::source_tuning(record, id)
		      == instrument::Tuning{.transpose = src.transpose, .cents = src.cents})) {
			return "source_tuning";
		}
		if (!(instrument::sample_playback(record, id)
		      == instrument::SamplePlayback{.repeat_mode = src.repeat_mode,
		                                    .time_stretch_amount = src.time_stretch_amount,
		                                    .interpolation = src.interpolation_mode,
		                                    .pitch_and_speed_independent = src.pitch_and_speed_independent,
		                                    .reversed = src.reversed})) {
			return "sample_playback";
		}
		if (instrument::source_ranges(record, id) != src.ranges) {
			return "source_ranges";
		}
		const instrument::DxPatchState* patch = instrument::dx_patch(record, id);
		if ((patch != nullptr) != src.dx_patch.has_value() || (patch != nullptr && !(*patch == *src.dx_patch))) {
			return "dx_patch";
		}
	}
	const synth::VoicingState voicing = instrument::voicing(record);
	if (voicing.priority != flat.voice_priority || voicing.polyphony != flat.polyphonic
	    || voicing.max_voices != flat.max_voice_count || voicing.master.transpose != flat.transpose
	    || voicing.master.cents != flat.cents) {
		return "voicing";
	}
	if (instrument::oscillator_sync(record) != flat.oscillator_sync) {
		return "oscillator_sync";
	}
	for (size_t s = 0; s < flat.sources.size(); s++) {
		if (instrument::phase_word(instrument::source_retrigger(record, static_cast<instrument::SourceId>(s)))
		    != flat.osc_retrigger_phase[s]) {
			return "source_retrigger";
		}
	}
	const std::array<synth::Pitch, 2> mods = instrument::modulators(record);
	for (size_t m = 0; m < mods.size(); m++) {
		if (mods[m].tuning.transpose != flat.modulator_transpose[m] || mods[m].tuning.cents != flat.modulator_cents[m]
		    || instrument::phase_word(mods[m].retrigger) != flat.modulator_retrigger_phase[m]) {
			return "modulators";
		}
	}
	if (instrument::modulator1_to_modulator0(record) != flat.modulator1_to_modulator0) {
		return "modulator1_to_modulator0";
	}
	for (size_t l = 0; l < 4; l++) {
		const synth::LfoState lfo = instrument::lfo(record, l);
		if (lfo.shape != flat.lfo_wave_type[l] || lfo.sync_type != flat.lfo_sync_type[l]
		    || lfo.sync_level != flat.lfo_sync_level[l]) {
			return "lfo";
		}
	}
	if (!(instrument::sound_effects(record) == flat.effects)) {
		return "sound_effects";
	}
	if (!(instrument::unison(record)
	      == deluge::synth::UnisonState{
	          .count = flat.num_unison, .detune = flat.unison_detune, .spread = flat.unison_stereo_spread})) {
		return "unison";
	}
	if (!(instrument::host_fields(record)
	      == instrument::HostFields{.side_chain_send_level = flat.side_chain_send_level,
	                                .output_midi_channel = flat.output_midi_channel,
	                                .output_midi_note_for_drum = flat.output_midi_note_for_drum})) {
		return "host_fields";
	}
	return {};
}

/// @brief The first rule on which the record's native form and the flat oracle disagree for @p flat; empty where none
/// does: every engine switch, every oscillator kind on each slot (with the held ranges of either kind), every loop
/// mode, and the setup transforms.
[[nodiscard]] inline std::string rule_difference(const instrument::FlatSoundState& flat) {
	namespace in = instrument;
	const in::SoundState record = flat;
	for (const in::SynthMode mode : {in::SynthMode::SUBTRACTIVE, in::SynthMode::RINGMOD, in::SynthMode::FM}) {
		if (!(in::switch_engine(record, mode) == in::SoundState(in::with_mode(flat, mode)))) {
			return "switch_engine";
		}
	}
	const in::OscType kinds[] = {in::OscType::SINE,      in::OscType::SAW,     in::OscType::SUPERSAW,
	                             in::OscType::WAVETABLE, in::OscType::SAMPLE,  in::OscType::DX7,
	                             in::OscType::INPUT_L,   in::OscType::INPUT_R, in::OscType::INPUT_STEREO};
	for (size_t s = 0; s < 2; s++) {
		const auto id = static_cast<in::SourceId>(s);
		for (const in::OscType type : kinds) {
			for (const bool wavetable : {false, true}) {
				if (!(in::switch_source(record, id, type, flat.sources[s].ranges, wavetable)
				      == in::SoundState(in::with_osc_type(flat, id, type, flat.sources[s].ranges, wavetable)))) {
					return "switch_source";
				}
			}
		}
		for (const in::SampleRepeatMode mode : {in::SampleRepeatMode::CUT, in::SampleRepeatMode::ONCE,
		                                        in::SampleRepeatMode::LOOP, in::SampleRepeatMode::STRETCH}) {
			if (!(in::with_loop_mode(record, id, mode) == in::SoundState(in::with_loop_mode(flat, id, mode)))) {
				return "with_loop_mode";
			}
		}
	}
	if (!(in::as_sample_sound(record) == in::SoundState(in::as_sample_sound(flat)))) {
		return "as_sample_sound";
	}
	if (!(in::as_default_synth(record) == in::SoundState(in::as_default_synth(flat)))) {
		return "as_default_synth";
	}
	for (const bool dx : {false, true}) {
		if (!(in::as_blank_synth(record, dx) == in::SoundState(in::as_blank_synth(flat, dx)))) {
			return "as_blank_synth";
		}
	}
	return {};
}

} // namespace deluge::test
