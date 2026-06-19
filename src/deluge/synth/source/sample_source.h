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

#include "synth/source/sample_playback_port.h" // SamplePlaybackPort
#include "synth/source/source.h"               // deluge::synth::Source, SourceRenderContext, SourceRenderResult
#include <cstdint>

/// @brief The sample-playback source (OscType::SAMPLE) for one voice-source — the library-resident render
/// orchestration of @ref deluge::synth::Source.
///
/// Drives one pooled voice-sample reader through a @ref SamplePlaybackPort: derives this block's
/// pitch + time-stretch (incl. sequence-sync), decides + sets up the resampling cache, handles the mid-note late
/// start, calls the port's render, and folds the result into the voice's oscBuffer (incl. the stereo-unison
/// mix-down). All the app-coupled machinery — the `VoiceSample` reader, the SD cluster cache, time-stretch, the
/// pool, storage, transport — lives App-side behind the port (@ref SamplePlaybackAdapter); this class names none
/// of it. The note lifecycle (note-on/off, zone change, clone, cursor read-out) is the adapter's own API.
///
/// @note Per the L1 reframe this is a **source component** (usable inside Subtractive/RingMod), not a swappable
/// engine — see `docs/dev/synth_l1_engine_architecture.md`.
namespace deluge::synth {

class SampleSource : public Source {
public:
	explicit SampleSource(SamplePlaybackPort* port) : port_(port) {}

	/// Render one block of sample playback into @c ctx.oscBuffer (the @ref deluge::synth::Source seam). Reads the
	/// per-block voice values in @p ctx (porta, priority, pre-modulation phaseIncrementStoredValue, the hoisted
	/// caching pitch-mod flag); the resampling/time-stretch/cache machinery + the per-note source/guide live behind
	/// the port. No @c Voice& reach.
	SourceRenderResult renderSource(const SourceRenderContext& ctx) override;

private:
	/// Derive this block's pitch (@p phaseIncrement, mutated in place) + @p timeStretchRatio (+ @p noteLengthInSamples
	/// when syncing) from the port. @return false ⇒ unassign (sync would exceed the 32× speed limit).
	bool getPitchAndSpeedParams(uint32_t* phaseIncrement, uint32_t* timeStretchRatio, uint32_t* noteLengthInSamples);
	/// The non-syncing time-stretch ratio: counteract pitch when pitch/speed are independent, then apply the manual
	/// time-stretch amount. Also used for the late-start position (with the non-modulated phaseIncrement).
	uint32_t getSpeedParamForNoSyncing(int32_t phaseIncrement, int32_t pitchAdjustNeutralValue);

	SamplePlaybackPort* port_;
};

} // namespace deluge::synth
