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
#pragma once

#include "definitions_cxx.hpp" // kNumModulators, kMaxNumVoicesUnison
#include <array>
#include <cstdint>

namespace deluge::synth {

class Voice;
struct VoiceConfig;

/// @brief The FM synth engine: the cross-source driver for `SynthMode::FM` (per the L1 reframe,
/// `docs/dev/synth_l1_engine_architecture.md`). It owns one unison part's FM topology — modulator pitch
/// derivation, the modulators (with the `modulator1_to_modulator0` routing and the carriers-only fallback when no
/// modulators are active), the carriers (FM'd by the modulators, with feedback), and the stereo-unison fold.
///
/// The engine owns the full per-voice FM render — the unison loop, the per-source carrier pitch math, the
/// modulator/carrier topology of each unison part and the per-operator renders — and the state only FM uses: the
/// modulator grid and its amplitude smoothing. The carriers render over each unison part source's `osc_pos` and
/// `carrier_feedback`, which every mode shares, reached through the `Voice&` it is handed.
class FMEngine {
public:
	// The state only FM uses (a live voice's mode never changes: changing a sound's mode ends its voices), indexed
	// [unison part][modulator]. Public: the voice's note-on seeds the phases and its phase-increment step sets the
	// increments.
	std::array<std::array<uint32_t, kNumModulators>, kMaxNumVoicesUnison> modulator_phase{};
	/// A modulator's phase increment when it is too high to render, and is not rendered.
	static constexpr uint32_t kInactive = 0xFFFFFFFF;
	std::array<std::array<uint32_t, kNumModulators>, kMaxNumVoicesUnison> modulator_phase_increment{}; // or kInactive
	std::array<std::array<int32_t, kNumModulators>, kMaxNumVoicesUnison> modulator_feedback{};
	std::array<int32_t, kNumModulators> modulator_amplitude_last_time{}; // the modulator-volume de-zipper

	/// Render all of @p voice's FM unison parts, accumulating into @p osc_buffer. Owns the unison loop (ascending
	/// frequency) and the per-source carrier pitch adjust: a too-high carrier zeroes that source's amplitude, which
	/// persists across unison parts. The source and modulator amplitude arrays are the voice's; @p source_amplitudes
	/// and @p modulators_active are knocked to 0/false on a too-high pitch, persisting across parts.
	/// @p did_stereo_temp_buffer is set when @p stereo_unison. @p modulator_scratch and @p carrier_scratch are the
	/// voice's spare buffers.
	void render(deluge::synth::Voice& voice, int32_t num_samples, int32_t overall_pitch_adjust, bool stereo_unison,
	            int32_t* source_amplitudes, const int32_t* source_amplitudes_now,
	            const int32_t* source_amplitude_increments, bool* modulators_active,
	            const int32_t* modulator_amplitude_increments, int32_t* osc_buffer, int32_t* modulator_scratch,
	            int32_t* carrier_scratch, bool& did_stereo_temp_buffer, const VoiceConfig& cfg);

private:
	/// Render one unison part of FM, accumulating into @p osc_buffer. The per-source and per-modulator arrays and the
	/// per-unison amplitudes come from @ref render. @p modulators_active entries are knocked to false on a too-high
	/// pitch, which persists across unison parts. @p modulator_scratch and @p carrier_scratch are the voice's spare
	/// buffers.
	void render_unison_part(deluge::synth::Voice& voice, int32_t u, int32_t num_samples, int32_t overall_pitch_adjust,
	                        bool stereo_unison, int32_t unison_amplitude_l, int32_t unison_amplitude_r,
	                        const uint32_t* phase_increments, const int32_t* source_amplitudes,
	                        const int32_t* source_amplitudes_now, const int32_t* source_amplitude_increments,
	                        bool* modulators_active, const int32_t* modulator_amplitude_increments, int32_t* osc_buffer,
	                        int32_t* modulator_scratch, int32_t* carrier_scratch, const VoiceConfig& cfg);

	// The FM operator renders: pure DSP over the buffers and phases they are handed.
	static void render_sine_wave_with_feedback(int32_t* buffer_start, int32_t num_samples, uint32_t* phase,
	                                           int32_t amplitude, uint32_t phase_increment, int32_t feedback_amount,
	                                           int32_t* last_feedback_value, bool add, int32_t amplitude_increment);
	static void render_fm_with_feedback(int32_t* buffer_start, int32_t num_samples, uint32_t* phase, int32_t amplitude,
	                                    uint32_t phase_increment, int32_t feedback_amount, int32_t* last_feedback_value,
	                                    int32_t amplitude_increment);
	static void render_fm_with_feedback_add(int32_t* buffer_start, int32_t num_samples, int32_t* fm_buffer,
	                                        uint32_t* phase, int32_t amplitude, uint32_t phase_increment,
	                                        int32_t feedback_amount, int32_t* last_feedback_value,
	                                        int32_t amplitude_increment);
};

} // namespace deluge::synth
