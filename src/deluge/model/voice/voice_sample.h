/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "dsp/fixed/sample_read/read_head.h"
#include "dsp/fixed/timestretch/cache_tail.h"
#include "model/sample/sample_read_lease.h"
#include <array>
#include <optional>

enum class LateStartAttemptStatus {
	SUCCESS = 0,
	FAILURE = 1,
	WAIT = 2,
};
namespace deluge::synth {
class SampleStretcher;
}
class SampleCache;
class Sample;
class VoiceSamplePlaybackGuide;
class SamplePlaybackGuide;
class SourceResources;

/// @brief A sample voice's playback: two read heads (the one it plays from and a spare, which a stretcher uses as its
/// older head), each with its lease on the chunk it reads, and the stretcher or cache it reads through.
class VoiceSample final {
public:
	using ReadHead = deluge::dsp::fixed::sample_read::ReadHead;
	using Direction = deluge::dsp::fixed::sample_read::Direction;

	VoiceSample() = default;
	VoiceSample(const VoiceSample&) = delete;
	~VoiceSample() { endTimeStretching(); }
	VoiceSample& operator=(const VoiceSample& other) = delete;
	// Deleted: the defaulted memberwise move-assign would overwrite the stretcher and cache without
	// endTimeStretching(), leaking whatever the destination currently owns. VoiceSamples are pool-managed and
	// referenced by pointer, so no object-level move-assign is needed; if one ever is, give it a body that releases
	// resources first.
	VoiceSample& operator=(VoiceSample&& other) = delete;

	void noteOn(SamplePlaybackGuide* guide, uint32_t samplesLate, int32_t priorityRating);
	bool noteOffWhenLoopEndPointExists(VoiceSamplePlaybackGuide* voiceSource, int32_t priorityRating);

	void setupCacheLoopPoints(SamplePlaybackGuide* voiceSource, Sample* sample, LoopType loopingType);
	LateStartAttemptStatus attemptLateSampleStart(SamplePlaybackGuide* voiceSource, Sample* sample,
	                                              int64_t rawSamplesLate, int32_t numSamples = 0);
	void endTimeStretching();
	bool render(SamplePlaybackGuide* guide, deluge::dsp::Q<31>* oscBuffer, int32_t numSamples, Sample* sample,
	            int32_t numChannels, LoopType loopingType, int32_t phaseIncrement, int32_t timeStretchRatio,
	            int32_t amplitude, int32_t amplitudeIncrement, int32_t bufferSize,
	            InterpolationMode desiredInterpolationMode, int32_t priorityRating);
	void beenUnassigned(bool wontBeUsedAgain);

	// AudioClips don't obey markers because they "fudge" instead.
	// Or if fudging can't happen cos no pre-margin, then
	// AudioClip::doTickForward() manually forces restart.
	[[nodiscard]] bool shouldObeyMarkers() const { return (cache == nullptr && stretcher == nullptr && !forAudioClip); }

	bool sampleZoneChanged(SamplePlaybackGuide* voiceSource, Sample* sample, bool reversed, MarkerType markerType,
	                       LoopType loopingType, int32_t priorityRating, bool forAudioClip = false);
	int32_t getPlaySample(Sample* sample, SamplePlaybackGuide* guide);
	bool stopUsingCache(SamplePlaybackGuide* guide, Sample* sample, int32_t priorityRating, bool loopingAtLowLevel);
	bool possiblySetUpCache(InterpolationMode interpolationMode, SamplePlaybackGuide* guide, int32_t phaseIncrement,
	                        int32_t timeStretchRatio, int32_t priorityRating, LoopType loopingType);
	bool fudgeTimeStretchingToAvoidClick(Sample* sample, SamplePlaybackGuide* guide, int32_t phaseIncrement,
	                                     int32_t numSamplesTilLoop, int32_t playDirection, int32_t priorityRating);

	uint32_t pendingSamplesLate = 0; // This isn't used for AudioClips. And for samples in STRETCH mode, the exact
	                                 // number isn't relevant - it gets recalculated
	/// The stretcher while this voice-sample stretches, from the engine's pool; its newer head is the one the voice
	/// plays from, its older the spare.
	deluge::synth::SampleStretcher* stretcher = nullptr;
	[[nodiscard]] bool stretching() const { return stretcher != nullptr; }

	/// The heads and their leases: heads_[newer_] is the one the voice plays from; the other is the spare, a level copy
	/// or the previous newer after a hop while stretching, unseated when stretching ends.
	std::array<ReadHead::State, 2> heads_{};
	std::array<SampleReadLease, 2> leases_{};
	uint8_t newer_ = 0;

	/// @brief The head the voice plays from, and its lease bound for this call.
	[[nodiscard]] ReadHead::State& head() { return heads_[newer_]; }
	SampleReadLease& lease(Sample& sample, SamplePlaybackGuide& guide, int32_t priority_rating);
	/// @brief The facts A0 reads: the data's start, the guide's end byte, the layout.
	[[nodiscard]] static deluge::dsp::fixed::sample_read::ReadFacts readFacts(const Sample& sample,
	                                                                          const SamplePlaybackGuide& guide);

	// The head the voice plays from, as its other users reach it
	RegionOutcome setupClusersForInitialPlay(SamplePlaybackGuide* guide, Sample* sample, int32_t byte_overshoot = 0,
	                                         bool just_looped = false, int32_t priority_rating = 1);
	bool changeClusterIfNecessary(SamplePlaybackGuide* guide, Sample* sample, bool looping_at_low_level,
	                              int32_t priority_rating = 1);
	bool reassessReassessmentLocation(SamplePlaybackGuide* guide, Sample* sample, int32_t priority_rating);
	int32_t getPlayByteLowLevel(Sample* sample, SamplePlaybackGuide* guide,
	                            bool compensate_for_interpolation_buffer = false);
	void unassignAllReasons(bool wont_be_used_again);
	/// @brief The frame a low-level loop restarts at: the guide's start byte after a loop.
	[[nodiscard]] static int32_t loopStartFrame(SamplePlaybackGuide* guide, Sample* sample);
	[[nodiscard]] bool hasCurrentRegion() const { return leases_[newer_].holds_chunk(); }
	/// @brief The playing head's phase.
	[[nodiscard]] deluge::dsp::UQ<24> phase() const { return heads_[newer_].phase; }

	SampleCache* cache = nullptr;
	bool doneFirstRenderYet = false;
	bool fudging = false;
	bool forAudioClip = false;   // This is a wee bit of a hack - but we need to be able to know this
	bool writingToCache = false; // Value is only valid if cache assigned

private:
	bool weShouldBeTimeStretchingNow(Sample* sample, SamplePlaybackGuide* guide, int32_t numSamples,
	                                 int32_t phaseIncrement, int32_t timeStretchRatio, int32_t playDirection,
	                                 int32_t priorityRating, LoopType loopingType,
	                                 const deluge::dsp::fixed::timestretch::CacheWindow* leavingCache = nullptr);
	/// @brief Where this voice leaves the cache it reads, for a stretcher crossfading out of it (C1); nullopt when the
	/// cache's chunk there has gone.
	[[nodiscard]] std::optional<deluge::dsp::fixed::timestretch::CacheWindow> cacheWindow(SampleCache* leaving) const;
	void switchToReadingCacheFromWriting();
	bool stopReadingFromCache();

	int32_t cacheBytePos = 0;
	uint32_t cacheLoopLengthBytes = 0;
	int32_t cacheLoopEndPointBytes = 0; // 2147483647 means no looping. Will be set to sample end-point if looping
	                                    // there. Gets re-set to 2147483647 when note "released"
	int32_t cacheEndPointBytes = 0; // Will sometimes be the whole length of the sample. Wherever the red marker is. Or
	                                // a little further if it's the full length of the sample, to allow for timestretch
	                                // / interpolation ring-out
	Sample* stretchedSample_ = nullptr; ///< The sample the stretcher's leases are on, for its end.
};
