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

#include "synth/voice/voice.h"
#include "bench/render_profile.h"
#include "definitions_cxx.hpp"
#include "dsp-next/fixed/interpolation/table_interpolation.h" // get_decay4
#include "dsp-next/fixed/shaping/wavefolder.h"
#include "dsp-next/fixed/vca/pan.h"
#include "synth/sources/interpolation_buffer_size.h"
#include "synth/voice/source_component_render.h"
#include "util/lookuptables/lookuptables.h"
#include "util/param_functions.h"

#include "dsp-next/fixed/tables/decay_small.h"
#include "util/misc.h"
#include <cstring>
#include <limits>
#include <new>
#include <optional>

using namespace deluge;
using deluge::dsp::fixed::get_decay4;
namespace params = deluge::modulation::params;

// Hopefully I could make this use the spareRenderingBuffer instead...

const Patcher::Config kPatcherConfigForVoice = {
    .first_param = 0,
    .first_non_volume_param = params::FIRST_LOCAL_NON_VOLUME,
    .first_hybrid_param = params::FIRST_LOCAL__HYBRID,
    .first_exp_param = params::FIRST_LOCAL_EXP,
    .end_params = params::FIRST_GLOBAL,
    .globality = GLOBALITY_LOCAL,
};

namespace deluge::synth {

using dsp::StereoSample;

namespace {
constexpr int32_t kQ31Max = std::numeric_limits<int32_t>::max();
constexpr int32_t kQ31Min = std::numeric_limits<int32_t>::min();
/// The bits of a priority rating that rank voices in one envelope stage by when they entered it.
constexpr uint32_t kTimeEnteredMask = 0x00FFFFFF;

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
		if (phase_increment >= (2026954652 >> shift_left_amount)) {
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

int32_t Voice::combine_expression_values(int32_t synth_level_value, int32_t expression_dimension) const {
	int32_t voice_level_value = this->local_expression_source_values_before_smoothing[expression_dimension];
	int32_t combined_value = (synth_level_value >> 1) + (voice_level_value >> 1);
	return lshift_and_saturate<1>(combined_value);
}

Voice::Voice() : patcher(kPatcherConfigForVoice, params.source_values, params.param_final_values) {
}

// Returns false if the note cannot play and the voice must be unassigned again
bool Voice::note_on(const deluge::synth::NoteSetup& setup, const SoundSnapshot& snapshot,
                    const deluge::synth::NoteOnEvent& event, deluge::synth::VoiceClock clock,
                    size_t num_active_voices) {
	input_characteristics[util::to_underlying(MIDICharacteristic::NOTE)] = event.note_before_arp;
	input_characteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = event.midi_channel;
	note_code_after_arpeggiation = event.note_after_arp;
	override_amplitude_envelope_release_rate = 0;

	if (event.note_after_arp >= 128) {
		params.source_values[util::to_underlying(PatchSource::NOTE)] = kQ31Max;
	}
	else if (event.note_after_arp <= 0) {
		params.source_values[util::to_underlying(PatchSource::NOTE)] = kQ31Min;
	}
	else {
		params.source_values[util::to_underlying(PatchSource::NOTE)] = (event.note_after_arp - 64) * 33554432;
	}

	// Setup "half-baked" envelope output values. These need to exist before we do the initial patching below - and it's
	// only after that that we can render the "actual" envelope output values, taking their own input patching into
	// account.
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		envelopes[e].set_output_level(setup.attack_is_zero[e] ? kQ31Max : 0);
	}

	// Setup and render local LFO
	lfo2.set_local_initial_phase(snapshot.lfo_config[LFO2_ID]);
	params.source_values[util::to_underlying(PatchSource::LFO_LOCAL_1)] =
	    lfo2.render(0, snapshot.lfo_config[LFO2_ID], 0, *noise_);
	lfo4.set_local_initial_phase(snapshot.lfo_config[LFO4_ID]);
	params.source_values[util::to_underlying(PatchSource::LFO_LOCAL_2)] =
	    lfo4.render(0, snapshot.lfo_config[LFO4_ID], 0, *noise_);

	// Setup some sources which won't change for the duration of this note
	params.source_values[util::to_underlying(PatchSource::VELOCITY)] =
	    (event.velocity == 128) ? kQ31Max : (static_cast<int32_t>(event.velocity) - 64) * 33554432;

	// "Random" source
	params.source_values[util::to_underlying(PatchSource::RANDOM)] = next_noise();

	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		local_expression_source_values_before_smoothing[m] = event.mpe[m] << 16;
		params.source_values[util::to_underlying(PatchSource::X) + m] =
		    combine_expression_values(setup.monophonic_expression[m], m);
	}

	if (event.reset_envelopes) {
		source_amplitudes_last_time.fill(0);
		fm_engine.modulator_amplitude_last_time.fill(0);
		overall_osc_amplitude_last_time = 0;
		done_first_render = false;
		filter_set.reset();

		saturation_.reset();
	}

	if (setup.porta) [[unlikely]] {
		setup_porta(setup.last_note_code);
	}
	else {
		porta_envelope_pos = kNoPorta;
	}

	// Patch all sources to exclusive params, to give them an initial value. Exclusive params (params with just 1
	// source) aren't continuously recalculated, so they need that initial value. Remember, calculating that initial
	// value also takes into account the "preset value". This probably isn't strictly necessary for sources which we
	// know will be constantly changing, because that would make patching constantly calculate too. But that's only
	// really the envelopes, plus the LFOs (just the local one?) if they're not square
	std::ranges::copy(setup.global_source_values, std::begin(params.source_values));
	patcher.perform_initial_patching(setup.initial_patching);

	// Setup and render envelopes - again. Because they're local params (since mid-late 2017), we really need to render
	// them *after* initial patching is performed.
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		int32_t attack = params.param_final_values[params::LOCAL_ENV_0_ATTACK + e];
		int32_t sustain = params.param_final_values[params::LOCAL_ENV_0_SUSTAIN + e];
		bool directly_to_decay = (attack > 245632); // Skip a (near-)instant attack straight to decay.
		params.source_values[util::to_underlying(PatchSource::ENVELOPE_0) + e] =
		    envelopes[e].note_on(directly_to_decay, sustain);
	}
	stamp_envelope0_stage_entry(clock);

	if (event.reset_envelopes) {
		for (int32_t s = 0; s < kNumSources; s++) {
			source_wave_indexes_last_time[s] = params.param_final_values[params::LOCAL_OSC_A_WAVE_INDEX + s];
		}
	}

	// Each source is active in every unison part if it can sound. In ring-mod, a source that cannot play fails the
	// note.
	for (int32_t s = 0; s < kNumSources; s++) {
		bool source_ever_active = setup.sources[s].ever_active;
		if (!source_ever_active || !setup.sources[s].playable) {
			if (snapshot.synth_mode == SynthMode::RINGMOD) {
				return false;
			}
			source_ever_active = false;
		}
		for (int32_t u = 0; u < snapshot.num_unison; u++) {
			unison_parts[u].sources[s].active = source_ever_active;
		}
	}

	calculate_phase_increments(setup, snapshot);

	const auto rating = static_cast<int32_t>(priority_rating(snapshot.voice_priority, num_active_voices));
	for (int32_t s = 0; s < kNumSources; s++) {
		if (!setup.sources[s].ever_active) [[unlikely]] {
			continue;
		}

		for (int32_t u = 0; u < snapshot.num_unison; u++) {

			// Check that we already marked this unison-part-source as active. Among other things, this ensures that if
			// the osc is set to SAMPLE, there actually is a sample loaded.
			if (unison_parts[u].sources[s].active) {
				bool success = unison_parts[u].sources[s].note_on(
				    setup.sources[s], event.samples_late, snapshot.osc_retrigger_phase[s], event.reset_envelopes,
				    snapshot.synth_mode, note_code_after_arpeggiation, event.velocity, rating, *noise_);
				if (!success) [[unlikely]] {
					return false; // This shouldn't really ever happen I don't think really...
				}
			}
		}
	}

	if (snapshot.synth_mode == SynthMode::FM) {
		uint32_t initial_phase = deluge::synth::va_shape::initial_phase_for_zero(OscType::SINE);
		for (int32_t u = 0; u < snapshot.num_unison; u++) {
			for (int32_t m = 0; m < kNumModulators; m++) {
				if (snapshot.modulator_retrigger_phase[m] != kNoRetrigger) {
					fm_engine.modulator_phase[u][m] = initial_phase + snapshot.modulator_retrigger_phase[m];
				}
				if (event.reset_envelopes) {
					fm_engine.modulator_feedback[u][m] = 0;
				}
			}
		}
	}

	previously_ignored_note_off = false;
	expression_sources_currently_smoothing.reset();
	filter_gain_last_time = 0;

	return true;
}

void Voice::expression_event_immediate(int32_t monophonic_value, int32_t voice_level_value, int32_t s) {
	int32_t expression_dimension = s - util::to_underlying(PatchSource::X);
	local_expression_source_values_before_smoothing[expression_dimension] = voice_level_value;
	expression_sources_final_value_changed[expression_dimension] = true;

	params.source_values[s] = combine_expression_values(monophonic_value, expression_dimension);
}

void Voice::expression_event_smooth(int32_t new_value, int32_t s) {
	int32_t expression_dimension = s - util::to_underlying(PatchSource::X);
	local_expression_source_values_before_smoothing[expression_dimension] = new_value;
	expression_sources_currently_smoothing[expression_dimension] = true;
}

void Voice::change_note(const deluge::synth::NoteSetup& setup, const SoundSnapshot& snapshot,
                        int32_t note_before_arpeggiation, int32_t note_after_arpeggiation, int32_t midi_channel,
                        std::span<const int16_t, kNumExpressionDimensions> mpe) {
	input_characteristics[util::to_underlying(MIDICharacteristic::NOTE)] = note_before_arpeggiation;
	input_characteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = midi_channel;
	note_code_after_arpeggiation = note_after_arpeggiation;

	// We definitely want to go to these values smoothly. Probably wish it was even smoother... Actually nah this sounds
	// / feels great!
	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		local_expression_source_values_before_smoothing[m] = mpe[m] << 16;
		// TODO: what if there's just channel aftertouch, and it's still held down...
	}
	expression_sources_currently_smoothing = 0b111;

	if (setup.porta) {
		setup_porta(setup.last_note_code);
	}

	calculate_phase_increments(setup, snapshot);
}

void Voice::setup_porta(int32_t last_note_code) {
	porta_envelope_pos = 0;
	int32_t semitone_adjustment = last_note_code - note_code_after_arpeggiation;

	int32_t note_within_octave = (semitone_adjustment + 120) % 12;
	int32_t octave = (semitone_adjustment + 120) / 12;

	int32_t phase_increment = noteIntervalTable[note_within_octave];

	int32_t shift_right_amount = 16 - octave;
	if (shift_right_amount >= 0) {
		phase_increment >>= shift_right_amount;
	}
	else {
		phase_increment = kQ31Max;
	}

	porta_envelope_max_amplitude = phase_increment - kMaxSampleValue;
}

void Voice::randomize_osc_phases(const SoundSnapshot& snapshot) {
	for (int32_t u = 0; u < snapshot.num_unison; u++) {
		for (int32_t s = 0; s < kNumSources; s++) {
			unison_parts[u].sources[s].osc_pos = next_noise();
			// TODO: we should do sample play pos, too
		}
		if (snapshot.synth_mode == SynthMode::FM) {
			for (int32_t m = 0; m < kNumModulators; m++) {
				fm_engine.modulator_phase[u][m] = next_noise();
			}
		}
	}
}

void Voice::calculate_phase_increments(const deluge::synth::NoteSetup& setup, const SoundSnapshot& snapshot) {

	int32_t note_code_with_master_transpose = note_code_after_arpeggiation + snapshot.transpose;

	for (int32_t s = 0; s < kNumSources; s++) {
		const deluge::synth::SourceSetup& source = setup.sources[s];

		const std::optional<uint32_t> base =
		    source.ever_active
		        ? base_phase_increment(source, snapshot.synth_mode, note_code_with_master_transpose + source.transpose)
		        : std::nullopt;
		if (!base.has_value()) { // Not sounding, or too high to render (above 22.05 kHz)
			for (int32_t u = 0; u < snapshot.num_unison; u++) {
				unison_parts[u].sources[s].active = false;
			}
			continue;
		}

		uint32_t phase_increment = *base;

		// Cents
		phase_increment = source.fine_tuner.detune(phase_increment);
		phase_increment = snapshot.fine_tuner.detune(phase_increment); // master transpose

		// If only one unison
		if (snapshot.num_unison == 1) {
			unison_parts[0].sources[s].phase_increment_stored_value = phase_increment;
		}

		// Or if multiple unison
		else {
			for (int32_t u = 0; u < snapshot.num_unison; u++) {
				unison_parts[u].sources[s].phase_increment_stored_value =
				    snapshot.unison_detuners[u].detune(phase_increment);
			}
		}
	}

	// FM modulators
	if (snapshot.synth_mode == SynthMode::FM) {
		for (int32_t m = 0; m < kNumModulators; m++) {

			if (!setup.modulator_audible[m]) {
				continue; // Only if modulator active
			}

			int32_t transposed_note_code = note_code_with_master_transpose + snapshot.modulator_transpose[m];
			int32_t note_within_octave = (transposed_note_code + 120 - 4) % 12;
			int32_t octave = (transposed_note_code + 120 - 4) / 12;
			int32_t shift_right_amount = 20 - octave;

			int32_t phase_increment = 0;

			if (shift_right_amount >= 0) {
				phase_increment = noteFrequencyTable[note_within_octave] >> shift_right_amount;
			}

			else {
				// Frequency too high to render! (Higher than 22.05kHz)
				for (int32_t u = 0; u < snapshot.num_unison; u++) {
					fm_engine.modulator_phase_increment[u][m] = FMEngine::kInactive;
				}
				continue;
			}

			// Cents
			phase_increment = snapshot.modulator_transposers[m].detune(phase_increment);
			phase_increment = snapshot.fine_tuner.detune(phase_increment); // master transpose

			// If only one unison
			if (snapshot.num_unison == 1) {
				fm_engine.modulator_phase_increment[0][m] = phase_increment;
			}

			// Or if multiple unison
			else {
				for (int32_t u = 0; u < snapshot.num_unison; u++) {
					fm_engine.modulator_phase_increment[u][m] = snapshot.unison_detuners[u].detune(phase_increment);
				}
			}
		}
	}
}

void Voice::note_off(const deluge::synth::ReleaseInfo& release, bool allow_release_stage,
                     deluge::synth::VoiceClock clock) {

	// Only do it if note-offs are meant to be processed for this sound. Otherwise ignore it.
	if (release.note_tails_allowed) {

		// If no release-stage, we'll stop as soon as we can
		if (!allow_release_stage || !has_release_stage()) {
			envelopes[0].unconditional_release(Envelope::Stage::FAST_RELEASE);
		}

		// Or, do the release-stage
		else {
			envelopes[0].note_off(release.envelope_has_sustain[0]);

			// Only start releasing envelope 2 if release wasn't at max value. LOCAL_ENV_1_RELEASE is a
			// *local* param, so read the voice's own params.paramFinalValues (sized LOCAL_LAST) — not
			// sound.paramFinalValues, which holds *global* params only (indexing it here was an
			// out-of-bounds read of adjacent Sound memory; the hardened host build's bounds check caught it).
			if (params.param_final_values[params::LOCAL_ENV_1_RELEASE] >= 9) {
				envelopes[1].note_off(release.envelope_has_sustain[1]);
			}
		}
		stamp_envelope0_stage_entry(clock);
	}

	else {
		previously_ignored_note_off = true;
	}

	if (release.synth_mode != SynthMode::FM) {
		const auto rating = static_cast<int32_t>(priority_rating(release.voice_priority, release.num_active_voices));
		for (int32_t s = 0; s < kNumSources; s++) {
			if (release.osc_types[s] == OscType::SAMPLE) {
				for (int32_t u = 0; u < release.num_unison; u++) {
					if (unison_parts[u].sources[s].active
					    && !unison_parts[u].sources[s].sample_source.port()->note_off_at_loop_end(rating)) {
						unison_parts[u].sources[s].unassign(false);
					}
				}
			}
			else if (release.osc_types[s] == OscType::DX7) {
				for (int u = 0; u < release.num_unison; u++) {
					unison_parts[u].sources[s].dx7_source.keyup();
				}
			}
		}
	}
}

uint32_t Voice::get_local_lfo_phase_increment(const LFOConfig& config, deluge::modulation::params::Local param,
                                              uint32_t synced_phase_increment) {
	if (config.sync_level == deluge::plugin::SyncLevel::NONE) {
		return params.param_final_values[param];
	}
	return synced_phase_increment;
}

// Returns false if became inactive and needs unassigning
[[gnu::hot]] bool Voice::render(const ControlBlock& control_block, const VoiceConfig& cfg, const VoiceBlock& block,
                                const VoiceRenderArgs& args) {
	spare_buffers_ = args.spare_buffers; // the render helpers read these
	sync_scratch_ = args.sync_scratch;
	int32_t* sound_buffer = args.buffer;
	const int32_t num_samples = args.num_samples;
	const bool sound_rendering_in_stereo = args.sound_in_stereo;
	const bool applying_pan_at_voice_level = args.pan_at_voice_level;
	uint32_t sources_changed = args.sources_changed;
	const int32_t external_pitch_adjust = args.pitch_adjust;

	bool did_stereo_temp_buffer = false;

	// Do envelopes - if they're patched to something (always do the first one though)
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		if (e == 0
		    || ((cfg.local_sources_patched_to_anything & (1 << (util::to_underlying(PatchSource::ENVELOPE_0) + e)))
		        != 0u)) {
			int32_t old = params.source_values[util::to_underlying(PatchSource::ENVELOPE_0) + e];
			int32_t release = params.param_final_values[params::LOCAL_ENV_0_RELEASE + e];
			if (e == 0 && (override_amplitude_envelope_release_rate != 0)) {
				release = override_amplitude_envelope_release_rate;
			}
			params.source_values[util::to_underlying(PatchSource::ENVELOPE_0) + e] =
			    envelopes[e].render(num_samples, params.param_final_values[params::LOCAL_ENV_0_ATTACK + e],
			                        params.param_final_values[params::LOCAL_ENV_0_DECAY + e],
			                        params.param_final_values[params::LOCAL_ENV_0_SUSTAIN + e], release,
			                        deluge::dsp::fixed::tables::kDecaySmall8.data());
			uint32_t any_change =
			    static_cast<uint32_t>(old != params.source_values[util::to_underlying(PatchSource::ENVELOPE_0) + e]);
			sources_changed |= any_change << (util::to_underlying(PatchSource::ENVELOPE_0) + e);
		}
	}
	stamp_envelope0_stage_entry(block.clock);

	bool unassign_voice_after = (envelopes[0].stage() == Envelope::Stage::OFF)
	                            || (envelopes[0].stage() > Envelope::Stage::DECAY
	                                && params.source_values[util::to_underlying(PatchSource::ENVELOPE_0)]
	                                       == std::numeric_limits<int32_t>::min());
	// Local LFO
	if ((cfg.local_sources_patched_to_anything & (1 << util::to_underlying(PatchSource::LFO_LOCAL_1))) != 0u) {
		int32_t old = params.source_values[util::to_underlying(PatchSource::LFO_LOCAL_1)];
		// TODO: Same as with LFO1, there should be no reason to recompute the phase increment
		// for every sample.
		params.source_values[util::to_underlying(PatchSource::LFO_LOCAL_1)] =
		    lfo2.render(num_samples, cfg.lfo2_config,
		                get_local_lfo_phase_increment(cfg.lfo2_config, params::LOCAL_LFO_LOCAL_FREQ_1,
		                                              cfg.lfo2_synced_phase_increment),
		                *noise_);
		uint32_t any_change =
		    static_cast<uint32_t>(old != params.source_values[util::to_underlying(PatchSource::LFO_LOCAL_1)]);
		sources_changed |= any_change << util::to_underlying(PatchSource::LFO_LOCAL_1);
	}
	if ((cfg.local_sources_patched_to_anything & (1 << util::to_underlying(PatchSource::LFO_LOCAL_2))) != 0u) {
		int32_t old = params.source_values[util::to_underlying(PatchSource::LFO_LOCAL_2)];
		// TODO: Same as with LFO1, there should be no reason to recompute the phase increment
		// for every sample.
		params.source_values[util::to_underlying(PatchSource::LFO_LOCAL_2)] =
		    lfo4.render(num_samples, cfg.lfo4_config,
		                get_local_lfo_phase_increment(cfg.lfo4_config, params::LOCAL_LFO_LOCAL_FREQ_2,
		                                              cfg.lfo4_synced_phase_increment),
		                *noise_);
		uint32_t any_change =
		    static_cast<uint32_t>(old != params.source_values[util::to_underlying(PatchSource::LFO_LOCAL_2)]);
		sources_changed |= any_change << util::to_underlying(PatchSource::LFO_LOCAL_2);
	}

	// MPE params

	expression_sources_currently_smoothing |= cfg.expression_sources_changed_at_synth_level;

	if (expression_sources_currently_smoothing.any()) {
		expression_sources_final_value_changed |= expression_sources_currently_smoothing;

		for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
			if (expression_sources_currently_smoothing[i]) {

				int32_t target_value = combine_expression_values(cfg.monophonic_expression_values[i], i);

				int32_t diff =
				    (target_value >> 8) - (params.source_values[i + util::to_underlying(PatchSource::X)] >> 8);

				if (diff == 0) {
					expression_sources_currently_smoothing[i] = false;
				}
				else {
					int32_t amount_to_add = diff * num_samples / 4;
					params.source_values[i + util::to_underlying(PatchSource::X)] += amount_to_add;
				}
			}
		}
	}

	sources_changed |= expression_sources_final_value_changed.to_ulong() << util::to_underlying(PatchSource::X);

	expression_sources_final_value_changed.reset();

	// Patch all the sources to their parameters
	if (sources_changed != 0u) {
		for (int32_t s = 0; s < util::to_underlying(kFirstLocalSource); s++) {
			params.source_values[s] = cfg.global_source_values[s];
		}
#if defined(DELUGE_STREAM_BENCH)
		const uint32_t t0 = deluge::bench::render_profile::cycles();
#endif
		patcher.perform_patching(sources_changed, control_block);
#if defined(DELUGE_STREAM_BENCH)
		deluge::bench::render_profile::g_totals.voice_patch += deluge::bench::render_profile::cycles() - t0;
#endif
	}

	// Sort out pitch
	int32_t overall_pitch_adjust = params.param_final_values[params::LOCAL_PITCH_ADJUST];

	// Pitch adjust from "external" - e.g. the Kit
	if (external_pitch_adjust != kMaxSampleValue) {
		int32_t output = multiply_32x32_rshift32_rounded(overall_pitch_adjust, external_pitch_adjust);
		if (output > 8388607) {
			output = 8388607; // Limit it a bit. Not really quite sure if necessary?
		}
		overall_pitch_adjust = output << 8;
	}

	// Pitch adjust via MIDI pitch bend and MPE
	int32_t total_bend_amount = // Comes out as a 32-bit number representing a range of +-192 semitones
	    (cfg.monophonic_expression_values[0] / 192) * cfg.bend_ranges[BEND_RANGE_MAIN]
	    + (local_expression_source_values_before_smoothing[0] / 192) * cfg.bend_ranges[BEND_RANGE_FINGER_LEVEL];

	overall_pitch_adjust = getExp(overall_pitch_adjust, total_bend_amount >> 1);

	// Porta
	if (porta_envelope_pos < 8388608) {
		int32_t env_value = get_decay4(porta_envelope_pos, 23);
		int32_t pitch_adjustment_here =
		    kMaxSampleValue + (multiply_32x32_rshift32_rounded(env_value, porta_envelope_max_amplitude) << 1);

		int32_t a = multiply_32x32_rshift32_rounded(overall_pitch_adjust, pitch_adjustment_here);
		if (a > 8388607) {
			a = 8388607; // Prevent overflow! Happened to Matt Bates
		}
		overall_pitch_adjust = a << 8;

		// Move envelope on. Using the "release rate" lookup table gives by far the best range of speed values
		int32_t envelope_speed = lookupReleaseRate(cableToExpParamShortcut(cfg.portamento)) >> 13;
		porta_envelope_pos += envelope_speed * num_samples;
	}

	// Auto-release for a play-once sample. Despite this being envelope-related, meaning we'd ideally prefer to do it
	// before patching, we can only do it after because we need to know pitch
	auto_release_if_due(cfg, block, overall_pitch_adjust);

	if (!done_first_render && params.param_final_values[params::LOCAL_ENV_0_ATTACK] > 245632) {
		for (int32_t m = 0; m < kNumModulators; m++) {
			fm_engine.modulator_amplitude_last_time[m] =
			    params.param_final_values[params::LOCAL_MODULATOR_0_VOLUME + m];
		}
	}

	// Apply envelope 0 to volume. This takes effect as a cut only; when the envelope is at max height, volume is
	// unaffected. Important that we use lshift_and_saturate here - otherwise, number can overflow if combining high
	// velocity patching with big LFO
	int32_t overall_osc_amplitude = lshift_and_saturate<2>(multiply_32x32_rshift32(
	    params.param_final_values[params::LOCAL_VOLUME],
	    (params.source_values[util::to_underlying(PatchSource::ENVELOPE_0)] >> 1) + 1073741824));

	// This is the gain which gets applied to compensate for any change in gain that the filter is going to cause
	int32_t filter_gain = 0;

	// Prepare the filters. Level adjustment for unison happens before the filters, so the gain going in is neutral.
	const bool allow_oversampling = cfg.cpu_direness < 14;
	filter_gain = filter_set.configure(cfg.volume_neutral_value_for_unison << 1,
	                                   {
	                                       .frequency = params.param_final_values[params::LOCAL_LPF_FREQ],
	                                       .resonance = params.param_final_values[params::LOCAL_LPF_RESONANCE],
	                                       .morph = params.param_final_values[params::LOCAL_LPF_MORPH],
	                                       .allow_oversampling = allow_oversampling,
	                                   },
	                                   cfg.low_pass,
	                                   {
	                                       .frequency = params.param_final_values[params::LOCAL_HPF_FREQ],
	                                       .resonance = params.param_final_values[params::LOCAL_HPF_RESONANCE],
	                                       .morph = params.param_final_values[params::LOCAL_HPF_MORPH],
	                                       .allow_oversampling = allow_oversampling,
	                                   },
	                                   cfg.high_pass, cfg.filter_route);

	SynthMode synth_mode = cfg.synth_mode;

	int32_t source_amplitudes[kNumSources];
	int32_t source_amplitude_increments[kNumSources];

	bool modulators_active[kNumModulators];
	int32_t modulator_amplitude_increments[kNumModulators];

	int32_t overall_oscillator_amplitude_increment = 0;

	// If not ringmod, then sources need their volume calculated
	if (synth_mode != SynthMode::RINGMOD) [[likely]] {

		// params::LOCAL_OSC_x_VOLUME can normally only be up to a quarter of full range, but patching can make it up to
		// full-range overallOscAmplitude (same range as) params::LOCAL_VOLUME, is the same.

		// Let's impose a new limit, that only a total of 4x amplification via patching is possible (not 16x).
		// Chances are, the user won't even need that much, let alone would have the osc volume *and* the synth master
		// volume on full We then have space to make each osc's amplitude 4x what it could have been otherwise

		// If FM, we work the overall amplitude into each oscillator's, to avoid having to do an extra multiplication
		// for every audio sample at the end
		if (synth_mode == SynthMode::FM) {

			// Apply compensation for unison
			overall_osc_amplitude =
			    multiply_32x32_rshift32_rounded(overall_osc_amplitude, cfg.volume_neutral_value_for_unison) << 3;

			int32_t a =
			    multiply_32x32_rshift32(params.param_final_values[params::LOCAL_OSC_A_VOLUME], overall_osc_amplitude);
			int32_t b =
			    multiply_32x32_rshift32(params.param_final_values[params::LOCAL_OSC_B_VOLUME], overall_osc_amplitude);

			// Clip off those amplitudes before they get too high. I think these were originally intended to stop the
			// amplitude rising to more than "4", whatever that meant?
			source_amplitudes[0] = std::min(a, static_cast<int32_t>(134217727));
			source_amplitudes[1] = std::min(b, static_cast<int32_t>(134217727));
		}

		// Or if subtractive, we don't do that, because we do want to apply the overall amplitude *after* the filter
		else {
			if (cfg.has_filters) {
				source_amplitudes[0] =
				    multiply_32x32_rshift32_rounded(params.param_final_values[params::LOCAL_OSC_A_VOLUME], filter_gain);
				source_amplitudes[1] =
				    multiply_32x32_rshift32_rounded(params.param_final_values[params::LOCAL_OSC_B_VOLUME], filter_gain);
			}
			else {
				source_amplitudes[0] = params.param_final_values[params::LOCAL_OSC_A_VOLUME] >> 4;
				source_amplitudes[1] = params.param_final_values[params::LOCAL_OSC_B_VOLUME] >> 4;
			}
		}

		bool should_avoid_incrementing = done_first_render
		                                     ? (filter_gain_last_time != filter_gain)
		                                     : (params.param_final_values[params::LOCAL_ENV_0_ATTACK] > 245632);

		if (should_avoid_incrementing) {
			for (int32_t s = 0; s < kNumSources; s++) {
				source_amplitudes_last_time[s] = source_amplitudes[s];
			}
		}

		for (int32_t s = 0; s < kNumSources; s++) {
			source_amplitude_increments[s] =
			    static_cast<int32_t>(source_amplitudes[s] - source_amplitudes_last_time[s]) / num_samples;
		}

		filter_gain_last_time = filter_gain;

		// If FM, cache whether modulators are active
		if (synth_mode == SynthMode::FM) {
			for (int32_t m = 0; m < kNumModulators; m++) {
				modulators_active[m] = (params.param_final_values[params::LOCAL_MODULATOR_0_VOLUME + m] != 0
				                        || fm_engine.modulator_amplitude_last_time[m] != 0);

				if (modulators_active[m]) {
					modulator_amplitude_increments[m] =
					    static_cast<int32_t>(params.param_final_values[params::LOCAL_MODULATOR_0_VOLUME + m]
					                         - fm_engine.modulator_amplitude_last_time[m])
					    / num_samples;
				}
			}
		}
	}

	int32_t source_wave_index_increments[kNumSources];

	if (synth_mode != SynthMode::FM) {
		if (!done_first_render && params.param_final_values[params::LOCAL_ENV_0_ATTACK] > 245632) {
			overall_osc_amplitude_last_time = overall_osc_amplitude;
		}
		overall_oscillator_amplitude_increment =
		    (overall_osc_amplitude - overall_osc_amplitude_last_time) / num_samples;

		for (int32_t s = 0; s < kNumSources; s++) {
			source_wave_index_increments[s] =
			    static_cast<int32_t>(params.param_final_values[params::LOCAL_OSC_A_WAVE_INDEX + s]
			                         - source_wave_indexes_last_time[s])
			    / num_samples;
		}
	}

	done_first_render = true;

	uint32_t osc_sync_pos[kMaxNumVoicesUnison];
	bool doing_osc_sync = cfg.doing_osc_sync;

	// Oscillator sync: capture the master (source 0) phase at block start — from whichever persistent engine owns
	// it (virtual-analog / wavetable), else oscPos.
	if (doing_osc_sync) {
		for (int32_t u = 0; u < cfg.num_unison; u++) {
			osc_sync_pos[u] = unison_parts[u].sources[0].current_osc_phase();
		}
	}

	// whether stereo unison actually is active. if stereo is being vetoed from higher up, don't do it.
	bool stereo_unison = (cfg.unison_stereo_spread != 0u) && cfg.num_unison > 1 && sound_rendering_in_stereo;

	int32_t* osc_buffer = nullptr;
	bool anything_in_osc_buffer = false;
	int32_t source_amplitudes_now[kNumSources];
	for (int32_t s = 0; s < kNumSources; s++) {
		source_amplitudes_now[s] = source_amplitudes_last_time[s];
	}

	int32_t amplitude_l = 0, amplitude_r = 0;
	bool do_panning = false;

	// two first indicies are reserved in case we need stereo for unison spread
	osc_buffer = spare_buffers_[0];
	int32_t channels = stereo_unison ? 2 : 1;

	int32_t const* const noise_end = osc_buffer + num_samples * channels;

	// If any noise, do that. By cutting a corner here, we do it just once for all "unison", rather than for each
	// unison. Increasing number of unison cuts the volume of the oscillators
	if (params.param_final_values[params::LOCAL_NOISE_VOLUME] != 0 && synth_mode != SynthMode::FM) [[unlikely]] {

		// This was >>2, but because I had a bug in V2.0.x which made noise too loud if filter on,
		// I'm now making this louder to compensate and remain consistent by going just >>1.
		// So now I really need to make it so that sounds made before V2.0 halve their noise volume... (Hey, did I
		// ever do this? Who knows...)
		int32_t n = params.param_final_values[params::LOCAL_NOISE_VOLUME] >> 1;
		if (cfg.has_filters) {
			n = multiply_32x32_rshift32(n, filter_gain) << 4;
		}

		// Perform the same limiting that we do above for the oscillators
		int32_t noise_amplitude = std::min(n, static_cast<int32_t>(268435455)) >> 2;

		int32_t* __restrict__ this_sample = osc_buffer;
		do {
			*this_sample = multiply_32x32_rshift32(next_noise(), noise_amplitude);
			this_sample++;
		} while (this_sample != noise_end);

		anything_in_osc_buffer = true;
	}

	// Otherwise, clear the buffer
	else {
		memset(osc_buffer, 0, channels * num_samples * sizeof(int32_t));
	}

	// Even if first rendering into a local Voice buffer, we'll very often still just do panning at the Sound level
	if (!applying_pan_at_voice_level) {
		do_panning = false;
	}
	else {
		// Set up panning
		const int32_t pan = params.param_final_values[params::LOCAL_PAN];
		do_panning = false;
		if (cfg.engine_rendering_in_stereo) {
			const deluge::dsp::fixed::PanAmplitudes amplitudes = deluge::dsp::fixed::pan_amplitudes(pan);
			amplitude_l = amplitudes.left;
			amplitude_r = amplitudes.right;
			do_panning = pan != 0;
		}
	}

	// Normal mode: subtractive / samples. The engine owns the per-source render loop + osc-sync + the stereo
	// buffer dance + the unison-inactive→unassign decision (it delegates each source's render back to
	// renderBasicSource for now).
	if (synth_mode == SynthMode::SUBTRACTIVE) {
		subtractive_engine.render(*this, control_block, block, osc_buffer, num_samples, doing_osc_sync, osc_sync_pos,
		                          overall_pitch_adjust, source_amplitudes_now, source_amplitude_increments,
		                          source_wave_index_increments, anything_in_osc_buffer, did_stereo_temp_buffer,
		                          unassign_voice_after, cfg);
	}

	// Ring-mod: the engine owns the whole per-voice render (unison loop + per-source pitch math + the combine),
	// mirroring SubtractiveEngine. The per-voice pre-roll is passed in.
	else if (synth_mode == SynthMode::RINGMOD) {
		ring_mod_engine.render(*this, osc_buffer, num_samples, doing_osc_sync, osc_sync_pos, overall_pitch_adjust,
		                       source_wave_index_increments, stereo_unison, filter_gain, did_stereo_temp_buffer,
		                       spare_buffers_[2], spare_buffers_[3], sync_scratch_, cfg, block);
	}

	// FM: the engine owns the whole per-voice render (unison loop + per-source carrier pitch math + the modulator/
	// carrier topology), mirroring SubtractiveEngine / RingModEngine. The per-voice pre-roll is passed in.
	else {
		fm_engine.render(*this, num_samples, overall_pitch_adjust, stereo_unison, source_amplitudes,
		                 source_amplitudes_now, source_amplitude_increments, modulators_active,
		                 modulator_amplitude_increments, osc_buffer, spare_buffers_[2], spare_buffers_[3],
		                 did_stereo_temp_buffer, cfg);
	}

	if (did_stereo_temp_buffer) {
		int32_t* const osc_buffer_end = osc_buffer + (num_samples << 1);
		// fold
		if (params.param_final_values[params::LOCAL_FOLD] > 0) {
			const std::span<dsp::q31> stereo{osc_buffer, osc_buffer_end};
			dsp::fixed::Wavefolder{params.param_final_values[params::LOCAL_FOLD]}.render_block(stereo, stereo);
		}
		// Filters
		filter_set.render_stereo({osc_buffer, osc_buffer_end}, args.filter_scratch, *noise_);

		// No clipping
		if (cfg.clipping_amount == 0u) {

			int32_t const* __restrict__ osc_buffer_pos = osc_buffer; // For traversal
			StereoSample* __restrict__ output_sample = reinterpret_cast<StereoSample*>(sound_buffer);
			int32_t overall_osc_amplitude_now = overall_osc_amplitude_last_time;

			do {
				int32_t output_sample_l = *(osc_buffer_pos++);
				int32_t output_sample_r = *(osc_buffer_pos++);

				overall_osc_amplitude_now += overall_oscillator_amplitude_increment;
				if (synth_mode != SynthMode::FM) {
					output_sample_l = multiply_32x32_rshift32_rounded(output_sample_l, overall_osc_amplitude_now) << 1;
					output_sample_r = multiply_32x32_rshift32_rounded(output_sample_r, overall_osc_amplitude_now) << 1;
				}

				// Write to the output buffer, panning or not
				if (do_panning) {
					*output_sample +=
					    deluge::dsp::panned_stereo(output_sample_l, output_sample_r, amplitude_l, amplitude_r);
				}
				else {
					*output_sample += StereoSample{.l = output_sample_l, .r = output_sample_r};
				}

				output_sample++;
			} while (osc_buffer_pos != osc_buffer_end);
		}

		// Yes clipping
		else {

			int32_t const* __restrict__ osc_buffer_pos = osc_buffer; // For traversal
			StereoSample* __restrict__ output_sample = reinterpret_cast<StereoSample*>(sound_buffer);
			int32_t overall_osc_amplitude_now = overall_osc_amplitude_last_time;
			const deluge::effects::SaturationParams saturation{.clipping_amount = cfg.clipping_amount,
			                                                   .variant = deluge::effects::SaturationVariant::VOICE};

			do {
				int32_t output_sample_l = *(osc_buffer_pos++);
				int32_t output_sample_r = *(osc_buffer_pos++);

				overall_osc_amplitude_now += overall_oscillator_amplitude_increment;
				if (synth_mode != SynthMode::FM) {
					output_sample_l = multiply_32x32_rshift32_rounded(output_sample_l, overall_osc_amplitude_now) << 1;
					output_sample_r = multiply_32x32_rshift32_rounded(output_sample_r, overall_osc_amplitude_now) << 1;
				}

				output_sample_l = saturation_.saturate(output_sample_l, 0, saturation);
				output_sample_r = saturation_.saturate(output_sample_r, 1, saturation);

				// Write to the output buffer, panning or not
				if (do_panning) {
					*output_sample +=
					    deluge::dsp::panned_stereo(output_sample_l, output_sample_r, amplitude_l, amplitude_r);
				}
				else {
					*output_sample += StereoSample{.l = output_sample_l, .r = output_sample_r};
				}

				output_sample++;
			} while (osc_buffer_pos != osc_buffer_end);
		}
	}
	else {
		/*
		do {
		    int32_t distanceToGoL = *oscBufferPos - hpfMem;
		    hpfMem += distanceToGoL >> 11;
		    *oscBufferPos -= hpfMem;

		} while (++oscBufferPos != oscBufferEnd);

		oscBufferPos = oscBuffer;
		*/

		int32_t* const osc_buffer_end = osc_buffer + num_samples;
		// wavefolding pre filter
		if (params.param_final_values[params::LOCAL_FOLD] > 0) {
			const std::span<dsp::q31> mono{osc_buffer, osc_buffer_end};
			dsp::fixed::Wavefolder{params.param_final_values[params::LOCAL_FOLD]}.render_block(mono, mono);
		}

		filter_set.render_mono({osc_buffer, osc_buffer_end}, args.filter_scratch, *noise_);

		// No clipping
		if (cfg.clipping_amount == 0u) {
			int32_t const* __restrict__ osc_buffer_pos = osc_buffer; // For traversal
			int32_t* __restrict__ output_sample = sound_buffer;
			int32_t overall_osc_amplitude_now = overall_osc_amplitude_last_time;

			do {
				int32_t output = *osc_buffer_pos;

				if (synth_mode != SynthMode::FM) {
					overall_osc_amplitude_now += overall_oscillator_amplitude_increment;
					output = multiply_32x32_rshift32_rounded(output, overall_osc_amplitude_now) << 1;
				}

				if (sound_rendering_in_stereo) {
					if (do_panning) {
						*(reinterpret_cast<StereoSample*>(output_sample)) +=
						    deluge::dsp::panned_mono(output, amplitude_l, amplitude_r);
					}
					else {
						*(reinterpret_cast<StereoSample*>(output_sample)) += StereoSample::from_mono(output);
					}
					output_sample += 2;
				}
				else {
					*output_sample += output;
					output_sample++;
				}
			} while (++osc_buffer_pos != osc_buffer_end);
		}

		// Yes clipping
		else {
			int32_t const* __restrict__ osc_buffer_pos = osc_buffer; // For traversal
			int32_t* __restrict__ output_sample = sound_buffer;
			int32_t overall_osc_amplitude_now = overall_osc_amplitude_last_time;

			const deluge::effects::SaturationParams saturation{.clipping_amount = cfg.clipping_amount,
			                                                   .variant = deluge::effects::SaturationVariant::VOICE};
			do {
				int32_t output = *osc_buffer_pos;

				if (synth_mode != SynthMode::FM) {
					overall_osc_amplitude_now += overall_oscillator_amplitude_increment;
					output = multiply_32x32_rshift32_rounded(output, overall_osc_amplitude_now) << 1;
				}

				output = saturation_.saturate(output, 0, saturation);

				if (sound_rendering_in_stereo) {
					if (do_panning) {
						*(reinterpret_cast<StereoSample*>(output_sample)) +=
						    deluge::dsp::panned_mono(output, amplitude_l, amplitude_r);
					}
					else {
						*(reinterpret_cast<StereoSample*>(output_sample)) += StereoSample::from_mono(output);
					}
					output_sample += 2;
				}
				else {
					*output_sample += output;
					output_sample++;
				}
			} while (++osc_buffer_pos != osc_buffer_end);
		}
	}

	for (int32_t s = 0; s < kNumSources; s++) {
		source_amplitudes_last_time[s] = source_amplitudes[s];
		source_wave_indexes_last_time[s] = params.param_final_values[params::LOCAL_OSC_A_WAVE_INDEX + s];
	}
	for (int32_t m = 0; m < kNumModulators; m++) {
		fm_engine.modulator_amplitude_last_time[m] = params.param_final_values[params::LOCAL_MODULATOR_0_VOLUME + m];
	}
	overall_osc_amplitude_last_time = overall_osc_amplitude;

	return !unassign_voice_after;
}

// A voice whose every active source is a play-once sample starts its release so that it ends with the sample: once the
// highest-pitched unison part has less left than the release takes.
void Voice::auto_release_if_due(const VoiceConfig& cfg, const VoiceBlock& block, int32_t overall_pitch_adjust) {
	// Not if already releasing, or with no release set, or a noise source
	if (cfg.synth_mode == SynthMode::FM || envelopes[0].stage() >= Envelope::Stage::RELEASE || !has_release_stage()
	    || cfg.noise_volume_contains_something) {
		return;
	}

	uint32_t which_sources_need_attention = 0;

	// Only if all active sources are play-once samples
	for (int32_t s = 0; s < kNumSources; s++) {
		if (!cfg.source_ever_active[s]) {
			continue;
		}

		// Not for anything but a play-once sample (STRETCH is too hard to calculate), nor for one whose loop end is
		// still obeyed
		if (cfg.osc_type[s] != OscType::SAMPLE || cfg.repeat_mode[s] != SampleRepeatMode::ONCE
		    || !block.sample_can_auto_release[s]) {
			return;
		}

		which_sources_need_attention |= (1 << s);
	}

	if (which_sources_need_attention == 0u) [[likely]] {
		return;
	}

	int32_t release_stage_length_samples =
	    static_cast<uint32_t>(8388608) / static_cast<uint32_t>(params.param_final_values[params::LOCAL_ENV_0_RELEASE]);

	int32_t highest_num_samples_left = 0;

	for (int32_t s = 0; s < kNumSources; s++) {
		if ((which_sources_need_attention & (1 << s)) == 0u) {
			continue;
		}

		// Calculated for the last unison part, which has the highest pitch, so ends soonest
		VoiceUnisonPartSource* voice_unison_part_source = &unison_parts[cfg.num_unison - 1].sources[s];
		if (!voice_unison_part_source->active) {
			continue;
		}

		const deluge::synth::SamplePlaybackPort& port = *voice_unison_part_source->sample_source.port();
		int32_t bytes_left = port.bytes_left_to_end();
		int32_t bytes_per_sample = port.bytes_per_frame();

		int32_t release_stage_length_bytes = release_stage_length_samples * bytes_per_sample;

		// The actual read rate: the last unison part's native rate, with the overall and this source's pitch adjust.
		// If the pitch goes crazy-high, this source does not hold the auto-release back
		uint32_t actual_sample_read_rate = voice_unison_part_source->phase_increment_stored_value;
		if (!adjust_pitch(actual_sample_read_rate, overall_pitch_adjust)) {
			continue;
		}
		if (!adjust_pitch(actual_sample_read_rate, params.param_final_values[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
			continue;
		}
		// The read rate leaves time stretching out, which songs made since V3.0 rely on.

		// Scale that according to our resampling rate
		if (actual_sample_read_rate != kMaxSampleValue) {
			release_stage_length_bytes =
			    (static_cast<int64_t>(release_stage_length_bytes) * actual_sample_read_rate) >> 24;
		}

		// Not yet, while this sample has more left than the release takes
		if (bytes_left >= release_stage_length_bytes) {
			return;
		}

		// How many samples are left for this source, worked out only here because it divides
		int32_t samples_left = bytes_left / bytes_per_sample;

		// Scale that according to our resampling rate
		if (actual_sample_read_rate != kMaxSampleValue) {
			samples_left = ((static_cast<int64_t>(samples_left) << 24) / actual_sample_read_rate);
		}

		highest_num_samples_left = std::max(highest_num_samples_left, samples_left);
	}

	// Every source that needed attention says now: release the amplitude envelope
	override_amplitude_envelope_release_rate = 8388608 / highest_num_samples_left;
	if (envelopes[0].is_at_attack_start()) {
		envelopes[0].set_output_level(kQ31Max);
	}
	envelopes[0].unconditional_release();
	stamp_envelope0_stage_entry(block.clock);
}

bool Voice::are_all_unison_parts_inactive(const VoiceConfig& cfg) const {
	// If no noise-source, then it might be time to unassign the voice...
	if (!cfg.noise_volume_contains_something) {

		// See if all unison parts are now inactive
		for (int32_t s = 0; s < kNumSources; s++) {
			if (!cfg.source_ever_active[s]) {
				continue;
			}
			for (int32_t u = 0; u < cfg.num_unison; u++) {
				if (unison_parts[u].sources[s].active) {
					return false;
				}
			}
		}

		// If here, no parts active anymore!
		return true;
	}

	return false;
}

// Returns false if it takes us above 22.05kHz, in which case it doesn't return a valid value
bool Voice::adjust_pitch(uint32_t& phase_increment, int32_t adjustment) {
	if (adjustment != kMaxSampleValue) {
		int32_t output = multiply_32x32_rshift32_rounded(phase_increment, adjustment);
		if (output >= (1 << 23)) {
			return false;
		}
		phase_increment = output << 8;
	}
	return true;
}

// This function renders all unison for a source/oscillator. Amplitude and the incrementing thereof is done
// independently for each unison, despite being the same for all of them, and you might be wondering why this is. Yes in
// the case of an 8-unison sound it'd work out slightly better to apply amplitude to all unison together, but here's why
// this just generally isn't all that advantageous:
//	-	Doing it separately for each unison, as we are, means each unison of *both* sources can just sum itself directly
// onto the buffer for the whole Voice, or in special cases the Sound, 		which only had to be wiped clean once. But
// applying amplitude to the combined unisons of one source would mean we'd need to clear a buffer for that source, then
// render our 		potentially just one unison into that (summing to the buffer's existing contents), then as amplitude
// is applied to the buffer, we'd write the output of that into the 		Voice buffer (summing to its contents too).
// So basically an extra level of summing and clearing would have to happen, in addition to the extra copying which is
// obvious. 	-	And you might be thinking that for oscillator sync this would work well, because it already applies
// amplitude in a separate step after rendering the wave. 		But no - a key part of why this works well as it is is
// that the summing between unisons only happens along with that amplitude-application, after the initial wave render
// has finished chopping around the contents of its buffer. Having that summed into an all-unison buffer would still
// require an additional copying(summing) step, 		and we might as well just apply amplitude while that's
// happening, which is exactly how it is currently.

// Render one unison part of one source, dispatching by oscType to the per-source component and folding the
// per-engine outcomes into one uniform SourceRenderResult. The single uniform dispatch seam a SourceComponent
// interface + the engine registry will build on. (Still a Voice method using public state — this is the seam,
// not yet the de-coupling.)
deluge::synth::SourceRenderResult
Voice::render_source_component(int32_t u, int32_t s, const ControlBlock& control_block, int32_t* osc_buffer,
                               int32_t num_samples, bool stereo_buffer, int32_t source_amplitude,
                               int32_t amplitude_increment, uint32_t phase_increment, bool do_osc_sync,
                               uint32_t* osc_sync_pos, uint32_t* osc_sync_phase_increments,
                               int32_t wave_index_increment, bool stereo_unison, int32_t amplitude_l,
                               int32_t amplitude_r, const VoiceConfig& cfg, const deluge::synth::VoiceBlock& block) {
	using deluge::synth::SourceRenderResult;
	VoiceUnisonPartSource* voice_unison_part_source = &unison_parts[u].sources[s];

	// SAMPLE caching policy: a sample is not cached when a source that can change during the note modulates its pitch.
	// The routing groups each destination's cables, so only the two pitch destinations' cables are scanned. Computed
	// only for SAMPLE, the one source that reads it, and only on a sample's first render.
	bool pitch_mod_disallows_cache = false;
	if (cfg.osc_type[s] == OscType::SAMPLE) {
		const deluge::synth::PatchRouting& routing = control_block.routing;
		const deluge::synth::CableTarget overall_pitch = deluge::synth::CableTarget::param(params::LOCAL_PITCH_ADJUST);
		const deluge::synth::CableTarget source_pitch =
		    deluge::synth::CableTarget::param(params::LOCAL_OSC_A_PITCH_ADJUST + s);
		for (const deluge::synth::RoutedDestination* d = routing.destinations[GLOBALITY_LOCAL];
		     d != nullptr && d->sources != 0 && !pitch_mod_disallows_cache; d++) {
			if (d->target != overall_pitch && d->target != source_pitch) {
				continue;
			}
			for (int32_t c = d->first_cable; c < d->end_cable; c++) {
				const PatchSource from = routing.cables[c].source;
				if (from == PatchSource::ENVELOPE_0 || from == PatchSource::ENVELOPE_1
				    || from == PatchSource::LFO_GLOBAL_1 || from == PatchSource::LFO_LOCAL_1
				    || from == PatchSource::LFO_GLOBAL_2 || from == PatchSource::LFO_LOCAL_2
				    || from == PatchSource::RANDOM
				    || (from == PatchSource::AFTERTOUCH
				        && (params.source_values[util::to_underlying(PatchSource::AFTERTOUCH)] != 0))
				    || (from == PatchSource::SIDECHAIN
				        && (cfg.global_source_values[util::to_underlying(PatchSource::SIDECHAIN)] != 0))) {
					pitch_mod_disallows_cache = true;
					break;
				}
			}
		}
	}

	// All five sources render through the polymorphic synth::Source seam, which owns (for oscillators) the osc-sync
	// freq-too-high SKIP + mono-scratch/stereo-fold, and (per engine) the block render. The app-side adapter just
	// gathers the already-modulated per-block values into the SourceRenderContext.
	uint32_t osc_retrigger_phase = cfg.osc_retrigger_phase[s]; // Yes we might need this even if not doing osc sync.
	uint32_t osc_sync_pos_this_unison = do_osc_sync ? osc_sync_pos[u] : 0;
	uint32_t osc_sync_phase_increments_this_unison = do_osc_sync ? osc_sync_phase_increments[u] : 0;

	// Work out pulse width
	uint32_t pulse_width =
	    static_cast<uint32_t>(lshift_and_saturate<1>(params.param_final_values[params::LOCAL_OSC_A_PHASE_WIDTH + s]));

	synth::SourceRenderContext ctx{
	    .osc_buffer = osc_buffer,
	    .num_samples = num_samples,
	    .stereo_buffer = stereo_buffer,
	    .stereo_unison = stereo_unison,
	    .amplitude_l = amplitude_l,
	    .amplitude_r = amplitude_r,
	    .source_amplitude = source_amplitude,
	    .amplitude_increment = amplitude_increment,
	    .phase_increment = phase_increment,
	    .do_osc_sync = do_osc_sync,
	    .osc_sync_pos = osc_sync_pos_this_unison,
	    .osc_sync_phase_increment = osc_sync_phase_increments_this_unison,
	    .controls = make_block_controls(source_amplitude, amplitude_increment, pulse_width, /*apply_amplitude=*/true,
	                                    do_osc_sync, osc_sync_pos_this_unison, osc_sync_phase_increments_this_unison,
	                                    osc_retrigger_phase, cfg.cpu_direness),
	    .wave_table_data = block.wave_tables[s],
	    .wave_index_last = static_cast<int32_t>(source_wave_indexes_last_time[s]),
	    .wave_index_increment = wave_index_increment,
	    .stereo_scratch = spare_buffers_[2],
	    .stereo_scratch_clear_samples = SSI_TX_BUFFER_NUM_SAMPLES,
	    .sync_scratch = sync_scratch_.data(),
	    .amp_mod_param = params.param_final_values[params::LOCAL_OSC_A_PHASE_WIDTH + s], // DX7 amplitude-mod control
	    .patch_changed = cfg.dx_patch_changed[s],                                        // DX7
	    .midinote = note_code_after_arpeggiation,                                        // DX7
	    .interpolation_buffer_size =
	        interpolation_buffer_size(cfg.interpolation_mode[s], static_cast<int32_t>(phase_increment),
	                                  cfg.cpu_direness), // live input
	    .porta_envelope_pos = porta_envelope_pos,        // sample
	    .priority_rating = static_cast<int32_t>(priority_rating(cfg.voice_priority, block.num_active_voices)), // sample
	    .phase_increment_stored_value = voice_unison_part_source->phase_increment_stored_value, // sample late-start
	    .pitch_mod_disallows_cache = pitch_mod_disallows_cache,                                 // sample caching policy
	    .noise = noise_,                                                                        // live-input hop search
	};

	// sample → sampleSource, DX7 → dx7Source, INPUT_* → inputSource, wavetable → wavetableSource, supersaw →
	// supersawSource, else virtual-analog → vaSource — every source is now a persistent synth::Source; the legacy
	// oscType megaswitch is gone.
	synth::Source* component = nullptr;
	if (cfg.osc_type[s] == OscType::SAMPLE) {
		component = &unison_parts[u].sources[s].sample_source;
	}
	else if (cfg.osc_type[s] == OscType::DX7) {
		component = &unison_parts[u].sources[s].dx7_source;
	}
	else if (cfg.osc_type[s] == OscType::INPUT_L || cfg.osc_type[s] == OscType::INPUT_R
	         || cfg.osc_type[s] == OscType::INPUT_STEREO) {
		component = &unison_parts[u].sources[s].input_source;
	}
	else if (unison_parts[u].sources[s].va_source.has_value()) {
		component = &*unison_parts[u].sources[s].va_source;
	}
	else if (unison_parts[u].sources[s].supersaw_source.has_value()) {
		component = &*unison_parts[u].sources[s].supersaw_source;
	}
	else {
		component = &*unison_parts[u].sources[s].wavetable_source;
	}
	return component->render_source(ctx);
}

bool Voice::render_basic_source(const ControlBlock& control_block, const VoiceBlock& block, int32_t s,
                                int32_t* __restrict__ osc_buffer, int32_t num_samples, bool stereo_buffer,
                                int32_t source_amplitude, int32_t overall_pitch_adjust, bool do_osc_sync,
                                uint32_t* __restrict__ osc_sync_pos, uint32_t* __restrict__ osc_sync_phase_increments,
                                int32_t amplitude_increment, std::span<uint32_t> phase_increments_out,
                                bool get_out_after_phase_increments, int32_t wave_index_increment,
                                const VoiceConfig& cfg) {
	bool unison_part_became_inactive = false;

	for (int32_t u = 0; u < cfg.num_unison; u++) {

		VoiceUnisonPartSource* voice_unison_part_source = &unison_parts[u].sources[s];

		// Samples may become inactive
		if (!voice_unison_part_source->active) {
			continue;
		}

		// The overall pitch adjust, then this source's
		uint32_t phase_increment = voice_unison_part_source->phase_increment_stored_value;
		if (!adjust_pitch(phase_increment, overall_pitch_adjust)
		    || !adjust_pitch(phase_increment, params.param_final_values[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
			// Too high to render
			if (!phase_increments_out.empty()) {
				phase_increments_out[u] = 0;
			}
			continue;
		}

		if (!phase_increments_out.empty()) {
			phase_increments_out[u] = phase_increment;

			if (get_out_after_phase_increments) {
				// Skipped source still advances its phase (it's the osc-sync master) — in whichever engine owns it.
				voice_unison_part_source->advance_osc_phase(phase_increment * num_samples);
				continue;
			}
		}

		bool stereo_unison = (cfg.unison_stereo_spread != 0u) && cfg.num_unison > 1 && stereo_buffer;
		const deluge::dsp::fixed::PanAmplitudes unison_pan_amplitudes =
		    deluge::dsp::fixed::pan_amplitudes(stereo_unison ? cfg.unison_pan[u] : 0);
		int32_t amplitude_l = unison_pan_amplitudes.left;
		int32_t amplitude_r = unison_pan_amplitudes.right;
		// used if mono source but stereoUnison active

		auto result = render_source_component(u, s, control_block, osc_buffer, num_samples, stereo_buffer,
		                                      source_amplitude, amplitude_increment, phase_increment, do_osc_sync,
		                                      osc_sync_pos, osc_sync_phase_increments, wave_index_increment,
		                                      stereo_unison, amplitude_l, amplitude_r, cfg, block);
		if (result == deluge::synth::SourceRenderResult::UNASSIGN) {
			unison_part_became_inactive = true;
			voice_unison_part_source->unassign(false);
		}
	}
	return unison_part_became_inactive;
}

bool Voice::do_fast_release(uint32_t release_increment, deluge::synth::VoiceClock clock) {
	if (done_first_render) {
		envelopes[0].unconditional_release(Envelope::Stage::FAST_RELEASE, release_increment);
		stamp_envelope0_stage_entry(clock);
		return true;
	}

	// Or if first render not done yet, we actually don't want to hear anything at all, so just unassign it

	return false;
}

bool Voice::speed_up_release(deluge::synth::VoiceClock clock, uint32_t min_fast_release_increment) {
	if (!done_first_render) {
		return false;
	}

	auto current_stage = envelopes[0].stage();
	if (current_stage == Envelope::Stage::RELEASE) {
		auto current_release = params.param_final_values[params::LOCAL_ENV_0_RELEASE];
		envelopes[0].unconditional_release(Envelope::Stage::FAST_RELEASE,
		                                   std::max<uint32_t>(current_release * 2, min_fast_release_increment));
	}
	else if (current_stage == Envelope::Stage::FAST_RELEASE) {
		auto current_release = envelopes[0].fast_release_rate();
		envelopes[0].unconditional_release(Envelope::Stage::FAST_RELEASE,
		                                   std::max<uint32_t>(current_release * 2, min_fast_release_increment));
	}
	// Cull-driven release must not inherit a patch's long musical release, or culled voices can stack up.
	else {
		envelopes[0].unconditional_release(Envelope::Stage::FAST_RELEASE, min_fast_release_increment);
	}
	stamp_envelope0_stage_entry(clock);
	return true;
}

bool Voice::do_immediate_release(deluge::synth::VoiceClock clock) {
	if (done_first_render) {
		envelopes[0].unconditional_off();
		stamp_envelope0_stage_entry(clock);
		return true;
	}

	// Or if first render not done yet, we actually don't want to hear anything at all, so just unassign it
	return false;
}

bool Voice::has_release_stage() {
	return (params.param_final_values[params::LOCAL_ENV_0_RELEASE] <= 18359);
}

// if more added, need to change the shifts around
static_assert(Envelope::kNumStages < 8, "Too many envelope stages");
static_assert(kNumVoicePriorities < 4, "Too many priority options");

// Higher numbers are lower priority. 1 is top priority. Will never return 0, because the voice clock starts at 1
uint32_t Voice::priority_rating(VoicePriority voice_priority, size_t num_active_voices) const {
	return
	    // Bits 30-31 - manual priority setting
	    (static_cast<uint32_t>(3 - util::to_underlying(voice_priority)) << 30)

	    // Bits 27-29 - how many voices that Sound has
	    // - that one really does need to go above state, otherwise "once" samples can still cut out synth drones.
	    // In a perfect world, culling for the purpose of "soliciting" a Voice would also count the new Voice being
	    // solicited, preferring to cut out that same Sound's old, say, one Voice, than another Sound's only Voice
	    + (static_cast<uint32_t>(std::min<size_t>(num_active_voices, 7)) << 27)

	    // Bits 24-26 - envelope state
	    + (static_cast<uint32_t>(envelopes[0].stage()) << 24)

	    // Bits  0-23 - time entered
	    + ((-envelope0_time_entered) & kTimeEnteredMask);
}

void Voice::stamp_envelope0_stage_entry(deluge::synth::VoiceClock clock) {
	if (envelopes[0].take_stage_changed()) {
		envelope0_time_entered = clock.tick();
	}
}

void Voice::reset_envelope0_time_entered(deluge::synth::VoiceClock clock) {
	if (envelopes[0].stage() == Envelope::Stage::SUSTAIN) {
		envelope0_time_entered = clock.tick();
	}
}

} // namespace deluge::synth
