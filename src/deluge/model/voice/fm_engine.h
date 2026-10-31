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
} // namespace deluge::synth

class Sound;
struct VoiceConfig;

/// @brief The FM synth engine: the cross-source driver for `SynthMode::FM` (per the L1 reframe,
/// `docs/dev/synth_l1_engine_architecture.md`). It owns one unison part's FM topology — modulator pitch
/// derivation, the modulators (with the `modulator1ToModulator0` routing and the carriers-only fallback when no
/// modulators are active), the carriers (FM'd by the modulators, with feedback), and the stereo-unison fold.
///
/// The engine owns the full per-voice FM render — the unison loop, the per-source carrier pitch math, and (per
/// unison part) the modulator/carrier topology — plus the per-operator sine/FM-with-feedback renders (the FM DSP —
/// `renderSineWaveWithFeedback` / `renderFMWithFeedback` / `renderFMWithFeedbackAdd`, below); the shared pitch
/// helper `Voice::adjustPitch` is a public static it calls. The **first stateful engine**: it now owns the
/// FM-exclusive per-voice state (the modulator grid + amplitude smoothing, below) — written only under FM mode and
/// read only here. The carriers still render over the *shared* per-source `oscPos`/`carrierFeedback` on the
/// VoiceUnisonPartSources (Source-tier state used by all modes), reached via the passed-in `Voice&` (public members
/// only — no `friend`), mirroring @ref SubtractiveEngine / @ref RingModEngine.
class FMEngine {
public:
	// The FM-exclusive per-voice state, relocated here from VoiceUnisonPart / Voice (only FM touches it; mode never
	// changes on a live voice — setSynthMode kills voices first). Indexed [unison part][modulator]. The setup that
	// writes these (note-on phase seed + per-unison pitch derivation) still lives in Voice for now — it relocates
	// into the engine at the kernel lift — so these stay public, as the VoiceUnisonPart fields were.
	std::array<std::array<uint32_t, kNumModulators>, kMaxNumVoicesUnison> modulator_phase;
	/// A modulator's phase increment when it is too high to render, and is not rendered.
	static constexpr uint32_t kInactive = 0xFFFFFFFF;
	std::array<std::array<uint32_t, kNumModulators>, kMaxNumVoicesUnison> modulator_phase_increment; // or kInactive
	std::array<std::array<int32_t, kNumModulators>, kMaxNumVoicesUnison> modulator_feedback;
	std::array<int32_t, kNumModulators> modulator_amplitude_last_time; // the modulator-volume de-zipper (voice-level)

	/// Render all of @p voice's FM unison parts, accumulating into @p oscBuffer. Owns the unison loop (ascending
	/// frequency) + the per-source carrier pitch adjust (a too-high carrier zeroes that source's amplitude, which —
	/// faithful to the inline code — persists across unison parts). The pre-roll source/modulator amplitude arrays
	/// are computed by Voice and passed in; @p sourceAmplitudes and @p modulatorsActive are **mutable** (entries
	/// knocked to 0/false on pitch-too-high, persisting across parts). @p didStereoTempBuffer is set when
	/// @p stereoUnison. @p modulatorScratch / @p carrierScratch are the caller's spare buffers
	/// (spareRenderingBuffer[2]/[3]).
	void render(deluge::synth::Voice& voice, int32_t num_samples, int32_t overall_pitch_adjust, bool stereo_unison,
	            int32_t* source_amplitudes, const int32_t* source_amplitudes_now,
	            const int32_t* source_amplitude_increments, bool* modulators_active,
	            const int32_t* modulator_amplitude_increments, int32_t* osc_buffer, int32_t* modulator_scratch,
	            int32_t* carrier_scratch, bool& did_stereo_temp_buffer, const VoiceConfig& cfg);

private:
	/// Render one unison part of FM, accumulating into @p oscBuffer. The per-source/per-modulator arrays +
	/// per-unison amplitudes are computed by @ref render and passed in. @p modulatorsActive is **mutable** — entries
	/// are knocked to false on pitch-too-high and (faithful to the inline code) that persists across unison
	/// parts. @p modulatorScratch / @p carrierScratch are the caller's spare buffers (spareRenderingBuffer[2]/[3]).
	void render_unison_part(deluge::synth::Voice& voice, int32_t u, int32_t num_samples, int32_t overall_pitch_adjust,
	                        bool stereo_unison, int32_t unison_amplitude_l, int32_t unison_amplitude_r,
	                        const uint32_t* phase_increments, const int32_t* source_amplitudes,
	                        const int32_t* source_amplitudes_now, const int32_t* source_amplitude_increments,
	                        bool* modulators_active, const int32_t* modulator_amplitude_increments, int32_t* osc_buffer,
	                        int32_t* modulator_scratch, int32_t* carrier_scratch, const VoiceConfig& cfg);

	// The FM operator renders (pure DSP over the passed buffers/phases). Moved here from Voice — they read no
	// Voice state, so the engine no longer needs to be a friend of Voice.
	static void render_sine_wave_with_feedback(int32_t* buffer_start, int32_t num_samples, uint32_t* phase,
	                                           int32_t amplitude, uint32_t phase_increment, int32_t feedback_amount,
	                                           int32_t* last_feedback_value, bool add, int32_t amplitude_increment);
	static void render_fm_with_feedback(int32_t* buffer_start, int32_t num_samples, int32_t* fm_buffer, uint32_t* phase,
	                                    int32_t amplitude, uint32_t phase_increment, int32_t feedback_amount,
	                                    int32_t* last_feedback_value, int32_t amplitude_increment);
	static void render_fm_with_feedback_add(int32_t* buffer_start, int32_t num_samples, int32_t* fm_buffer,
	                                        uint32_t* phase, int32_t amplitude, uint32_t phase_increment,
	                                        int32_t feedback_amount, int32_t* last_feedback_value,
	                                        int32_t amplitude_increment);
};
