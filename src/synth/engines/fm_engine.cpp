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

#include "synth/engines/fm_engine.h"
#include "arm_neon_shim.h" // NEON intrinsics (SIMDe on host) for the operator renders
#include "definitions_cxx.hpp"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/fixed_point_neon.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/osc/sine.h" // deluge::dsp::fixed::sine / sine_fm_operator
#include "dsp-next/fixed/vca/pan.h"
#include "synth/engines/pitch.h"
#include "synth/sources/va/va_shapes.h" // va_shape::initial_phase_for_zero
#include "synth/voice/sound_snapshot.h"
#include "util/lookuptables/lookuptables.h" // noteFrequencyTable
#include <algorithm>
#include <cstring>
#include <utility>

using namespace deluge;

namespace deluge::synth {

namespace {
using dsp::Q;
using dsp::QVec;
using dsp::UQ;

/// @brief @p phase moved on by a block's step, an integer product, wrapping.
[[gnu::always_inline]] inline void advance(uint32_t* phase, uint32_t phase_increment, int32_t num_samples) {
	*phase = add_wrap(UQ<32>::from_raw(*phase), UQ<32>::from_raw(phase_increment * num_samples)).raw();
}

/// @brief The feedback's phase modulation: the last output's Q31 word times the amount, Q30, held to 22 bits. Hard
/// clipping, not tanh: tanh aliases, even anti-aliased, and the clipped feedback sounds solid.
[[gnu::always_inline]] inline int32_t feedback_modulation(int32_t feedback_value, int32_t feedback_amount) {
	return clamp_bits<22>(mul(Q<31>::from_raw(feedback_value), Q<31>::from_raw(feedback_amount))).raw();
}

/// @brief A sine's Q31 word times the amplitude, the high word: Q30, as the carriers' buffer sums it.
[[gnu::always_inline]] inline int32_t scaled(int32_t value, int32_t amplitude) {
	return mul(Q<31>::from_raw(value), Q<31>::from_raw(amplitude)).raw();
}

/// @brief @p accumulator plus a sine's Q31 word times the amplitude, the rounded high word, wrapping.
[[gnu::always_inline]] inline int32_t accumulated(int32_t accumulator, int32_t value, int32_t amplitude) {
	return mla_r(Q<30>::from_raw(accumulator), Q<31>::from_raw(value), Q<31>::from_raw(amplitude)).raw();
}

/// @brief Four lanes of the ramping amplitude, each read at Q30 (halved), so the doubling multiply gives the high word.
[[gnu::always_inline]] inline QVec<30> ramp_lanes(int32_t& amplitude_now, int32_t amplitude_increment) {
	int32x4_t lanes;
	for (int32_t i = 0; i < 4; i++) {
		amplitude_now += amplitude_increment;
		lanes[i] = as_floor<Q<30>>(Q<31>::from_raw(amplitude_now)).raw();
	}
	return QVec<30>::from_raw(lanes);
}

/// @brief @p result added into the buffer's four words at @p sample, wrapping.
[[gnu::always_inline]] inline void add_into(int32_t* sample, QVec<30> result) {
	vst1q_s32(sample, add_wrap(QVec<30>::from_raw(vld1q_s32(sample)), result).raw());
}
} // namespace

bool FMEngine::note_on(const EngineNote& note) {
	if (!SlotEngine::note_on(note)) {
		return false;
	}
	const int32_t num_unison = note.snapshot->num_unison;
	const std::array<uint32_t, kNumModulators>& retrigger_phase = note.snapshot->modulator_retrigger_phase;
	const bool reset = note.reset;
	const uint32_t initial_phase = va_shape::initial_phase_for_zero(OscType::SINE);
	for (int32_t u = 0; u < num_unison; u++) {
		for (int32_t m = 0; m < kNumModulators; m++) {
			if (retrigger_phase[m] != kNoRetrigger) {
				modulator_phase_[u][m] = initial_phase + retrigger_phase[m];
			}
			if (reset) {
				modulator_feedback_[u][m] = 0;
			}
		}
	}
	return true;
}

void FMEngine::randomize_part(int32_t u, dsp::fixed::Cong& noise) {
	for (int32_t m = 0; m < kNumModulators; m++) {
		modulator_phase_[u][m] = noise.next();
	}
}

void FMEngine::calculate_phase_increments(const EngineNote& note) {
	SlotEngine::calculate_phase_increments(note);

	const SoundSnapshot& snapshot = *note.snapshot;
	const int32_t note_code_with_master_transpose = note.note_code + snapshot.transpose;
	const std::array<bool, kNumModulators>& modulator_audible = note.setup->modulator_audible;
	for (int32_t m = 0; m < kNumModulators; m++) {
		if (!modulator_audible[m]) {
			continue;
		}

		const int32_t transposed_note_code = note_code_with_master_transpose + snapshot.modulator_transpose[m];
		const int32_t note_within_octave = (transposed_note_code + 120 - 4) % 12;
		const int32_t octave = (transposed_note_code + 120 - 4) / 12;
		const int32_t shift_right_amount = 20 - octave;

		if (shift_right_amount < 0) {
			// Too high to render (above 22.05 kHz)
			for (int32_t u = 0; u < snapshot.num_unison; u++) {
				modulator_phase_increment_[u][m] = kInactive;
			}
			continue;
		}
		int32_t phase_increment = noteFrequencyTable[note_within_octave] >> shift_right_amount;

		// Cents, then the master transpose's
		phase_increment = snapshot.modulator_transposers[m].detune(phase_increment);
		phase_increment = snapshot.fine_tuner.detune(phase_increment);

		if (snapshot.num_unison == 1) {
			modulator_phase_increment_[0][m] = phase_increment;
		}
		else {
			for (int32_t u = 0; u < snapshot.num_unison; u++) {
				modulator_phase_increment_[u][m] = snapshot.unison_detuners[u].detune(phase_increment);
			}
		}
	}
}

void FMEngine::set_levels(std::span<const int32_t, kNumModulators> modulator_volume) {
	std::ranges::copy(modulator_volume, modulator_amplitude_last_time_.begin());
}

EngineRenderResult FMEngine::render(const EngineBlock& block) {
	const VoiceConfig& cfg = *block.cfg;
	const FmControls& c = block.fm;
	const bool stereo_unison = c.stereo_unison;

	// FM renders no noise
	memset(block.osc_buffer, 0, (stereo_unison ? 2 : 1) * block.num_samples * sizeof(int32_t));

	Pass pass = begin(block);

	// The unison parts ascend in frequency, so a carrier too high in one is too high in the rest: its amplitude is
	// knocked to 0 for them all
	for (int32_t u = 0; u < cfg.num_unison; u++) {
		const std::array<uint32_t, kNumSources> phase_increments = carrier_increments(block, u, pass);
		render_unison_part(block, u, phase_increments, pass);
	}

	// The source levels every engine keeps current: the carriers' (0 where too high) and the wavetable indexes
	block.levels->amplitudes_last_time = pass.source_amplitudes;
	for (int32_t s = 0; s < kNumSources; s++) {
		block.levels->wave_indexes_last_time[s] = c.wave_index[s];
	}
	// osc_buffer is always a stereo buffer when the unison is spread
	return EngineRenderResult{.did_stereo_buffer = stereo_unison};
}

FMEngine::Pass FMEngine::begin(const EngineBlock& block) const {
	const FmControls& c = block.fm;
	SourceLevels& levels = *block.levels;
	const int32_t num_samples = block.num_samples;
	Pass pass;

	// Each carrier's amplitude, with the voice's worked in: no multiplication per sample at the end. Clipped below
	// "4" in the oscillators' terms.
	for (int32_t s = 0; s < kNumSources; s++) {
		pass.source_amplitudes[s] =
		    std::min(mul(Q<31>::from_raw(c.carrier_volume[s]), Q<31>::from_raw(c.overall_osc_amplitude)).raw(),
		             static_cast<int32_t>(134217727));
	}

	// A new filter gain, or a slow attack on the first block, jumps to the new levels rather than ramping to them
	const bool hold = c.first_render ? (c.env0_attack > 245632) : (levels.filter_gain_last_time != c.filter_gain);
	if (hold) {
		levels.amplitudes_last_time = pass.source_amplitudes;
	}
	for (int32_t s = 0; s < kNumSources; s++) {
		pass.source_amplitude_increments[s] =
		    static_cast<int32_t>(pass.source_amplitudes[s] - levels.amplitudes_last_time[s]) / num_samples;
	}
	levels.filter_gain_last_time = c.filter_gain;

	// Whether each modulator sounds this block
	for (int32_t m = 0; m < kNumModulators; m++) {
		pass.modulators_active[m] = (c.modulator_volume[m] != 0 || modulator_amplitude_last_time_[m] != 0);
		if (pass.modulators_active[m]) {
			pass.modulator_amplitude_increments[m] =
			    static_cast<int32_t>(c.modulator_volume[m] - modulator_amplitude_last_time_[m]) / num_samples;
		}
	}

	pass.source_amplitudes_now = levels.amplitudes_last_time;
	return pass;
}

std::array<uint32_t, kNumSources> FMEngine::carrier_increments(const EngineBlock& block, int32_t u, Pass& pass) {
	const FmControls& c = block.fm;
	std::array<uint32_t, kNumSources> phase_increments{};
	for (int32_t s = 0; s < kNumSources; s++) {
		phase_increments[s] = block.parts[u].slots[s].phase_increment_stored_value;
		const bool too_high =
		    (c.overall_pitch_adjust != kMaxSampleValue && !adjust_pitch(phase_increments[s], c.overall_pitch_adjust));
		if (too_high) {
			pass.source_amplitudes[s] = 0;
		}
	}
	for (int32_t s = 0; s < kNumSources; s++) {
		if (!adjust_pitch(phase_increments[s], c.carrier_pitch_adjust[s])) {
			pass.source_amplitudes[s] = 0;
		}
	}
	return phase_increments;
}

std::array<uint32_t, kNumModulators> FMEngine::modulator_increments(const EngineBlock& block, int32_t u,
                                                                    Pass& pass) const {
	const FmControls& c = block.fm;
	std::array<uint32_t, kNumModulators> increments{};
	for (int32_t m = 0; m < kNumModulators; m++) {
		increments[m] = modulator_phase_increment_[u][m];
		if (increments[m] == kInactive) {
			pass.modulators_active[m] = false;
		}
	}
	if (c.overall_pitch_adjust != kMaxSampleValue) {
		for (int32_t m = 0; m < kNumModulators; m++) {
			if (pass.modulators_active[m] && !adjust_pitch(increments[m], c.overall_pitch_adjust)) {
				pass.modulators_active[m] = false;
			}
		}
	}
	for (int32_t m = 0; m < kNumModulators; m++) {
		if (pass.modulators_active[m] && !adjust_pitch(increments[m], c.modulator_pitch_adjust[m])) {
			pass.modulators_active[m] = false;
		}
	}
	return increments;
}

bool FMEngine::render_modulators(const EngineBlock& block, int32_t u,
                                 std::span<const uint32_t, kNumModulators> increments, const Pass& pass) {
	const VoiceConfig& cfg = *block.cfg;
	const FmControls& c = block.fm;
	const int32_t num_samples = block.num_samples;
	int32_t* const modulator_scratch = block.spare_buffers[2];
	const std::array<bool, kNumModulators>& active = pass.modulators_active;

	// Modulator 1 into a silent modulator 0 sounds nothing
	const bool into_silent_modulator = cfg.modulator1_to_modulator0 && !active[0];
	if (active[1] && !into_silent_modulator) {
		render_sine_wave_with_feedback(modulator_scratch, num_samples, &modulator_phase_[u][1],
		                               modulator_amplitude_last_time_[1], increments[1], c.modulator_feedback[1],
		                               &modulator_feedback_[u][1], false, pass.modulator_amplitude_increments[1]);
		if (cfg.modulator1_to_modulator0) {
			// Modulator 0, frequency-modulated by modulator 1
			render_fm_with_feedback(modulator_scratch, num_samples, modulator_phase_[u].data(),
			                        modulator_amplitude_last_time_[0], increments[0], c.modulator_feedback[0],
			                        modulator_feedback_[u].data(), pass.modulator_amplitude_increments[0]);
		}
		else if (active[0]) {
			// Modulator 0 beside it, added
			render_sine_wave_with_feedback(modulator_scratch, num_samples, modulator_phase_[u].data(),
			                               modulator_amplitude_last_time_[0], increments[0], c.modulator_feedback[0],
			                               modulator_feedback_[u].data(), true, pass.modulator_amplitude_increments[0]);
		}
		return true;
	}
	if (!active[1] && active[0]) {
		render_sine_wave_with_feedback(modulator_scratch, num_samples, modulator_phase_[u].data(),
		                               modulator_amplitude_last_time_[0], increments[0], c.modulator_feedback[0],
		                               modulator_feedback_[u].data(), false, pass.modulator_amplitude_increments[0]);
		return true;
	}
	return false;
}

void FMEngine::render_unison_part(const EngineBlock& block, int32_t u,
                                  std::span<const uint32_t, kNumSources> phase_increments, Pass& pass) {
	const FmControls& c = block.fm;
	const int32_t num_samples = block.num_samples;
	int32_t* const osc_buffer = block.osc_buffer;

	// The modulators' increments, with the overall and their own pitch adjust; one too high stops sounding, which
	// holds for the higher parts after this one
	const std::array<uint32_t, kNumModulators> increments = modulator_increments(block, u, pass);

	int32_t* fm_osc_buffer = osc_buffer;
	if (c.stereo_unison) {
		// buffer 0-1: stereo output, 2: modulators, 3: per-unison carriers
		fm_osc_buffer = block.spare_buffers[3];
		memset(fm_osc_buffer, 0, num_samples * sizeof(int32_t));
	}

	const bool modulated = render_modulators(block, u, increments, pass);

	// The carriers: frequency-modulated by the modulators, else plain sines
	for (int32_t s = 0; s < kNumSources; s++) {
		if (pass.source_amplitudes[s] == 0) {
			continue;
		}
		SlotPhase& carrier = block.parts[u].slots[s];
		if (modulated) {
			// The carriers render into their own buffer, modulated by the modulators'
			// NOLINTNEXTLINE(readability-suspicious-call-argument)
			render_fm_with_feedback_add(fm_osc_buffer, num_samples, block.spare_buffers[2], &carrier.osc_pos,
			                            pass.source_amplitudes_now[s], phase_increments[s], c.carrier_feedback[s],
			                            &carrier.carrier_feedback, pass.source_amplitude_increments[s]);
		}
		else {
			render_sine_wave_with_feedback(fm_osc_buffer, num_samples, &carrier.osc_pos, pass.source_amplitudes_now[s],
			                               phase_increments[s], c.carrier_feedback[s], &carrier.carrier_feedback, true,
			                               pass.source_amplitude_increments[s]);
		}
	}

	if (c.stereo_unison) {
		// Pan this part's carriers into the stereo buffer
		const dsp::fixed::PanAmplitudes pan = dsp::fixed::pan_amplitudes(block.cfg->unison_pan[u]);
		for (int32_t i = 0; i < num_samples; i++) {
			const dsp::StereoSample panned =
			    dsp::panned_mono(fm_osc_buffer[i], Q<30>::from_raw(pan.left), Q<30>::from_raw(pan.right));
			int32_t* frame = &osc_buffer[i << 1];
			frame[0] = add_wrap(Q<31>::from_raw(frame[0]), Q<31>::from_raw(panned.l)).raw();
			frame[1] = add_wrap(Q<31>::from_raw(frame[1]), Q<31>::from_raw(panned.r)).raw();
		}
	}
}

void FMEngine::render_sine_wave_with_feedback(int32_t* buffer_start, int32_t num_samples, uint32_t* phase,
                                              int32_t amplitude, uint32_t phase_increment, int32_t feedback_amount,
                                              int32_t* last_feedback_value, bool add, int32_t amplitude_increment) {

	uint32_t phase_now = *phase;
	advance(phase, phase_increment, num_samples);

	if (feedback_amount != 0) {
		int32_t amplitude_now = amplitude;
		int32_t* this_sample = buffer_start;
		int32_t feedback_value = *last_feedback_value;
		int32_t* buffer_end = buffer_start + num_samples;
		do {
			amplitude_now += amplitude_increment;
			const int32_t feedback = feedback_modulation(feedback_value, feedback_amount);

			feedback_value = dsp::fixed::sine_fm_operator(phase_now += phase_increment, feedback);

			*this_sample =
			    add ? accumulated(*this_sample, feedback_value, amplitude_now) : scaled(feedback_value, amplitude_now);
		} while (++this_sample != buffer_end);

		*last_feedback_value = feedback_value;
	}
	else {
		render_sine_wave(buffer_start, num_samples, phase_now, amplitude, phase_increment, add, amplitude_increment);
	}
}

void FMEngine::render_sine_wave(int32_t* buffer_start, int32_t num_samples, uint32_t phase_now, int32_t amplitude,
                                uint32_t phase_increment, bool add, int32_t amplitude_increment) {
	int32_t amplitude_now = amplitude;
	int32_t* this_sample = buffer_start;
	int32_t* buffer_end = buffer_start + num_samples;

	if (amplitude_increment != 0) {
		do {
			auto phase_vector = Argon<uint32_t>{phase_now}.MultiplyAdd(uint32x4_t{1, 2, 3, 4}, phase_increment);
			phase_now += phase_increment * 4;
			int32x4_t sine_value_vector = dsp::fixed::sine(phase_vector);

			// Rounded, as a held level is
			const QVec<30> result = mul_doubling_r_sat(ramp_lanes(amplitude_now, amplitude_increment),
			                                           QVec<31>::from_raw(sine_value_vector));
			if (add) {
				add_into(this_sample, result);
			}
			else {
				vst1q_s32(this_sample, result.raw());
			}

			this_sample += 4;
		} while (this_sample < buffer_end);
	}

	else {
		do {
			auto phase_vector = Argon<uint32_t>{phase_now}.MultiplyAdd(uint32x4_t{1, 2, 3, 4}, phase_increment);
			phase_now += phase_increment * 4;
			int32x4_t sine_value_vector = dsp::fixed::sine(phase_vector);
			// A held level rounds
			const QVec<30> result = mul_doubling_r_sat(QVec<31>::from_raw(sine_value_vector),
			                                           as_floor<Q<30>>(Q<31>::from_raw(amplitude_now)));
			if (add) {
				add_into(this_sample, result);
			}
			else {
				vst1q_s32(this_sample, result.raw());
			}

			this_sample += 4;
		} while (this_sample < buffer_end);
	}
}

void FMEngine::render_fm_with_feedback(int32_t* buffer_start, int32_t num_samples, uint32_t* phase, int32_t amplitude,
                                       uint32_t phase_increment, int32_t feedback_amount, int32_t* last_feedback_value,
                                       int32_t amplitude_increment) {

	uint32_t phase_now = *phase;
	advance(phase, phase_increment, num_samples);

	if (feedback_amount != 0) {
		int32_t amplitude_now = amplitude;
		int32_t* this_sample = buffer_start;
		int32_t* buffer_end = buffer_start + num_samples;

		int32_t feedback_value = *last_feedback_value;
		do {
			amplitude_now += amplitude_increment;

			const int32_t feedback = feedback_modulation(feedback_value, feedback_amount);

			// The modulator's and the feedback's phase modulation, summed wrapping
			const int32_t sum = add_wrap(Q<31>::from_raw(*this_sample), Q<31>::from_raw(feedback)).raw();

			feedback_value = dsp::fixed::sine_fm_operator(phase_now += phase_increment, sum);
			*this_sample = scaled(feedback_value, amplitude_now);
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
			*this_sample = scaled(fm_value, amplitude_now);
		} while (++this_sample != buffer_end);
	}
}

void FMEngine::render_fm_with_feedback_add(int32_t* buffer_start, int32_t num_samples, int32_t* fm_buffer,
                                           uint32_t* phase, int32_t amplitude, uint32_t phase_increment,
                                           int32_t feedback_amount, int32_t* last_feedback_value,
                                           int32_t amplitude_increment) {

	uint32_t phase_now = *phase;
	advance(phase, phase_increment, num_samples);

	if (feedback_amount != 0) {
		int32_t amplitude_now = amplitude;
		int32_t* this_sample = buffer_start;
		int32_t* fm_sample = fm_buffer;
		int32_t* buffer_end = buffer_start + num_samples;

		int32_t feedback_value = *last_feedback_value;
		do {
			amplitude_now += amplitude_increment;

			const int32_t feedback = feedback_modulation(feedback_value, feedback_amount);

			// The modulator's and the feedback's phase modulation, summed wrapping
			const int32_t sum = add_wrap(Q<31>::from_raw(*(fm_sample++)), Q<31>::from_raw(feedback)).raw();

			feedback_value = dsp::fixed::sine_fm_operator(phase_now += phase_increment, sum);
			*this_sample = accumulated(*this_sample, feedback_value, amplitude_now);
		} while (++this_sample != buffer_end);

		*last_feedback_value = feedback_value;
	}
	else {
		render_fm_add(buffer_start, num_samples, fm_buffer, phase_now, amplitude, phase_increment, amplitude_increment);
	}
}

void FMEngine::render_fm_add(int32_t* buffer_start, int32_t num_samples, int32_t* fm_buffer, uint32_t phase_now,
                             int32_t amplitude, uint32_t phase_increment, int32_t amplitude_increment) {
	int32_t amplitude_now = amplitude;
	int32_t* this_sample = buffer_start;
	int32_t* buffer_end = buffer_start + num_samples;
	auto* fm_sample = reinterpret_cast<uint32_t*>(fm_buffer);
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

			// Rounded, as a held level is
			add_into(this_sample, mul_doubling_r_sat(ramp_lanes(amplitude_now, amplitude_increment),
			                                         QVec<31>::from_raw(sine_value_vector)));

			if (this_sample >= buffer_pre_end) {
				break;
			}

			this_sample += 4;
			fm_sample += 4;
			phase_vector =
			    add_wrap(dsp::UQVec<32>::from_raw(phase_vector), dsp::UQVec<32>::from_raw(phase_increment_vector))
			        .raw();
		}
	}

	else {
		while (true) {
			uint32x4_t phase_shift = vld1q_u32(fm_sample);
			int32x4_t sine_value_vector =
			    dsp::fixed::sine(Argon<uint32_t>{phase_vector} + (Argon<uint32_t>{phase_shift} << 8));

			// A held level rounds
			add_into(this_sample, mul_doubling_r_sat(QVec<31>::from_raw(sine_value_vector),
			                                         as_floor<Q<30>>(Q<31>::from_raw(amplitude_now))));

			if (this_sample >= buffer_pre_end) {
				break;
			}

			this_sample += 4;
			fm_sample += 4;
			phase_vector =
			    add_wrap(dsp::UQVec<32>::from_raw(phase_vector), dsp::UQVec<32>::from_raw(phase_increment_vector))
			        .raw();
		}
	}
}

} // namespace deluge::synth
