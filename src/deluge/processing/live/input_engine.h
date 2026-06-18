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

#include "definitions_cxx.hpp"   // OscType
#include "synth/source/source.h" // synth::Source, SourceRenderContext, SourceRenderResult
#include <cstdint>

class LivePitchShifter;

/// @brief The live-input source (INPUT_L / INPUT_R / INPUT_STEREO) for one voice-source — a @ref synth::Source.
///
/// Reads AudioEngine's live-input ring and sums it into the voice's oscBuffer, pitch-shifting through a
/// lazily-created @ref LivePitchShifter when the played note is off the input's natural rate. It is a *foreign*,
/// app-coupled source (it reaches AudioEngine + the allocator), so — like @ref deluge::dsp::DX7Engine — it stays
/// app-side and keeps those reaches **internal**, behind the @ref synth::Source the voice sees; there is no
/// separate live-input port. It owns the pooled pitch-shifter; ownership is **explicit** (freed in @ref unassign,
/// never in a destructor) — the per-source objects are pooled and reused in place.
class InputEngine : public deluge::synth::Source {
	LivePitchShifter* livePitchShifter_ = nullptr;

public:
	/// Render this live-input source's contribution into @c ctx.oscBuffer (always ACTIVE — live input never
	/// self-unassigns). The @ref deluge::synth::Source seam; the channel-routing/pitch-shift render is the private
	/// @ref render below.
	deluge::synth::SourceRenderResult renderSource(const deluge::synth::SourceRenderContext& ctx) override;

	/// Free the pooled pitch-shifter (no-op if none held).
	void unassign();

private:
	/// Render one block, *summing* the live input into @p oscBuffer. @p oscType selects channel routing
	/// (L / R / stereo, with the internal-mic + condense rules); @p phaseIncrement != unity engages the
	/// pitch-shifter. @p interpolationBufferSize is precomputed by Voice (only used on the pitch-shift arm).
	/// When @p stereoUnison is set, oscBuffer is an interleaved stereo buffer and this unison part's
	/// mono input is panned across it with @p amplitudeL / @p amplitudeR (the stereo-unison-spread fix).
	void render(OscType oscType, int32_t* oscBuffer, int32_t numSamples, uint32_t phaseIncrement,
	            int32_t sourceAmplitude, int32_t amplitudeIncrement, bool stereoBuffer, bool stereoUnison,
	            int32_t amplitudeL, int32_t amplitudeR, int32_t interpolationBufferSize);
};
