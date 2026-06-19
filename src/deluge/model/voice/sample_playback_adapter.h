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

#include "definitions_cxx.hpp"                 // LoopType / MarkerType
#include "synth/source/sample_playback_port.h" // synth::SamplePlaybackPort
#include <cstdint>

class Sample;
class Source;
class Voice;
class VoiceSample;
class VoiceSamplePlaybackGuide;

/// @brief The firmware adapter behind @ref deluge::synth::SamplePlaybackPort: one pooled @ref VoiceSample reader
/// for one voice-source. Holds the source's `VoiceSample` + the per-note `::Source`/guide cached at note-on,
/// soliciting/releasing from the `AudioEngine` voice-sample pool and answering the library `SampleSource`'s render
/// queries/ops from that state. This is the app-coupled half the library-resident `SampleSource` no longer
/// carries (storage, the SD cluster cache, time-stretch, the pool, `playbackHandler` all stay here).
///
/// It also owns the note **lifecycle** (solicit/release, note-off-at-loop-end, zone change, the GUI play-cursor
/// read-out, the unison-count-change clone) — app-typed methods called directly by the App, deliberately off the
/// port (the library shell never drives them). Ownership of the pooled `VoiceSample` is **explicit** (released in
/// @ref unassign, never in a destructor) — the per-source objects are pooled and reused in place.
class SamplePlaybackAdapter final : public deluge::synth::SamplePlaybackPort {
public:
	// --- App-side note lifecycle (called directly by the App, not via the port) ------------------------------

	/// Note-on for the SAMPLE source: cache the per-note @p source + @p guide (stable for the note), then solicit
	/// (or restart in place) the pooled VoiceSample and set up its clusters for initial play. Returns false only on
	/// a hard failure that should silence the whole Voice; true otherwise (incl. the unplayable / samples-late
	/// early-outs). Mirrors the legacy SampleSource::noteOn exactly.
	bool noteOn(::Source* source, VoiceSamplePlaybackGuide* guide, uint32_t samplesLate, int32_t priorityRating);

	/// Free the pooled VoiceSample back to AudioEngine (no-op if none held); clear the cached source/guide.
	void unassign(bool deletingSong);

	/// Note-off when the sample has a loop-end point. Returns false if the source should be unassigned now.
	bool noteOffAtLoopEnd(Voice* voice, VoiceSamplePlaybackGuide* guide);

	/// React to a sample zone/marker change. Returns false if this source is now out of bounds and should be
	/// unassigned.
	bool zoneChanged(VoiceSamplePlaybackGuide* guide, Sample* sample, bool reversed, MarkerType markerType,
	                 LoopType loopingType, int32_t priorityRating);

	/// Low-level play position in bytes (for the auto-release length calculation).
	int32_t getPlayByteLowLevel(Sample* sample, VoiceSamplePlaybackGuide* guide);

	/// The wrapped low-level reader, or nullptr if none is held (GUI play-cursor read-out + the clone below).
	[[nodiscard]] VoiceSample* voiceSample() const { return voiceSample_; }

	/// Unison-count increase: solicit a fresh VoiceSample for this (new) part and clone @p from's low-level reader
	/// state into it. Returns false if none could be solicited.
	bool cloneVoiceSampleFrom(const SamplePlaybackAdapter& from);

	// --- deluge::synth::SamplePlaybackPort (primitive queries/ops the library SampleSource drives) ------------
	[[nodiscard]] int32_t numChannels() const override;
	[[nodiscard]] int32_t sampleRate() const override;
	[[nodiscard]] int32_t neutralPhaseIncrement() const override;
	[[nodiscard]] int32_t sampleLengthAtSystemSampleRate() const override;
	[[nodiscard]] bool isStretchRepeatMode() const override;
	[[nodiscard]] bool pitchAndSpeedIndependent() const override;
	[[nodiscard]] int32_t timeStretchAmount() const override;
	[[nodiscard]] uint32_t sequenceSyncLengthTicks() const override;
	[[nodiscard]] LoopType loopingType() const override;
	[[nodiscard]] LoopPoints loopPoints() const override;
	[[nodiscard]] uint64_t timePerInternalTickBig() const override;
	[[nodiscard]] int64_t syncedNumSamplesIn() const override;
	[[nodiscard]] int32_t numSamplesLaggingBehindSync() const override;
	[[nodiscard]] uint32_t adjustPitchToCorrectDriftFromSync(uint32_t phaseIncrement) const override;
	[[nodiscard]] int32_t interpolationBufferSize(uint32_t phaseIncrement) const override;
	[[nodiscard]] uint32_t pendingSamplesLate() const override;
	[[nodiscard]] bool doneFirstRenderYet() const override;
	LateStartOutcome attemptLateStart(int64_t rawSamplesLate, int32_t numSamples) override;
	bool possiblySetUpCache(int32_t phaseIncrement, int32_t timeStretchRatio, int32_t priorityRating,
	                        LoopType loopingType) override;
	bool render(int32_t* buffer, int32_t numSamples, LoopType loopingType, int32_t phaseIncrement,
	            int32_t timeStretchRatio, int32_t sourceAmplitude, int32_t amplitudeIncrement,
	            int32_t interpolationBufferSize, int32_t priorityRating) override;

private:
	VoiceSample* voiceSample_ = nullptr;
	/// The per-note source config + playback guide, cached at note-on (stable for the note; `sound.sources[s]` /
	/// `voice.guides[s]` are fixed members). The render queries above read these. Cleared in @ref unassign.
	::Source* source_ = nullptr;
	VoiceSamplePlaybackGuide* guide_ = nullptr;
};
