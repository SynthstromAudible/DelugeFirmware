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

#include "definitions_cxx.hpp" // kNumModulators, kNumSources, kMaxNumVoicesUnison
#include "dsp-next/fixed/noise/cong.h"
#include "synth/engines/slot_engine.h"
#include "synth/engines/source_levels.h"
#include "synth/voice/unison_part.h"
#include "synth/voice/voice_config.h"
#include "synth/voice/voice_inputs.h"
#include <array>
#include <cstdint>
#include <span>

namespace deluge::synth {

struct SoundSnapshot;

/// @brief The FM engine: a whole-voice generator of two modulators (modulator 1 optionally into modulator 0) and two
/// carriers, each with feedback, in every unison part. It ignores the slots' sources: the carriers render over each
/// slot's @c osc_pos and @c carrier_feedback, which every mode shares; the modulator grid is its own.
class FMEngine : public SlotEngine {
public:
	/// A modulator's phase increment when it is too high to render, and is not rendered.
	static constexpr uint32_t kInactive = 0xFFFFFFFF;

	/// Starts the note: the carriers' slots as every engine does, then the modulators: each with a retrigger phase
	/// restarts there, and a new voice clears their feedback.
	bool note_on(const EngineNote& note) override;
	/// The carriers' increments as every engine works them out, then each audible modulator's; a modulator too high
	/// to render is inactive.
	void calculate_phase_increments(const EngineNote& note) override;
	/// A new voice's random modulator phases for unison part @p u, drawn after its sources' (the draw order).
	void randomize_part(int32_t u, dsp::fixed::Cong& noise) override;
	/// Clears the modulator levels' de-zipper (a new voice, whatever its mode).
	void reset_levels() { modulator_amplitude_last_time_.fill(0); }
	/// Sets the modulator levels to @p modulator_volume: held still on a slow attack's first block, and stored for the
	/// next block at each block's end, whatever the mode.
	void set_levels(std::span<const int32_t, kNumModulators> modulator_volume);

	/// Renders @p block's FM into its buffer, which it clears first (FM renders no noise), and stores the source levels
	/// the next block starts from.
	EngineRenderResult render(const EngineBlock& block) override;

private:
	/// What each unison part's render in this block shares.
	struct Pass {
		/// Each carrier's amplitude this block; knocked to 0 where too high to render, for the higher parts too.
		std::array<int32_t, kNumSources> source_amplitudes{};
		std::array<int32_t, kNumSources> source_amplitudes_now{};       ///< Each carrier's amplitude at block start
		std::array<int32_t, kNumSources> source_amplitude_increments{}; ///< Its per-sample increment
		/// Whether each modulator sounds; knocked to false where too high to render, for the higher parts too.
		std::array<bool, kNumModulators> modulators_active{};
		std::array<int32_t, kNumModulators> modulator_amplitude_increments{}; ///< Each one's per-sample increment
	};

	/// The block's carrier levels (held, or ramping from the last block's) and which modulators sound.
	[[nodiscard]] Pass begin(const EngineBlock& block) const;
	/// The carriers' increments in unison part @p u, knocking a too-high carrier's amplitude to 0.
	[[nodiscard]] static std::array<uint32_t, kNumSources> carrier_increments(const EngineBlock& block, int32_t u,
	                                                                          Pass& pass);
	/// The modulators' increments in unison part @p u, silencing a too-high modulator.
	[[nodiscard]] std::array<uint32_t, kNumModulators> modulator_increments(const EngineBlock& block, int32_t u,
	                                                                        Pass& pass) const;
	/// Renders unison part @p u's modulators into the modulator scratch. @return whether any sounded.
	[[nodiscard]] bool render_modulators(const EngineBlock& block, int32_t u,
	                                     std::span<const uint32_t, kNumModulators> increments, const Pass& pass);
	/// Renders unison part @p u, adding into the voice's buffer.
	void render_unison_part(const EngineBlock& block, int32_t u,
	                        std::span<const uint32_t, kNumSources> phase_increments, Pass& pass);

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
	/// The sine operator without feedback, four samples at a time (NEON).
	static void render_sine_wave(int32_t* buffer_start, int32_t num_samples, uint32_t phase_now, int32_t amplitude,
	                             uint32_t phase_increment, bool add, int32_t amplitude_increment);
	/// The carrier frequency-modulated by @p fm_buffer without feedback, added, four samples at a time (NEON).
	static void render_fm_add(int32_t* buffer_start, int32_t num_samples, int32_t* fm_buffer, uint32_t phase_now,
	                          int32_t amplitude, uint32_t phase_increment, int32_t amplitude_increment);

	// The state only FM uses, indexed [unison part][modulator]
	std::array<std::array<uint32_t, kNumModulators>, kMaxNumVoicesUnison> modulator_phase_{};
	std::array<std::array<uint32_t, kNumModulators>, kMaxNumVoicesUnison> modulator_phase_increment_{}; // or kInactive
	std::array<std::array<int32_t, kNumModulators>, kMaxNumVoicesUnison> modulator_feedback_{};
	std::array<int32_t, kNumModulators> modulator_amplitude_last_time_{}; ///< The modulator volumes' de-zipper
};

} // namespace deluge::synth
