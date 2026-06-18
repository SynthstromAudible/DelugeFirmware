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

#include "definitions_cxx.hpp"   // LoopType / MarkerType
#include "synth/source/source.h" // deluge::synth::Source, SourceRenderContext, SourceRenderResult
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
/// @ref deluge::dsp::DX7Source and @ref InputSource it is a *foreign*, app-coupled engine (it reaches storage/,
/// model/sample, AudioEngine, the allocator and ParamManager), so it is a standalone class, not a
/// synth::Source. Ownership of the pooled VoiceSample is **explicit** (released in @ref unassign, never in
/// a destructor) — the per-source objects are pooled and reused in place.
///
/// @note Per the L1 reframe this is a **source component** (usable inside Subtractive/RingMod), not a swappable
/// engine — see `docs/dev/synth_l1_engine_architecture.md`. The `*Engine` name predates that doc; the rename
/// rides the engine-forming slices (likely behind the `SampleSource` port).
class SampleSource : public deluge::synth::Source {
public:
	/// Note-on for the SAMPLE source: cache the per-note @p source + @p guide (stable for the note, like DX7's
	/// patch), then solicit (or restart in place) the pooled VoiceSample and set up its clusters for initial play.
	/// Returns false only on a hard failure that should silence the whole Voice (out of VoiceSamples / cluster
	/// setup failed); true otherwise — including the "unplayable / no file" and "samples-late" early-outs, which
	/// keep the Voice sounding. Mirrors the legacy VoiceUnisonPartSource::noteOn SAMPLE block exactly.
	bool noteOn(::Source* source, VoiceSamplePlaybackGuide* guide, uint32_t samplesLate, int32_t priorityRating);

	/// Render one block of sample playback into @c ctx.oscBuffer (the @ref deluge::synth::Source seam). Reads the
	/// per-note source/guide cached at note-on + the per-block voice values in @p ctx (porta, priority,
	/// pre-modulation phaseIncrementStoredValue, the hoisted caching pitch-mod flag); the VoiceSample
	/// resampling/time-stretch/cache machinery + stereo-unison mix-down are unchanged. No @c Voice& reach.
	deluge::synth::SourceRenderResult renderSource(const deluge::synth::SourceRenderContext& ctx) override;

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
	bool cloneVoiceSampleFrom(const SampleSource& from);

private:
	// NB ::Source is the global preset-side class; the unqualified `Source` here is this class's injected
	// base-class name (deluge::synth::Source), so the preset type must be written ::Source throughout.
	bool getPitchAndSpeedParams(::Source* source, VoiceSamplePlaybackGuide* guide, uint32_t* phaseIncrement,
	                            uint32_t* timeStretchRatio, uint32_t* noteLengthInSamples);
	uint32_t getSpeedParamForNoSyncing(::Source* source, int32_t phaseIncrement, int32_t pitchAdjustNeutralValue);

	VoiceSample* voiceSample_ = nullptr;
	/// The per-note source config + playback guide, cached at note-on (stable for the note — `sound.sources[s]` /
	/// `voice.guides[s]` are fixed members; mirrors DX7's `patch_`). Lets `render` shed its `Voice&` reach. Cleared
	/// in @ref unassign.
	::Source* source_ = nullptr;
	VoiceSamplePlaybackGuide* guide_ = nullptr;
};
