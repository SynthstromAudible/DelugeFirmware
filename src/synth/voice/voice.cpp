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
#include "definitions_cxx.hpp"
#include "dsp-next/fixed/interpolation/table_interpolation.h" // get_decay4
#include "dsp-next/fixed/shaping/wavefolder.h"
#include "dsp-next/fixed/vca/pan.h"
#include "synth/engines/pitch.h"
#include "util/lookuptables/lookuptables.h"
#include "util/param_functions.h"

#include "dsp-next/fixed/tables/decay_small.h"
#include "util/misc.h"
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <utility>

using namespace deluge;
using deluge::dsp::fixed::get_decay4;
namespace params = deluge::modulation::params;

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

} // namespace

int32_t Voice::combine_expression_values(int32_t synth_level_value, int32_t expression_dimension) const {
	int32_t voice_level_value = this->local_expression_source_values_before_smoothing_[expression_dimension];
	int32_t combined_value = (synth_level_value >> 1) + (voice_level_value >> 1);
	return lshift_and_saturate<1>(combined_value);
}

Voice::Voice() : patcher_(kPatcherConfigForVoice, params_.source_values, params_.param_final_values) {
}

// Returns false if the note cannot play and the voice must be unassigned again
bool Voice::note_on(const deluge::synth::NoteSetup& setup, const SoundSnapshot& snapshot,
                    const deluge::synth::NoteOnEvent& event, deluge::synth::VoiceClock clock, size_t num_active_voices,
                    dsp::fixed::Cong& noise) {
	noise_ = &noise;
	input_characteristics_[util::to_underlying(MIDICharacteristic::NOTE)] = event.note_before_arp;
	input_characteristics_[util::to_underlying(MIDICharacteristic::CHANNEL)] = event.midi_channel;
	note_code_after_arpeggiation_ = event.note_after_arp;
	override_amplitude_envelope_release_rate_ = 0;

	if (event.note_after_arp >= 128) {
		params_.source_values[util::to_underlying(PatchSource::NOTE)] = kQ31Max;
	}
	else if (event.note_after_arp <= 0) {
		params_.source_values[util::to_underlying(PatchSource::NOTE)] = kQ31Min;
	}
	else {
		params_.source_values[util::to_underlying(PatchSource::NOTE)] = (event.note_after_arp - 64) * 33554432;
	}

	// Setup "half-baked" envelope output values. These need to exist before we do the initial patching below - and it's
	// only after that that we can render the "actual" envelope output values, taking their own input patching into
	// account.
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		envelopes_[e].set_output_level(setup.attack_is_zero[e] ? kQ31Max : 0);
	}

	// Setup and render local LFO
	lfo2_.set_local_initial_phase(snapshot.lfo_config[LFO2_ID]);
	params_.source_values[util::to_underlying(PatchSource::LFO_LOCAL_1)] =
	    lfo2_.render(0, snapshot.lfo_config[LFO2_ID], 0, *noise_);
	lfo4_.set_local_initial_phase(snapshot.lfo_config[LFO4_ID]);
	params_.source_values[util::to_underlying(PatchSource::LFO_LOCAL_2)] =
	    lfo4_.render(0, snapshot.lfo_config[LFO4_ID], 0, *noise_);

	// Setup some sources which won't change for the duration of this note
	params_.source_values[util::to_underlying(PatchSource::VELOCITY)] =
	    (event.velocity == 128) ? kQ31Max : (static_cast<int32_t>(event.velocity) - 64) * 33554432;

	// "Random" source
	params_.source_values[util::to_underlying(PatchSource::RANDOM)] = next_noise();

	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		local_expression_source_values_before_smoothing_[m] = event.mpe[m] << 16;
		params_.source_values[util::to_underlying(PatchSource::X) + m] =
		    combine_expression_values(setup.monophonic_expression[m], m);
	}

	if (event.reset_envelopes) {
		source_levels_.amplitudes_last_time.fill(0);
		engines_.fm().reset_levels();
		overall_osc_amplitude_last_time_ = 0;
		done_first_render_ = false;
		filter_set_.reset();

		output_saturation_.reset();
	}

	if (setup.porta) [[unlikely]] {
		setup_porta(setup.last_note_code);
	}
	else {
		porta_envelope_pos_ = kNoPorta;
	}

	// Patch all sources to exclusive params, to give them an initial value. Exclusive params (params with just 1
	// source) aren't continuously recalculated, so they need that initial value. Remember, calculating that initial
	// value also takes into account the "preset value". This probably isn't strictly necessary for sources which we
	// know will be constantly changing, because that would make patching constantly calculate too. But that's only
	// really the envelopes, plus the LFOs (just the local one?) if they're not square
	std::ranges::copy(setup.global_source_values, std::begin(params_.source_values));
	patcher_.perform_initial_patching(setup.initial_patching);

	// Setup and render envelopes - again. Because they're local params (since mid-late 2017), we really need to render
	// them *after* initial patching is performed.
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		int32_t attack = params_.param_final_values[params::LOCAL_ENV_0_ATTACK + e];
		int32_t sustain = params_.param_final_values[params::LOCAL_ENV_0_SUSTAIN + e];
		bool directly_to_decay = (attack > 245632); // Skip a (near-)instant attack straight to decay.
		params_.source_values[util::to_underlying(PatchSource::ENVELOPE_0) + e] =
		    envelopes_[e].note_on(directly_to_decay, sustain);
	}
	stamp_envelope0_stage_entry(clock);

	if (event.reset_envelopes) {
		for (int32_t s = 0; s < kNumSources; s++) {
			source_levels_.wave_indexes_last_time[s] = params_.param_final_values[params::LOCAL_OSC_A_WAVE_INDEX + s];
		}
	}

	// The engine marks each source active where it can sound, works out the phase increments and starts the sources
	const EngineNote note{
	    .parts = unison_parts,
	    .setup = &setup,
	    .snapshot = &snapshot,
	    .note_code = note_code_after_arpeggiation_,
	    .velocity = event.velocity,
	    .samples_late = event.samples_late,
	    .priority_rating = static_cast<int32_t>(priority_rating(snapshot.voice_priority, num_active_voices)),
	    .reset = event.reset_envelopes,
	    .noise = noise_,
	};
	if (!engines_.for_mode(snapshot.synth_mode).note_on(note)) {
		return false;
	}

	previously_ignored_note_off_ = false;
	expression_sources_currently_smoothing_.reset();
	source_levels_.filter_gain_last_time = 0;

	return true;
}

void Voice::expression_event_immediate(int32_t monophonic_value, int32_t voice_level_value, int32_t s) {
	int32_t expression_dimension = s - util::to_underlying(PatchSource::X);
	local_expression_source_values_before_smoothing_[expression_dimension] = voice_level_value;
	expression_sources_final_value_changed_[expression_dimension] = true;

	params_.source_values[s] = combine_expression_values(monophonic_value, expression_dimension);
}

void Voice::expression_event_smooth(int32_t new_value, int32_t s) {
	int32_t expression_dimension = s - util::to_underlying(PatchSource::X);
	local_expression_source_values_before_smoothing_[expression_dimension] = new_value;
	expression_sources_currently_smoothing_[expression_dimension] = true;
}

void Voice::change_note(const deluge::synth::NoteSetup& setup, const SoundSnapshot& snapshot,
                        int32_t note_before_arpeggiation, int32_t note_after_arpeggiation, int32_t midi_channel,
                        std::span<const int16_t, kNumExpressionDimensions> mpe) {
	input_characteristics_[util::to_underlying(MIDICharacteristic::NOTE)] = note_before_arpeggiation;
	input_characteristics_[util::to_underlying(MIDICharacteristic::CHANNEL)] = midi_channel;
	note_code_after_arpeggiation_ = note_after_arpeggiation;

	// We definitely want to go to these values smoothly. Probably wish it was even smoother... Actually nah this sounds
	// / feels great!
	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		local_expression_source_values_before_smoothing_[m] = mpe[m] << 16;
		// TODO: what if there's just channel aftertouch, and it's still held down...
	}
	expression_sources_currently_smoothing_ = 0b111;

	if (setup.porta) {
		setup_porta(setup.last_note_code);
	}

	calculate_phase_increments(setup, snapshot);
}

void Voice::setup_porta(int32_t last_note_code) {
	porta_envelope_pos_ = 0;
	int32_t semitone_adjustment = last_note_code - note_code_after_arpeggiation_;

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

	porta_envelope_max_amplitude_ = phase_increment - kMaxSampleValue;
}

void Voice::randomize_osc_phases(const SoundSnapshot& snapshot, dsp::fixed::Cong& noise) {
	noise_ = &noise;
	for (int32_t u = 0; std::cmp_less(u, snapshot.num_unison); u++) {
		for (int32_t s = 0; s < kNumSources; s++) {
			unison_parts[u].slots[s].osc_pos = next_noise();
			// TODO: we should do sample play pos, too
		}
		engines_.for_mode(snapshot.synth_mode).randomize_part(u, *noise_);
	}
}

void Voice::calculate_phase_increments(const deluge::synth::NoteSetup& setup, const SoundSnapshot& snapshot) {
	engines_.for_mode(snapshot.synth_mode)
	    .calculate_phase_increments({
	        .parts = unison_parts,
	        .setup = &setup,
	        .snapshot = &snapshot,
	        .note_code = note_code_after_arpeggiation_,
	    });
}

void Voice::note_off(const deluge::synth::ReleaseInfo& release, bool allow_release_stage,
                     deluge::synth::VoiceClock clock) {

	// Only do it if note-offs are meant to be processed for this sound. Otherwise ignore it.
	if (release.note_tails_allowed) {

		// If no release-stage, we'll stop as soon as we can
		if (!allow_release_stage || !has_release_stage()) {
			envelopes_[0].unconditional_release(Envelope::Stage::FAST_RELEASE);
		}

		// Or, do the release-stage
		else {
			envelopes_[0].note_off(release.envelope_has_sustain[0]);

			// Only start releasing envelope 2 if its release isn't at the maximum. LOCAL_ENV_1_RELEASE is a local
			// param, so it is read from the voice's own final values.
			if (params_.param_final_values[params::LOCAL_ENV_1_RELEASE] >= 9) {
				envelopes_[1].note_off(release.envelope_has_sustain[1]);
			}
		}
		stamp_envelope0_stage_entry(clock);
	}

	else {
		previously_ignored_note_off_ = true;
	}

	// The parts' sources' releases: a sample's loop end, a DX7's key-up (none in FM)
	engines_.for_mode(release.synth_mode)
	    .release(unison_parts, release,
	             static_cast<int32_t>(priority_rating(release.voice_priority, release.num_active_voices)));
}

uint32_t Voice::get_local_lfo_phase_increment(const LFOConfig& config, deluge::modulation::params::Local param,
                                              uint32_t synced_phase_increment) {
	if (config.sync_level == deluge::plugin::SyncLevel::NONE) {
		return params_.param_final_values[param];
	}
	return synced_phase_increment;
}

// Returns false if became inactive and needs unassigning
[[gnu::hot]] bool Voice::render(const ControlBlock& control_block, const VoiceConfig& cfg, const VoiceBlock& block,
                                const VoiceRenderArgs& args) {
	spare_buffers_ = args.spare_buffers; // the render helpers read these
	sync_scratch_ = args.sync_scratch;
	noise_ = args.noise;
	int32_t* sound_buffer = args.buffer;
	const int32_t num_samples = args.num_samples;
	const bool sound_rendering_in_stereo = args.sound_in_stereo;
	const bool applying_pan_at_voice_level = args.pan_at_voice_level;
	const int32_t external_pitch_adjust = args.pitch_adjust;

	bool did_stereo_temp_buffer = false;

	bool unassign_voice_after = render_modulation(control_block, cfg, block, num_samples, args.sources_changed);

	const int32_t overall_pitch_adjust = pitch_adjust(cfg, num_samples, external_pitch_adjust);

	// Auto-release for a play-once sample. Despite this being envelope-related, meaning we'd ideally prefer to do it
	// before patching, we can only do it after because we need to know pitch
	auto_release_if_due(cfg, block, overall_pitch_adjust);

	if (!done_first_render_ && params_.param_final_values[params::LOCAL_ENV_0_ATTACK] > 245632) {
		engines_.fm().set_levels(modulator_volumes());
	}

	// Apply envelope 0 to volume. This takes effect as a cut only; when the envelope is at max height, volume is
	// unaffected. Important that we use lshift_and_saturate here - otherwise, number can overflow if combining high
	// velocity patching with big LFO
	int32_t overall_osc_amplitude = lshift_and_saturate<2>(multiply_32x32_rshift32(
	    params_.param_final_values[params::LOCAL_VOLUME],
	    (params_.source_values[util::to_underlying(PatchSource::ENVELOPE_0)] >> 1) + 1073741824));

	// This is the gain which gets applied to compensate for any change in gain that the filter is going to cause
	int32_t filter_gain = 0;

	// Prepare the filters. Level adjustment for unison happens before the filters, so the gain going in is neutral.
	const bool allow_oversampling = cfg.cpu_direness < 14;
	filter_gain = filter_set_.configure(cfg.volume_neutral_value_for_unison << 1,
	                                    {
	                                        .frequency = params_.param_final_values[params::LOCAL_LPF_FREQ],
	                                        .resonance = params_.param_final_values[params::LOCAL_LPF_RESONANCE],
	                                        .morph = params_.param_final_values[params::LOCAL_LPF_MORPH],
	                                        .allow_oversampling = allow_oversampling,
	                                    },
	                                    cfg.low_pass,
	                                    {
	                                        .frequency = params_.param_final_values[params::LOCAL_HPF_FREQ],
	                                        .resonance = params_.param_final_values[params::LOCAL_HPF_RESONANCE],
	                                        .morph = params_.param_final_values[params::LOCAL_HPF_MORPH],
	                                        .allow_oversampling = allow_oversampling,
	                                    },
	                                    cfg.high_pass, cfg.filter_route);

	SynthMode synth_mode = cfg.synth_mode;

	int32_t overall_oscillator_amplitude_increment = 0;
	const bool first_render = !done_first_render_;

	if (synth_mode == SynthMode::FM) {
		// FM works the overall amplitude, compensated for unison, into each carrier's, saving a multiplication per
		// sample at the end
		overall_osc_amplitude =
		    multiply_32x32_rshift32_rounded(overall_osc_amplitude, cfg.volume_neutral_value_for_unison) << 3;
	}
	else {
		if (first_render && params_.param_final_values[params::LOCAL_ENV_0_ATTACK] > 245632) {
			overall_osc_amplitude_last_time_ = overall_osc_amplitude;
		}
		overall_oscillator_amplitude_increment =
		    (overall_osc_amplitude - overall_osc_amplitude_last_time_) / num_samples;
	}

	done_first_render_ = true;

	// whether stereo unison actually is active. if stereo is being vetoed from higher up, don't do it.
	bool stereo_unison = (cfg.unison_stereo_spread != 0u) && cfg.num_unison > 1 && sound_rendering_in_stereo;

	const VoicePan pan = voice_pan(cfg, applying_pan_at_voice_level);

	// two first indicies are reserved in case we need stereo for unison spread
	int32_t* osc_buffer = spare_buffers_[0];

	EngineBlock engine_block{
	    .osc_buffer = osc_buffer,
	    .num_samples = num_samples,
	    .parts = unison_parts,
	    .levels = &source_levels_,
	    .cfg = &cfg,
	    .voice_block = &block,
	    .spare_buffers = spare_buffers_,
	    .sync_scratch = sync_scratch_,
	    .noise = noise_,
	    .dual = {},
	    .fm = {},
	};
	if (synth_mode == SynthMode::FM) {
		engine_block.fm =
		    fm_controls(overall_osc_amplitude, filter_gain, overall_pitch_adjust, first_render, stereo_unison);
	}
	else {
		engine_block.dual = dual_source_controls(control_block, cfg, block, filter_gain, overall_pitch_adjust,
		                                         first_render, stereo_unison);
	}
	const EngineRenderResult result = engines_.for_mode(synth_mode).render(engine_block);
	did_stereo_temp_buffer = result.did_stereo_buffer;

	if (result.all_parts_ended && end_with_parts()) {
		unassign_voice_after = true;
	}

	const VoiceOutput output{
	    .buffer = osc_buffer,
	    .num_samples = num_samples,
	    .sound_buffer = sound_buffer,
	    .amplitude_start = overall_osc_amplitude_last_time_,
	    .amplitude_increment = overall_oscillator_amplitude_increment,
	    .fm = synth_mode == SynthMode::FM,
	    .pan = pan,
	    .sound_in_stereo = sound_rendering_in_stereo,
	    .clipping_amount = cfg.clipping_amount,
	};
	if (did_stereo_temp_buffer) {
		const std::span<dsp::q31> stereo{osc_buffer, osc_buffer + (num_samples << 1)};
		if (params_.param_final_values[params::LOCAL_FOLD] > 0) {
			dsp::fixed::Wavefolder{params_.param_final_values[params::LOCAL_FOLD]}.render_block(stereo, stereo);
		}
		filter_set_.render_stereo(stereo, args.filter_scratch, *noise_);
		if (cfg.clipping_amount == 0u) {
			write_stereo<false>(output);
		}
		else {
			write_stereo<true>(output);
		}
	}
	else {
		const std::span<dsp::q31> mono{osc_buffer, osc_buffer + num_samples};
		// wavefolding pre filter
		if (params_.param_final_values[params::LOCAL_FOLD] > 0) {
			dsp::fixed::Wavefolder{params_.param_final_values[params::LOCAL_FOLD]}.render_block(mono, mono);
		}
		filter_set_.render_mono(mono, args.filter_scratch, *noise_);
		if (cfg.clipping_amount == 0u) {
			write_mono<false>(output);
		}
		else {
			write_mono<true>(output);
		}
	}

	engines_.fm().set_levels(modulator_volumes());
	overall_osc_amplitude_last_time_ = overall_osc_amplitude;

	return !unassign_voice_after;
}

bool Voice::render_modulation(const ControlBlock& control_block, const VoiceConfig& cfg, const VoiceBlock& block,
                              int32_t num_samples, uint32_t sources_changed) {
	// Do envelopes - if they're patched to something (always do the first one though)
	for (int32_t e = 0; e < kNumEnvelopes; e++) {
		if (e == 0
		    || ((cfg.local_sources_patched_to_anything & (1 << (util::to_underlying(PatchSource::ENVELOPE_0) + e)))
		        != 0u)) {
			int32_t old = params_.source_values[util::to_underlying(PatchSource::ENVELOPE_0) + e];
			int32_t release = params_.param_final_values[params::LOCAL_ENV_0_RELEASE + e];
			if (e == 0 && (override_amplitude_envelope_release_rate_ != 0)) {
				release = override_amplitude_envelope_release_rate_;
			}
			params_.source_values[util::to_underlying(PatchSource::ENVELOPE_0) + e] =
			    envelopes_[e].render(num_samples, params_.param_final_values[params::LOCAL_ENV_0_ATTACK + e],
			                         params_.param_final_values[params::LOCAL_ENV_0_DECAY + e],
			                         params_.param_final_values[params::LOCAL_ENV_0_SUSTAIN + e], release,
			                         deluge::dsp::fixed::tables::kDecaySmall8.data());
			const auto any_change =
			    static_cast<uint32_t>(old != params_.source_values[util::to_underlying(PatchSource::ENVELOPE_0) + e]);
			sources_changed |= any_change << (util::to_underlying(PatchSource::ENVELOPE_0) + e);
		}
	}
	stamp_envelope0_stage_entry(block.clock);

	const bool envelope_ended = (envelopes_[0].stage() == Envelope::Stage::OFF)
	                            || (envelopes_[0].stage() > Envelope::Stage::DECAY
	                                && params_.source_values[util::to_underlying(PatchSource::ENVELOPE_0)]
	                                       == std::numeric_limits<int32_t>::min());
	// Local LFO
	if ((cfg.local_sources_patched_to_anything & (1 << util::to_underlying(PatchSource::LFO_LOCAL_1))) != 0u) {
		int32_t old = params_.source_values[util::to_underlying(PatchSource::LFO_LOCAL_1)];
		// TODO: Same as with LFO1, there should be no reason to recompute the phase increment
		// for every sample.
		params_.source_values[util::to_underlying(PatchSource::LFO_LOCAL_1)] =
		    lfo2_.render(num_samples, cfg.lfo2_config,
		                 get_local_lfo_phase_increment(cfg.lfo2_config, params::LOCAL_LFO_LOCAL_FREQ_1,
		                                               cfg.lfo2_synced_phase_increment),
		                 *noise_);
		const auto any_change =
		    static_cast<uint32_t>(old != params_.source_values[util::to_underlying(PatchSource::LFO_LOCAL_1)]);
		sources_changed |= any_change << util::to_underlying(PatchSource::LFO_LOCAL_1);
	}
	if ((cfg.local_sources_patched_to_anything & (1 << util::to_underlying(PatchSource::LFO_LOCAL_2))) != 0u) {
		int32_t old = params_.source_values[util::to_underlying(PatchSource::LFO_LOCAL_2)];
		// TODO: Same as with LFO1, there should be no reason to recompute the phase increment
		// for every sample.
		params_.source_values[util::to_underlying(PatchSource::LFO_LOCAL_2)] =
		    lfo4_.render(num_samples, cfg.lfo4_config,
		                 get_local_lfo_phase_increment(cfg.lfo4_config, params::LOCAL_LFO_LOCAL_FREQ_2,
		                                               cfg.lfo4_synced_phase_increment),
		                 *noise_);
		const auto any_change =
		    static_cast<uint32_t>(old != params_.source_values[util::to_underlying(PatchSource::LFO_LOCAL_2)]);
		sources_changed |= any_change << util::to_underlying(PatchSource::LFO_LOCAL_2);
	}

	// MPE params

	smooth_expression(cfg, num_samples);

	sources_changed |= expression_sources_final_value_changed_.to_ulong() << util::to_underlying(PatchSource::X);

	expression_sources_final_value_changed_.reset();

	// Patch all the sources to their parameters
	if (sources_changed != 0u) {
		for (int32_t s = 0; std::cmp_less(s, util::to_underlying(kFirstLocalSource)); s++) {
			params_.source_values[s] = cfg.global_source_values[s];
		}
		patcher_.perform_patching(sources_changed, control_block);
	}
	return envelope_ended;
}

int32_t Voice::pitch_adjust(const VoiceConfig& cfg, int32_t num_samples, int32_t external_pitch_adjust) {
	// Sort out pitch
	int32_t overall_pitch_adjust = params_.param_final_values[params::LOCAL_PITCH_ADJUST];

	// Pitch adjust from "external" - e.g. the Kit
	if (external_pitch_adjust != kMaxSampleValue) {
		// Limited to keep the shift below in range
		const int32_t output =
		    std::min(multiply_32x32_rshift32_rounded(overall_pitch_adjust, external_pitch_adjust), 8388607);
		overall_pitch_adjust = output << 8;
	}

	// Pitch adjust via MIDI pitch bend and MPE
	int32_t total_bend_amount = // Comes out as a 32-bit number representing a range of +-192 semitones
	    (cfg.monophonic_expression_values[0] / 192) * cfg.bend_ranges[BEND_RANGE_MAIN]
	    + (local_expression_source_values_before_smoothing_[0] / 192) * cfg.bend_ranges[BEND_RANGE_FINGER_LEVEL];

	overall_pitch_adjust = getExp(overall_pitch_adjust, total_bend_amount >> 1);

	// Porta
	if (porta_envelope_pos_ < 8388608) {
		int32_t env_value = get_decay4(porta_envelope_pos_, 23);
		int32_t pitch_adjustment_here =
		    kMaxSampleValue + (multiply_32x32_rshift32_rounded(env_value, porta_envelope_max_amplitude_) << 1);

		// Limited to keep the shift below from overflowing
		const int32_t a =
		    std::min(multiply_32x32_rshift32_rounded(overall_pitch_adjust, pitch_adjustment_here), 8388607);
		overall_pitch_adjust = a << 8;

		// Move envelope on. Using the "release rate" lookup table gives by far the best range of speed values
		int32_t envelope_speed = lookupReleaseRate(cableToExpParamShortcut(cfg.portamento)) >> 13;
		porta_envelope_pos_ += envelope_speed * num_samples;
	}
	return overall_pitch_adjust;
}

Voice::VoicePan Voice::voice_pan(const VoiceConfig& cfg, bool applying_pan_at_voice_level) const {
	// Even if first rendering into a local Voice buffer, we'll very often still just do panning at the Sound level
	if (!applying_pan_at_voice_level || !cfg.engine_rendering_in_stereo) {
		return {};
	}
	const int32_t pan = params_.param_final_values[params::LOCAL_PAN];
	const deluge::dsp::fixed::PanAmplitudes amplitudes = deluge::dsp::fixed::pan_amplitudes(pan);
	return {.do_panning = pan != 0, .amplitude_l = amplitudes.left, .amplitude_r = amplitudes.right};
}

template <bool kClipping>
void Voice::write_stereo(const VoiceOutput& output) {
	[[maybe_unused]] const deluge::effects::SaturationParams saturation{
	    .clipping_amount = output.clipping_amount, .variant = deluge::effects::SaturationVariant::VOICE};
	int32_t const* __restrict__ osc_buffer_pos = output.buffer;
	int32_t const* const osc_buffer_end = output.buffer + (output.num_samples << 1);
	auto* __restrict__ output_sample = reinterpret_cast<StereoSample*>(output.sound_buffer);
	int32_t overall_osc_amplitude_now = output.amplitude_start;

	do {
		int32_t output_sample_l = *(osc_buffer_pos++);
		int32_t output_sample_r = *(osc_buffer_pos++);

		overall_osc_amplitude_now += output.amplitude_increment;
		if (!output.fm) {
			output_sample_l = multiply_32x32_rshift32_rounded(output_sample_l, overall_osc_amplitude_now) << 1;
			output_sample_r = multiply_32x32_rshift32_rounded(output_sample_r, overall_osc_amplitude_now) << 1;
		}

		if constexpr (kClipping) {
			output_sample_l = output_saturation_.saturate(output_sample_l, 0, saturation);
			output_sample_r = output_saturation_.saturate(output_sample_r, 1, saturation);
		}

		// Write to the output buffer, panning or not
		if (output.pan.do_panning) {
			*output_sample += deluge::dsp::panned_stereo(output_sample_l, output_sample_r, output.pan.amplitude_l,
			                                             output.pan.amplitude_r);
		}
		else {
			*output_sample += StereoSample{.l = output_sample_l, .r = output_sample_r};
		}

		output_sample++;
	} while (osc_buffer_pos != osc_buffer_end);
}

template <bool kClipping>
void Voice::write_mono(const VoiceOutput& output) {
	[[maybe_unused]] const deluge::effects::SaturationParams saturation{
	    .clipping_amount = output.clipping_amount, .variant = deluge::effects::SaturationVariant::VOICE};
	int32_t const* __restrict__ osc_buffer_pos = output.buffer;
	int32_t const* const osc_buffer_end = output.buffer + output.num_samples;
	int32_t* __restrict__ output_sample = output.sound_buffer;
	int32_t overall_osc_amplitude_now = output.amplitude_start;

	do {
		int32_t sample = *osc_buffer_pos;

		if (!output.fm) {
			overall_osc_amplitude_now += output.amplitude_increment;
			sample = multiply_32x32_rshift32_rounded(sample, overall_osc_amplitude_now) << 1;
		}

		if constexpr (kClipping) {
			sample = output_saturation_.saturate(sample, 0, saturation);
		}

		if (output.sound_in_stereo) {
			if (output.pan.do_panning) {
				*(reinterpret_cast<StereoSample*>(output_sample)) +=
				    deluge::dsp::panned_mono(sample, output.pan.amplitude_l, output.pan.amplitude_r);
			}
			else {
				*(reinterpret_cast<StereoSample*>(output_sample)) += StereoSample::from_mono(sample);
			}
			output_sample += 2;
		}
		else {
			*output_sample += sample;
			output_sample++;
		}
	} while (++osc_buffer_pos != osc_buffer_end);
}

void Voice::smooth_expression(const VoiceConfig& cfg, int32_t num_samples) {
	expression_sources_currently_smoothing_ |= cfg.expression_sources_changed_at_synth_level;

	if (expression_sources_currently_smoothing_.any()) {
		expression_sources_final_value_changed_ |= expression_sources_currently_smoothing_;

		for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
			if (expression_sources_currently_smoothing_[i]) {

				int32_t target_value = combine_expression_values(cfg.monophonic_expression_values[i], i);

				int32_t diff =
				    (target_value >> 8) - (params_.source_values[i + util::to_underlying(PatchSource::X)] >> 8);

				if (diff == 0) {
					expression_sources_currently_smoothing_[i] = false;
				}
				else {
					int32_t amount_to_add = diff * num_samples / 4;
					params_.source_values[i + util::to_underlying(PatchSource::X)] += amount_to_add;
				}
			}
		}
	}
}

uint32_t Voice::play_once_sample_sources(const VoiceConfig& cfg, const VoiceBlock& block) {
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
			return 0;
		}

		which_sources_need_attention |= (1 << s);
	}
	return which_sources_need_attention;
}

bool Voice::end_with_parts() {
	// Without filters the voice can unassign at once; with them it fast-releases first, to avoid a click
	if (!filter_set_.is_on()) {
		return true;
	}
	if (envelopes_[0].stage() < Envelope::Stage::FAST_RELEASE) {
		envelopes_[0].unconditional_release(Envelope::Stage::FAST_RELEASE);
	}
	return false;
}

// A voice whose every active source is a play-once sample starts its release so that it ends with the sample: once the
// highest-pitched unison part has less left than the release takes.
void Voice::auto_release_if_due(const VoiceConfig& cfg, const VoiceBlock& block, int32_t overall_pitch_adjust) {
	// Not if already releasing, or with no release set, or a noise source
	if (cfg.synth_mode == SynthMode::FM || envelopes_[0].stage() >= Envelope::Stage::RELEASE || !has_release_stage()
	    || cfg.noise_volume_contains_something) {
		return;
	}

	const uint32_t which_sources_need_attention = play_once_sample_sources(cfg, block);
	if (which_sources_need_attention == 0u) [[likely]] {
		return;
	}

	const auto release_stage_length_samples =
	    static_cast<int32_t>(static_cast<uint32_t>(8388608)
	                         / static_cast<uint32_t>(params_.param_final_values[params::LOCAL_ENV_0_RELEASE]));

	int32_t highest_num_samples_left = 0;

	for (int32_t s = 0; s < kNumSources; s++) {
		if ((which_sources_need_attention & (1 << s)) == 0u) {
			continue;
		}

		// Calculated for the last unison part, which has the highest pitch, so ends soonest
		SourceSlot* slot = &unison_parts[cfg.num_unison - 1].slots[s];
		if (!slot->active) {
			continue;
		}

		const deluge::synth::SamplePlaybackPort& port = *slot->sample().port();
		int32_t bytes_left = port.bytes_left_to_end();
		int32_t bytes_per_sample = port.bytes_per_frame();

		int32_t release_stage_length_bytes = release_stage_length_samples * bytes_per_sample;

		// The actual read rate: the last unison part's native rate, with the overall and this source's pitch adjust.
		// If the pitch goes crazy-high, this source does not hold the auto-release back
		uint32_t actual_sample_read_rate = slot->phase_increment_stored_value;
		if (!adjust_pitch(actual_sample_read_rate, overall_pitch_adjust)) {
			continue;
		}
		if (!adjust_pitch(actual_sample_read_rate, params_.param_final_values[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
			continue;
		}
		// The read rate leaves time stretching out, which songs made since V3.0 rely on.

		// Scale that according to our resampling rate
		if (actual_sample_read_rate != kMaxSampleValue) {
			release_stage_length_bytes = static_cast<int32_t>(
			    (static_cast<int64_t>(release_stage_length_bytes) * actual_sample_read_rate) >> 24);
		}

		// Not yet, while this sample has more left than the release takes
		if (bytes_left >= release_stage_length_bytes) {
			return;
		}

		// How many samples are left for this source, worked out only here because it divides
		int32_t samples_left = bytes_left / bytes_per_sample;

		// Scale that according to our resampling rate
		if (actual_sample_read_rate != kMaxSampleValue) {
			samples_left = static_cast<int32_t>((static_cast<int64_t>(samples_left) << 24) / actual_sample_read_rate);
		}

		highest_num_samples_left = std::max(highest_num_samples_left, samples_left);
	}

	// Every source that needed attention says now: release the amplitude envelope
	// No source with samples left (each was skipped, or has under a frame left) leaves the release rate alone, as the
	// device's integer division by zero gives 0
	override_amplitude_envelope_release_rate_ = highest_num_samples_left != 0 ? 8388608 / highest_num_samples_left : 0;
	if (envelopes_[0].is_at_attack_start()) {
		envelopes_[0].set_output_level(kQ31Max);
	}
	envelopes_[0].unconditional_release();
	stamp_envelope0_stage_entry(block.clock);
}

DualSourceControls Voice::dual_source_controls(const ControlBlock& control_block, const VoiceConfig& cfg,
                                               const VoiceBlock& block, int32_t filter_gain,
                                               int32_t overall_pitch_adjust, bool first_render,
                                               bool stereo_unison) const {
	DualSourceControls controls{
	    .noise_volume = params_.param_final_values[params::LOCAL_NOISE_VOLUME],
	    .env0_attack = params_.param_final_values[params::LOCAL_ENV_0_ATTACK],
	    .filter_gain = filter_gain,
	    .overall_pitch_adjust = overall_pitch_adjust,
	    .porta_envelope_pos = porta_envelope_pos_,
	    .priority_rating = static_cast<int32_t>(priority_rating(cfg.voice_priority, block.num_active_voices)),
	    .note_code = note_code_after_arpeggiation_,
	    .first_render = first_render,
	    .stereo_unison = stereo_unison,
	};
	for (int32_t s = 0; s < kNumSources; s++) {
		controls.volume[s] = params_.param_final_values[params::LOCAL_OSC_A_VOLUME + s];
		controls.pitch_adjust[s] = params_.param_final_values[params::LOCAL_OSC_A_PITCH_ADJUST + s];
		controls.phase_width[s] = params_.param_final_values[params::LOCAL_OSC_A_PHASE_WIDTH + s];
		controls.wave_index[s] = params_.param_final_values[params::LOCAL_OSC_A_WAVE_INDEX + s];
		controls.pitch_mod_disallows_cache[s] =
		    cfg.osc_type[s] == OscType::SAMPLE && pitch_mod_disallows_cache(control_block, cfg, s);
	}
	return controls;
}

FmControls Voice::fm_controls(int32_t overall_osc_amplitude, int32_t filter_gain, int32_t overall_pitch_adjust,
                              bool first_render, bool stereo_unison) const {
	FmControls controls{
	    .overall_osc_amplitude = overall_osc_amplitude,
	    .env0_attack = params_.param_final_values[params::LOCAL_ENV_0_ATTACK],
	    .filter_gain = filter_gain,
	    .overall_pitch_adjust = overall_pitch_adjust,
	    .first_render = first_render,
	    .stereo_unison = stereo_unison,
	};
	for (int32_t s = 0; s < kNumSources; s++) {
		controls.carrier_volume[s] = params_.param_final_values[params::LOCAL_OSC_A_VOLUME + s];
		controls.carrier_pitch_adjust[s] = params_.param_final_values[params::LOCAL_OSC_A_PITCH_ADJUST + s];
		controls.carrier_feedback[s] = params_.param_final_values[params::LOCAL_CARRIER_0_FEEDBACK + s];
		controls.wave_index[s] = params_.param_final_values[params::LOCAL_OSC_A_WAVE_INDEX + s];
	}
	for (int32_t m = 0; m < kNumModulators; m++) {
		controls.modulator_volume[m] = params_.param_final_values[params::LOCAL_MODULATOR_0_VOLUME + m];
		controls.modulator_pitch_adjust[m] = params_.param_final_values[params::LOCAL_MODULATOR_0_PITCH_ADJUST + m];
		controls.modulator_feedback[m] = params_.param_final_values[params::LOCAL_MODULATOR_0_FEEDBACK + m];
	}
	return controls;
}

bool Voice::pitch_mod_disallows_cache(const ControlBlock& control_block, const VoiceConfig& cfg, int32_t s) const {
	// A sample is not cached when a source that can change during the note modulates its pitch. The routing groups
	// each destination's cables, so only the two pitch destinations' cables are scanned.
	const deluge::synth::PatchRouting& routing = control_block.routing;
	const deluge::synth::CableTarget overall_pitch = deluge::synth::CableTarget::param(params::LOCAL_PITCH_ADJUST);
	const deluge::synth::CableTarget source_pitch =
	    deluge::synth::CableTarget::param(params::LOCAL_OSC_A_PITCH_ADJUST + s);
	for (const deluge::synth::RoutedDestination* d = routing.destinations[GLOBALITY_LOCAL];
	     d != nullptr && d->sources != 0; d++) {
		if (d->target != overall_pitch && d->target != source_pitch) {
			continue;
		}
		for (int32_t c = d->first_cable; std::cmp_less(c, d->end_cable); c++) {
			const PatchSource from = routing.cables[c].source;
			if (from == PatchSource::ENVELOPE_0 || from == PatchSource::ENVELOPE_1 || from == PatchSource::LFO_GLOBAL_1
			    || from == PatchSource::LFO_LOCAL_1 || from == PatchSource::LFO_GLOBAL_2
			    || from == PatchSource::LFO_LOCAL_2 || from == PatchSource::RANDOM
			    || (from == PatchSource::AFTERTOUCH
			        && (params_.source_values[util::to_underlying(PatchSource::AFTERTOUCH)] != 0))
			    || (from == PatchSource::SIDECHAIN
			        && (cfg.global_source_values[util::to_underlying(PatchSource::SIDECHAIN)] != 0))) {
				return true;
			}
		}
	}
	return false;
}

bool Voice::do_fast_release(uint32_t release_increment, deluge::synth::VoiceClock clock) {
	if (done_first_render_) {
		envelopes_[0].unconditional_release(Envelope::Stage::FAST_RELEASE, release_increment);
		stamp_envelope0_stage_entry(clock);
		return true;
	}

	// Or if first render not done yet, we actually don't want to hear anything at all, so just unassign it

	return false;
}

bool Voice::speed_up_release(deluge::synth::VoiceClock clock, uint32_t min_fast_release_increment) {
	if (!done_first_render_) {
		return false;
	}

	auto current_stage = envelopes_[0].stage();
	if (current_stage == Envelope::Stage::RELEASE) {
		auto current_release = params_.param_final_values[params::LOCAL_ENV_0_RELEASE];
		envelopes_[0].unconditional_release(Envelope::Stage::FAST_RELEASE,
		                                    std::max<uint32_t>(current_release * 2, min_fast_release_increment));
	}
	else if (current_stage == Envelope::Stage::FAST_RELEASE) {
		auto current_release = envelopes_[0].fast_release_rate();
		envelopes_[0].unconditional_release(Envelope::Stage::FAST_RELEASE,
		                                    std::max<uint32_t>(current_release * 2, min_fast_release_increment));
	}
	// Cull-driven release must not inherit a patch's long musical release, or culled voices can stack up.
	else {
		envelopes_[0].unconditional_release(Envelope::Stage::FAST_RELEASE, min_fast_release_increment);
	}
	stamp_envelope0_stage_entry(clock);
	return true;
}

bool Voice::do_immediate_release(deluge::synth::VoiceClock clock) {
	if (done_first_render_) {
		envelopes_[0].unconditional_off();
		stamp_envelope0_stage_entry(clock);
		return true;
	}

	// Or if first render not done yet, we actually don't want to hear anything at all, so just unassign it
	return false;
}

bool Voice::has_release_stage() {
	return (params_.param_final_values[params::LOCAL_ENV_0_RELEASE] <= 18359);
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
	    + (static_cast<uint32_t>(envelopes_[0].stage()) << 24)

	    // Bits  0-23 - time entered
	    + ((-envelope0_time_entered_) & kTimeEnteredMask);
}

void Voice::stamp_envelope0_stage_entry(deluge::synth::VoiceClock clock) {
	if (envelopes_[0].take_stage_changed()) {
		envelope0_time_entered_ = clock.tick();
	}
}

void Voice::reset_envelope0_time_entered(deluge::synth::VoiceClock clock) {
	if (envelopes_[0].stage() == Envelope::Stage::SUSTAIN) {
		envelope0_time_entered_ = clock.tick();
	}
}

} // namespace deluge::synth
