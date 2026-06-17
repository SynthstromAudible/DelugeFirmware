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

#include "definitions_cxx.hpp"                   // LoopType / MarkerType
#include "model/voice/source_component_render.h" // deluge::voice::SourceRenderResult
#include <cstdint>

class Sample;
class Sound;
class Source;
class Voice;
class VoiceSample;
class VoiceSamplePlaybackGuide;
class ParamManagerForTimeline;

/// @brief The sample-playback source engine (OscType::SAMPLE) for one voice-source.
///
/// Wraps a pooled @ref VoiceSample — the low-level resampling/time-stretch/cache reader — and owns the SAMPLE
/// arm of the voice lifecycle: solicit/restart at note-on, the per-block render (pitch+speed derivation,
/// late-start, caching decision, the VoiceSample::render call and stereo-unison mix-down), and teardown. Like
/// @ref deluge::dsp::DX7Engine and @ref InputEngine it is a *foreign*, app-coupled engine (it reaches storage/,
/// model/sample, AudioEngine, the allocator and ParamManager), so it is a standalone class, not a
/// synth::SourceEngine. Ownership of the pooled VoiceSample is **explicit** (released in @ref unassign, never in
/// a destructor) — the per-source objects are pooled and reused in place.
///
/// @note Per the L1 reframe this is a **source component** (usable inside Subtractive/RingMod), not a swappable
/// engine — see `docs/dev/synth_l1_engine_architecture.md`. The `*Engine` name predates that doc; the rename
/// rides the engine-forming slices (likely behind the `SampleSource` port).
class SampleEngine {
public:
	/// Note-on for the SAMPLE source: solicit (or restart in place) the pooled VoiceSample and set up its
	/// clusters for initial play. Returns false only on a hard failure that should silence the whole Voice (out
	/// of VoiceSamples / cluster setup failed); true otherwise — including the "unplayable / no file" and
	/// "samples-late" early-outs, which keep the Voice sounding. Mirrors the legacy
	/// VoiceUnisonPartSource::noteOn SAMPLE block exactly.
	bool noteOn(VoiceSamplePlaybackGuide* guide, uint32_t samplesLate, int32_t priorityRating);

	/// Render one block of sample playback, adding into @p oscBuffer. @p phaseIncrement is the already
	/// pitch-adjusted read rate; @p phaseIncrementStoredValue is the pre-modulation value (needed by the
	/// late-start maths). @p stereoUnisonScratch is the caller's spare buffer used only when @p stereoUnison.
	/// Reaches back into @p voice / @p sound / @p paramManager for the guide, params and caching policy — the
	/// app-coupling this engine deliberately localises.
	deluge::voice::SourceRenderResult render(Voice& voice, Sound& sound, ParamManagerForTimeline* paramManager,
	                                         int32_t s, int32_t* __restrict__ oscBuffer, int32_t numSamples,
	                                         bool stereoBuffer, int32_t sourceAmplitude, int32_t amplitudeIncrement,
	                                         uint32_t phaseIncrement, uint32_t phaseIncrementStoredValue,
	                                         bool stereoUnison, int32_t amplitudeL, int32_t amplitudeR,
	                                         int32_t* stereoUnisonScratch);

	/// Free the pooled VoiceSample back to AudioEngine (no-op if none held).
	void unassign(bool deletingSong);

	/// Note-off when the sample has a loop-end point. Returns false if the source should be unassigned now.
	/// Forwards to VoiceSample::noteOffWhenLoopEndPointExists.
	bool noteOffAtLoopEnd(Voice* voice, VoiceSamplePlaybackGuide* guide);

	/// React to a sample zone/marker change. Returns false if this source is now out of bounds and should be
	/// unassigned. Forwards to VoiceSample::sampleZoneChanged.
	bool zoneChanged(VoiceSamplePlaybackGuide* guide, Sample* sample, bool reversed, MarkerType markerType,
	                 LoopType loopingType, int32_t priorityRating);

	/// Low-level play position in bytes (for the auto-release length calculation). Forwards to VoiceSample.
	int32_t getPlayByteLowLevel(Sample* sample, VoiceSamplePlaybackGuide* guide);

	/// The wrapped low-level reader, or nullptr if none is held. Exposed for the few app-side call-sites that
	/// still reach the VoiceSample directly (GUI play-cursor read-out, the unison-count-change clone below).
	[[nodiscard]] VoiceSample* voiceSample() const { return voiceSample_; }

	/// Unison-count increase: solicit a fresh VoiceSample for this (new) part and clone @p from's low-level
	/// reader state into it. Returns false if none could be solicited. Mirrors the legacy Sound::setNumUnison
	/// clone (the caller still does the per-part stopUsingCache afterwards).
	bool cloneVoiceSampleFrom(const SampleEngine& from);

private:
	bool getPitchAndSpeedParams(Source* source, VoiceSamplePlaybackGuide* guide, uint32_t* phaseIncrement,
	                            uint32_t* timeStretchRatio, uint32_t* noteLengthInSamples);
	uint32_t getSpeedParamForNoSyncing(Source* source, int32_t phaseIncrement, int32_t pitchAdjustNeutralValue);

	VoiceSample* voiceSample_ = nullptr;
};
