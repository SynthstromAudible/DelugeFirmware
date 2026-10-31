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

#include "model/voice/fm_engine.h"
#include "arm_neon_shim.h" // NEON intrinsics (SIMDe on host) for the operator renders
#include "definitions_cxx.hpp"
#include "dsp-next/fixed/osc/sine.h" // deluge::dsp::fixed::sine / sine_fm_operator
#include "dsp-next/fixed/vca/pan.h"
#include "model/voice/voice.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"
#include "util/fixedpoint.h"
#include <cstring>

using namespace deluge;
using deluge::synth::Voice;
namespace params = deluge::modulation::params;

void FMEngine::render(Voice& voice, int32_t num_samples, int32_t overall_pitch_adjust, bool stereo_unison,
                      int32_t* source_amplitudes, const int32_t* source_amplitudes_now,
                      const int32_t* source_amplitude_increments, bool* modulators_active,
                      const int32_t* modulator_amplitude_increments, int32_t* osc_buffer, int32_t* modulator_scratch,
                      int32_t* carrier_scratch, bool& did_stereo_temp_buffer, const VoiceConfig& cfg) {

	if (stereo_unison) {
		// oscBuffer is always a stereo temp buffer
		did_stereo_temp_buffer = true;
	}

	// For each unison part
	for (int32_t u = 0; u < cfg.num_unison; u++) {

		const deluge::dsp::fixed::PanAmplitudes unison_pan_amplitudes =
		    deluge::dsp::fixed::pan_amplitudes(stereo_unison ? cfg.unison_pan[u] : 0);
		int32_t unison_amplitude_l = unison_pan_amplitudes.left;
		int32_t unison_amplitude_r = unison_pan_amplitudes.right;

		// Work out the phase increments of the two sources. If these are too high, sourceAmplitudes[s] is set to 0.
		// Yes this will affect all unison parts, which seems like it's not what we want, but since we're traversing
		// the unison parts in ascending frequency, it's fine!

		uint32_t phase_increments[kNumSources];
		for (int32_t s = 0; s < kNumSources; s++) {
			phase_increments[s] = voice.unison_parts[u].sources[s].phase_increment_stored_value;
		}

		// If overall pitch adjusted...
		if (overall_pitch_adjust != kMaxSampleValue) {
			for (int32_t s = 0; s < kNumSources; s++) {
				if (!Voice::adjust_pitch(phase_increments[s], overall_pitch_adjust)) {
					source_amplitudes[s] = 0;
				}
			}
		}

		// If individual source pitch adjusted...
		for (int32_t s = 0; s < kNumSources; s++) {
			if (!Voice::adjust_pitch(phase_increments[s],
			                         voice.params.param_final_values[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
				source_amplitudes[s] = 0;
			}
		}

		render_unison_part(voice, u, num_samples, overall_pitch_adjust, stereo_unison, unison_amplitude_l,
		                   unison_amplitude_r, phase_increments, source_amplitudes, source_amplitudes_now,
		                   source_amplitude_increments, modulators_active, modulator_amplitude_increments, osc_buffer,
		                   modulator_scratch, carrier_scratch, cfg);
	}
}

void FMEngine::render_unison_part(Voice& voice, int32_t u, int32_t num_samples, int32_t overall_pitch_adjust,
                                  bool stereo_unison, int32_t unison_amplitude_l, int32_t unison_amplitude_r,
                                  const uint32_t* phase_increments, const int32_t* source_amplitudes,
                                  const int32_t* source_amplitudes_now, const int32_t* source_amplitude_increments,
                                  bool* modulators_active, const int32_t* modulator_amplitude_increments,
                                  int32_t* osc_buffer, int32_t* modulator_scratch, int32_t* carrier_scratch,
                                  const VoiceConfig& cfg) {

	// If overall pitch adjusted, adjust modulator pitches
	uint32_t phase_increment_modulator[kNumModulators];
	for (int32_t m = 0; m < kNumModulators; m++) {
		phase_increment_modulator[m] = modulator_phase_increment[u][m];
		if (phase_increment_modulator[m] == kInactive) {
			modulators_active[m] = false; // If frequency marked as too high
		}
	}

	if (overall_pitch_adjust != kMaxSampleValue) {
		for (int32_t m = 0; m < kNumModulators; m++) {
			if (modulators_active[m]) {
				if (!Voice::adjust_pitch(phase_increment_modulator[m], overall_pitch_adjust)) {
					modulators_active[m] = false;
				}
			}
		}
	}

	// Check if individual modulator pitches adjusted
	for (int32_t m = 0; m < kNumModulators; m++) {
		if (modulators_active[m]) {
			if (!Voice::adjust_pitch(phase_increment_modulator[m],
			                         voice.params.param_final_values[params::LOCAL_MODULATOR_0_PITCH_ADJUST + m])) {
				modulators_active[m] = false;
			}
		}
	}

	int32_t* fm_osc_buffer = osc_buffer;
	if (stereo_unison) {
		// buffer 0-1: stereo output, 2: modulators, 3: per-unison carriers
		fm_osc_buffer = carrier_scratch;
		memset(fm_osc_buffer, 0, num_samples * sizeof(int32_t));
	}

	// Modulators
	if (modulators_active[1]) {

		// If special case where mod1 is modulating mod0 but mod0 is inactive, go away...
		if (cfg.modulator1_to_modulator0 && !modulators_active[0]) {
			goto noModulatorsActive;
		}

		// Render mod1
		render_sine_wave_with_feedback(modulator_scratch, num_samples, &modulator_phase[u][1],
		                               modulator_amplitude_last_time[1], phase_increment_modulator[1],
		                               voice.params.param_final_values[params::LOCAL_MODULATOR_1_FEEDBACK],
		                               &modulator_feedback[u][1], false, modulator_amplitude_increments[1]);

		// If mod1 is modulating mod0...
		if (cfg.modulator1_to_modulator0) {
			// .. render modulator0, receiving the FM from mod1
			render_fm_with_feedback(modulator_scratch, num_samples, nullptr, modulator_phase[u].data(),
			                        modulator_amplitude_last_time[0], phase_increment_modulator[0],
			                        voice.params.param_final_values[params::LOCAL_MODULATOR_0_FEEDBACK],
			                        modulator_feedback[u].data(), modulator_amplitude_increments[0]);
		}

		// Otherwise, so long as modulator0 is in fact active, render it separately and add it
		else if (modulators_active[0]) {
			render_sine_wave_with_feedback(modulator_scratch, num_samples, modulator_phase[u].data(),
			                               modulator_amplitude_last_time[0], phase_increment_modulator[0],
			                               voice.params.param_final_values[params::LOCAL_MODULATOR_0_FEEDBACK],
			                               modulator_feedback[u].data(), true, modulator_amplitude_increments[0]);
		}
	}
	else {
		if (modulators_active[0]) {
			render_sine_wave_with_feedback(modulator_scratch, num_samples, modulator_phase[u].data(),
			                               modulator_amplitude_last_time[0], phase_increment_modulator[0],
			                               voice.params.param_final_values[params::LOCAL_MODULATOR_0_FEEDBACK],
			                               modulator_feedback[u].data(), false, modulator_amplitude_increments[0]);
		}
		else {
noModulatorsActive:
			for (int32_t s = 0; s < kNumSources; s++) {
				if (source_amplitudes[s] != 0) {
					render_sine_wave_with_feedback(
					    fm_osc_buffer, num_samples, &voice.unison_parts[u].sources[s].osc_pos, source_amplitudes_now[s],
					    phase_increments[s], voice.params.param_final_values[params::LOCAL_CARRIER_0_FEEDBACK + s],
					    &voice.unison_parts[u].sources[s].carrier_feedback, true, source_amplitude_increments[s]);
				}
			}

			goto carriersDone;
		}
	}

	// Carriers
	for (int32_t s = 0; s < kNumSources; s++) {
		if (source_amplitudes[s] != 0) {
			render_fm_with_feedback_add(
			    fm_osc_buffer, num_samples, modulator_scratch, &voice.unison_parts[u].sources[s].osc_pos,
			    source_amplitudes_now[s], phase_increments[s],
			    voice.params.param_final_values[params::LOCAL_CARRIER_0_FEEDBACK + s],
			    &voice.unison_parts[u].sources[s].carrier_feedback, source_amplitude_increments[s]);
		}
	}

carriersDone: {}
	if (stereo_unison) {
		// double up the temp buffer
		for (int32_t i = 0; i < num_samples; i++) {
			osc_buffer[(i << 1)] += multiply_32x32_rshift32(fm_osc_buffer[i], unison_amplitude_l) << 2;
			osc_buffer[(i << 1) + 1] += multiply_32x32_rshift32(fm_osc_buffer[i], unison_amplitude_r) << 2;
		}
	}
}

void FMEngine::render_sine_wave_with_feedback(int32_t* buffer_start, int32_t num_samples, uint32_t* phase,
                                              int32_t amplitude, uint32_t phase_increment, int32_t feedback_amount,
                                              int32_t* last_feedback_value, bool add, int32_t amplitude_increment) {

	uint32_t phase_now = *phase;
	*phase += phase_increment * num_samples;

	if (feedback_amount != 0) {
		int32_t amplitude_now = amplitude;
		int32_t* this_sample = buffer_start;
		int32_t feedback_value = *last_feedback_value;
		int32_t* buffer_end = buffer_start + num_samples;
		do {
			amplitude_now += amplitude_increment;
			int32_t feedback = multiply_32x32_rshift32(feedback_value, feedback_amount);

			// We do hard clipping of the feedback amount. Doing tanH causes aliasing - even if we used the anti-aliased
			// version. The hard clipping one sounds really solid.
			feedback = signed_saturate<22>(feedback);

			feedback_value = dsp::fixed::sine_fm_operator(phase_now += phase_increment, feedback);

			if (add) {
				*this_sample = multiply_accumulate_32x32_rshift32_rounded(*this_sample, feedback_value, amplitude_now);
			}
			else {
				*this_sample = multiply_32x32_rshift32(feedback_value, amplitude_now);
			}
		} while (++this_sample != buffer_end);

		*last_feedback_value = feedback_value;
	}
	else {
		int32_t amplitude_now = amplitude;
		int32_t* this_sample = buffer_start;
		int32_t* buffer_end = buffer_start + num_samples;

		if (amplitude_increment != 0) {
			do {
				auto phase_vector = Argon<uint32_t>{phase_now}.MultiplyAdd(uint32x4_t{1, 2, 3, 4}, phase_increment);
				phase_now += phase_increment * 4;
				int32x4_t sine_value_vector = dsp::fixed::sine(phase_vector);

				int32x4_t amplitude_vector;
				for (int32_t i = 0; i < 4; i++) {
					amplitude_now += amplitude_increment;
					amplitude_vector[i] = amplitude_now >> 1;
				}

				int32x4_t result_value_vector = vqdmulhq_s32(amplitude_vector, sine_value_vector);

				if (add) {
					int32x4_t existing_buffer_contents = vld1q_s32(this_sample);
					int32x4_t added = vaddq_s32(existing_buffer_contents, result_value_vector);
					vst1q_s32(this_sample, added);
				}
				else {
					vst1q_s32(this_sample, result_value_vector);
				}

				this_sample += 4;
			} while (this_sample < buffer_end);
		}

		else {
			do {
				auto phase_vector = Argon<uint32_t>{phase_now}.MultiplyAdd(uint32x4_t{1, 2, 3, 4}, phase_increment);
				phase_now += phase_increment * 4;
				int32x4_t sine_value_vector = dsp::fixed::sine(phase_vector);
				int32x4_t result_value_vector = vqrdmulhq_n_s32(sine_value_vector, amplitude_now >> 1);

				if (add) {
					int32x4_t existing_buffer_contents = vld1q_s32(this_sample);
					int32x4_t added = vaddq_s32(existing_buffer_contents, result_value_vector);
					vst1q_s32(this_sample, added);
				}
				else {
					vst1q_s32(this_sample, result_value_vector);
				}

				this_sample += 4;
			} while (this_sample < buffer_end);
		}
	}
}

void FMEngine::render_fm_with_feedback(int32_t* buffer_start, int32_t num_samples, int32_t* fm_buffer, uint32_t* phase,
                                       int32_t amplitude, uint32_t phase_increment, int32_t feedback_amount,
                                       int32_t* last_feedback_value, int32_t amplitude_increment) {

	uint32_t phase_now = *phase;
	*phase += phase_increment * num_samples;

	if (feedback_amount != 0) {
		int32_t amplitude_now = amplitude;
		int32_t* this_sample = buffer_start;
		int32_t* buffer_end = buffer_start + num_samples;

		int32_t feedback_value = *last_feedback_value;
		do {
			amplitude_now += amplitude_increment;

			int32_t feedback = multiply_32x32_rshift32(feedback_value, feedback_amount);

			// We do hard clipping of the feedback amount. Doing tanH causes aliasing - even if we used the anti-aliased
			// version. The hard clipping one sounds really solid.
			feedback = signed_saturate<22>(feedback);

			uint32_t sum = static_cast<uint32_t>(*this_sample) + static_cast<uint32_t>(feedback);

			feedback_value = dsp::fixed::sine_fm_operator(phase_now += phase_increment, sum);
			*this_sample = multiply_32x32_rshift32(feedback_value, amplitude_now);
		} while (++this_sample != buffer_end);

		*last_feedback_value = feedback_value;
	}
	else {
		int32_t amplitude_now = amplitude;
		int32_t* this_sample = buffer_start;
		int32_t* buffer_end = buffer_start + num_samples;
		do {
			amplitude_now += amplitude_increment;
			int32_t fm_value = dsp::fixed::sine_fm_operator(phase_now += phase_increment, *this_sample);
			*this_sample = multiply_32x32_rshift32(fm_value, amplitude_now);
		} while (++this_sample != buffer_end);
	}
}

void FMEngine::render_fm_with_feedback_add(int32_t* buffer_start, int32_t num_samples, int32_t* fm_buffer,
                                           uint32_t* phase, int32_t amplitude, uint32_t phase_increment,
                                           int32_t feedback_amount, int32_t* last_feedback_value,
                                           int32_t amplitude_increment) {

	uint32_t phase_now = *phase;
	*phase += phase_increment * num_samples;

	if (feedback_amount != 0) {
		int32_t amplitude_now = amplitude;
		int32_t* this_sample = buffer_start;
		int32_t* fm_sample = fm_buffer;
		int32_t* buffer_end = buffer_start + num_samples;

		int32_t feedback_value = *last_feedback_value;
		do {
			amplitude_now += amplitude_increment;

			int32_t feedback = multiply_32x32_rshift32(feedback_value, feedback_amount);

			// We do hard clipping of the feedback amount. Doing tanH causes aliasing - even if we used the anti-aliased
			// version. The hard clipping one sounds really solid.
			feedback = signed_saturate<22>(feedback);

			uint32_t sum = static_cast<uint32_t>(*(fm_sample++)) + static_cast<uint32_t>(feedback);

			feedback_value = dsp::fixed::sine_fm_operator(phase_now += phase_increment, sum);
			*this_sample = multiply_accumulate_32x32_rshift32_rounded(*this_sample, feedback_value, amplitude_now);
		} while (++this_sample != buffer_end);

		*last_feedback_value = feedback_value;
	}
	else {
		int32_t amplitude_now = amplitude;
		int32_t* this_sample = buffer_start;
		int32_t* buffer_end = buffer_start + num_samples;
		uint32_t* fm_sample = reinterpret_cast<uint32_t*>(fm_buffer);
		int32_t* buffer_pre_end = buffer_end - 4;

		uint32x4_t phase_vector;
		for (int32_t i = 0; i < 4; i++) {
			phase_now += phase_increment;
			phase_vector[i] = phase_now;
		}

		uint32x4_t phase_increment_vector = vdupq_n_u32(phase_increment << 2);

		if (amplitude_increment != 0) {
			while (true) {
				uint32x4_t phase_shift = vld1q_u32(fm_sample);
				int32x4_t sine_value_vector =
				    dsp::fixed::sine(Argon<uint32_t>{phase_vector} + (Argon<uint32_t>{phase_shift} << 8));

				int32x4_t amplitude_vector;
				for (int32_t i = 0; i < 4; i++) {
					amplitude_now += amplitude_increment;
					amplitude_vector[i] = amplitude_now >> 1;
				}

				int32x4_t result_value_vector = vqdmulhq_s32(amplitude_vector, sine_value_vector);

				int32x4_t existing_buffer_contents = vld1q_s32(this_sample);
				int32x4_t added = vaddq_s32(existing_buffer_contents, result_value_vector);
				vst1q_s32(this_sample, added);

				if (this_sample >= buffer_pre_end) {
					break;
				}

				this_sample += 4;
				fm_sample += 4;
				phase_vector = vaddq_u32(phase_vector, phase_increment_vector);
			}
		}

		else {
			while (true) {
				uint32x4_t phase_shift = vld1q_u32(fm_sample);
				int32x4_t sine_value_vector =
				    dsp::fixed::sine(Argon<uint32_t>{phase_vector} + (Argon<uint32_t>{phase_shift} << 8));

				int32x4_t result_value_vector = vqrdmulhq_n_s32(sine_value_vector, amplitude_now >> 1);

				int32x4_t existing_buffer_contents = vld1q_s32(this_sample);
				int32x4_t added = vaddq_s32(existing_buffer_contents, result_value_vector);
				vst1q_s32(this_sample, added);

				if (this_sample >= buffer_pre_end) {
					break;
				}

				this_sample += 4;
				fm_sample += 4;
				phase_vector = vaddq_u32(phase_vector, phase_increment_vector);
			}
		}
	}
}
