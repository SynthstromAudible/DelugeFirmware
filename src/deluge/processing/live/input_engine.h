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

#include "definitions_cxx.hpp" // OscType
#include <cstdint>

class LivePitchShifter;

/// @brief The live-input source engine (INPUT_L / INPUT_R / INPUT_STEREO) for one voice-source.
///
/// Reads AudioEngine's live-input ring and sums it into the voice's oscBuffer, pitch-shifting through a
/// lazily-created @ref LivePitchShifter when the played note is off the input's natural rate. Like @ref
/// deluge::dsp::DX7Engine it is a *foreign*, app-coupled engine (it reaches AudioEngine + the allocator), so it is
/// a standalone class, not a synth::Source. It owns the pooled pitch-shifter; ownership is **explicit**
/// (freed in @ref unassign, never in a destructor) — the per-source objects are pooled and reused in place.
///
/// @note Per the L1 reframe this is a **source component** (usable inside Subtractive/RingMod), not a swappable
/// engine — see `docs/dev/synth_l1_engine_architecture.md`. The `*Engine` name predates that doc; the rename
/// rides the engine-forming slices (likely behind a live-input port).
class InputEngine {
	LivePitchShifter* livePitchShifter_ = nullptr;

public:
	/// Render one block, *summing* the live input into @p oscBuffer. @p oscType selects channel routing
	/// (L / R / stereo, with the internal-mic + condense rules); @p phaseIncrement != unity engages the
	/// pitch-shifter. @p interpolationBufferSize is precomputed by Voice (only used on the pitch-shift arm).
	/// When @p stereoUnison is set, oscBuffer is an interleaved stereo buffer and this unison part's
	/// mono input is panned across it with @p amplitudeL / @p amplitudeR (the stereo-unison-spread fix).
	void render(OscType oscType, int32_t* oscBuffer, int32_t numSamples, uint32_t phaseIncrement,
	            int32_t sourceAmplitude, int32_t amplitudeIncrement, bool stereoBuffer, bool stereoUnison,
	            int32_t amplitudeL, int32_t amplitudeR, int32_t interpolationBufferSize);

	/// Free the pooled pitch-shifter (no-op if none held).
	void unassign();
};
