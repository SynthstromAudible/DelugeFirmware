/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "arm_neon_shim.h"

#include "definitions_cxx.hpp"
#include "dsp/interpolate/interpolate.h"
#include "libdeluge/sample_source.h"
#include <array>
#include <cstdint>
#define REASSESSMENT_ACTION_STOP_OR_LOOP 0
#define REASSESSMENT_ACTION_NEXT_CLUSTER 1

class VoiceSamplePlaybackGuide;
class Voice;
class Sample;
struct DelugeSampleSource; // per-reader residency cursor over the region port (libdeluge/sample_source.h)
class TimeStretcher;
class SamplePlaybackGuide;

/// @brief The region port's residency tri-state, as a SCOPED enum.
///
/// Deliberately not `DelugeRegionState` itself: that is a plain C enum whose `DELUGE_REGION_READY`
/// is 1, so `if (!state)` compiles at every call site and is silently wrong (`!UNAVAILABLE` is
/// `false`). A scoped enum makes the compiler find every site that must be updated.
enum class RegionOutcome : uint8_t {
	Ready,       ///< The region is resident and pinned; `region_` is set.
	Loading,     ///< Reserved and enqueued, not yet filled. Retry later; do NOT treat as failure.
	Unavailable, ///< Out of range, or the port could not reserve at all. A genuine failure.
};

/// @brief Map the C-ABI tri-state onto RegionOutcome.
///
/// Any unrecognised value is treated as Unavailable, the conservative choice: a caller that drops a
/// voice on an unknown state is safe, one that waits forever is not.
/// @param state The port's residency state.
/// @return The corresponding scoped outcome.
constexpr RegionOutcome regionOutcomeFrom(DelugeRegionState state) {
	switch (state) {
	case DELUGE_REGION_READY:
		return RegionOutcome::Ready;
	case DELUGE_REGION_LOADING:
		return RegionOutcome::Loading;
	default:
		return RegionOutcome::Unavailable;
	}
}

class SampleLowLevelReader {
public:
	SampleLowLevelReader() = default;
	virtual ~SampleLowLevelReader();
	explicit SampleLowLevelReader(SampleLowLevelReader&, bool stealReasons = false);
	SampleLowLevelReader(SampleLowLevelReader&& other) noexcept;
	SampleLowLevelReader& operator=(const SampleLowLevelReader& other) = delete;
	SampleLowLevelReader& operator=(SampleLowLevelReader&& other) noexcept;

	void unassignAllReasons(bool wontBeUsedAgain);
	void jumpForwardLinear(int32_t numChannels, int32_t byteDepth, uint32_t bitMask, int32_t jumpAmount,
	                       int32_t phaseIncrement);
	void jumpForwardZeroes(int32_t bufferSize, int32_t numChannels, int32_t phaseIncrement);
	void fillInterpolationBufferRetrospectively(Sample* sample, int32_t bufferSize, int32_t startI,
	                                            int32_t playDirection);
	void jumpBackSamples(Sample* sample, int32_t numToJumpBack, int32_t playDirection);
	/// @brief Position the play cursor at @p bytePosWithinNewCluster within the cluster whose resident
	///        base is @p clusterBase, and recompute the reassessment window for it.
	///
	/// Called once playback has just crossed into a new resident cluster.
	/// @param guide                   Playback guide supplying playback state.
	/// @param sample                  The sample being played.
	/// @param clusterBase             Resident base of the new cluster (the region port's
	///                                `region.payload_base`, sourced by the caller from the region it
	///                                just acquired).
	/// @param bytePosWithinNewCluster Byte offset of the play position within that cluster.
	/// @param byteDepth               Byte depth of the sample; currently unused by this function.
	void setupForPlayPosMovedIntoNewCluster(SamplePlaybackGuide* guide, Sample* sample, char* clusterBase,
	                                        int32_t bytePosWithinNewCluster, int32_t byteDepth);
	/// @brief Set up the reader's clusters for the start of playback, at the guide's start byte.
	/// @return Ready when playback can begin now; Loading when the first cluster is reserved but its
	///         fill is still in flight (the caller may defer rather than drop); Unavailable when the
	///         sample is unplayable or the start byte is out of range.
	RegionOutcome setupClusersForInitialPlay(SamplePlaybackGuide* guide, Sample* sample, int32_t byteOvershoot = 0,
	                                         bool justLooped = false, int32_t priorityRating = 1);
	bool moveOnToNextCluster(SamplePlaybackGuide* guide, Sample* sample, int32_t priorityRating = 1);
	bool changeClusterIfNecessary(SamplePlaybackGuide* guide, Sample* sample, bool loopingAtLowLevel,
	                              int32_t priorityRating = 1);
	bool considerUpcomingWindow(SamplePlaybackGuide* guide, Sample* sample, int32_t* numSamples, int32_t phaseIncrement,
	                            bool loopingAtLowLevel, int32_t bufferSize, bool allowEndlessSilenceAtEnd = false,
	                            int32_t priorityRating = 1);
	void setupReassessmentLocation(SamplePlaybackGuide* guide, Sample* sample);
	void misalignPlaybackParameters(Sample* sample);
	void realignPlaybackParameters(Sample* sample);
	bool reassessReassessmentLocation(SamplePlaybackGuide* guide, Sample* sample, int32_t priorityRating);
	int32_t getPlayByteLowLevel(Sample* sample, SamplePlaybackGuide* guide,
	                            bool compensateForInterpolationBuffer = false);

	/// @brief Set up the reader's clusters to play from @p startPlaybackAtByte.
	/// @return Ready when playback can begin now; Loading when the covering cluster is reserved but not
	///         yet filled; Unavailable when @p startPlaybackAtByte lies outside the sample's audio data.
	RegionOutcome setupClustersForPlayFromByte(SamplePlaybackGuide* guide, Sample* sample, int32_t startPlaybackAtByte,
	                                           int32_t priorityRating);

	[[nodiscard]] virtual bool shouldObeyMarkers() const { return false; }

	void readSamplesNative(int32_t** __restrict__ oscBufferPos, int32_t numSamplesTotal, Sample* sample,
	                       int32_t jumpAmount, int32_t numChannels, int32_t numChannelsAfterCondensing,
	                       int32_t* amplitude, int32_t amplitudeIncrement, TimeStretcher* timeStretcher = nullptr,
	                       bool bufferingToTimeStretcher = false);

	void readSamplesResampled(int32_t** __restrict__ oscBufferPos, int32_t numSamples, Sample* sample,
	                          int32_t jumpAmount, int32_t numChannels, int32_t numChannelsAfterCondensing,
	                          int32_t phaseIncrement, int32_t* amplitude, int32_t amplitudeIncrement,
	                          int32_t bufferSize, bool writingCache, char** __restrict__ cacheWritePos,
	                          bool* doneAnySamplesYet, TimeStretcher* timeStretcher, bool bufferingToTimeStretcher,
	                          int32_t whichKernel);

	bool readSamplesForTimeStretching(int32_t* oscBufferPos, SamplePlaybackGuide* guide, Sample* sample,
	                                  int32_t numSamples, int32_t numChannels, int32_t numChannelsAfterCondensing,
	                                  int32_t phaseIncrement, int32_t amplitude, int32_t amplitudeIncrement,
	                                  bool loopingAtLowLevel, int32_t jumpAmount, int32_t bufferSize,
	                                  TimeStretcher* timeStretcher, bool bufferingToTimeStretcher,
	                                  int32_t whichPlayHead, int32_t whichKernel, int32_t priorityRating);
	/// @brief Reconcile lease ownership after copying `region_` from @p other into this reader.
	///
	/// The caller has already copied `region_` (this reader's residency, including its independent
	/// lease via `region_.lease`) from @p other -- via the copy/move constructors' initializer list, or
	/// the move-assignment operator just before calling here. This function only settles who owns the
	/// leases that copy implies.
	///
	/// If @p stealReasons, `this` takes over @p other's residency wholesale: the region-port cursor
	/// (and the leases it holds on the current/prefetch chunks) transfers from @p other to `this`, and
	/// `other.region_` is cleared so @p other won't release the independent lease `this` now owns.
	/// Otherwise (the non-stealing copy used by `TimeStretcher::olderPartReader`), `this` takes its own
	/// independent hard lease on the chunk `region_` describes, so both readers pin the shared chunk and
	/// the refcount rises by one; this reader's `source_` is left null and re-opens lazily on the next
	/// assignClusters().
	/// @param other        The reader whose residency was just copied into this one.
	/// @param stealReasons True to steal @p other's residency outright; false to take an additional,
	///                     independent lease alongside it.
	void adoptResidencyFrom(SampleLowLevelReader& other, bool stealReasons);

	void bufferIndividualSampleForInterpolation(int32_t numChannels, int32_t byteDepth, char* playPosNow);
	void bufferZeroForInterpolation(int32_t numChannels);

	/// @brief Does the reader currently hold a resident region? The reader's sole presence query — true
	///        exactly when `region_` is populated (and thus the reader holds its one independent hard
	///        lease on that chunk, via `region_.lease`).
	[[nodiscard]] bool hasCurrentRegion() const { return region_.payload_base != nullptr; }

	uint32_t oscPos{};
	char* currentPlayPos{};
	char* reassessmentLocation{};
	char* clusterStartLocation{}; // You're allowed to read from this location, but not move any further "back" past it
	uint8_t reassessmentAction{};
	int8_t interpolationBufferSizeLastTime{}; // 0 if was previously switched off

	deluge::dsp::Interpolator interpolator_{};

protected:
	/// @name Residency cursor and its retained region
	///
	/// @note Protected, not private: the cache-replay resync in `VoiceSample::render` (the subclass)
	///       acquires the uncached resume cluster through this same cursor -- that is what keeps
	///       `region_` tracking the CACHE position instead of the stale pre-cache one. No other class
	///       can reach them.
	/// @{

	/// @brief Per-reader residency cursor over the region port (libdeluge/sample_source.h), opened
	///        lazily against the sample's `stream()` (see ensureSource). Holds the current + prefetch
	///        pins.
	DelugeSampleSource* source_ = nullptr;
	void* source_backing_ = nullptr; ///< the &sample->stream() `source_` was opened against (reader reuse)

	/// @brief The reader's sole residency representation: the last region acquired through the port
	///        (assignClusters / moveOnToNextCluster / the cache-resync).
	///
	/// Its `payload_base` is the source of the interpolation window's base pointer -- the
	/// `clusterStartLocation` look-behind floor and the `reassessmentLocation` trailing/front slack
	/// reach are computed from it in setupReassessmentLocation(). `region_.payload_base` gates
	/// presence (hasCurrentRegion()), `region_.region_index` supplies the current cluster index, and
	/// `region_.lease` IS the resident chunk pointer.
	/// @note The reader holds exactly ONE independent hard lease on it while `region_` is populated
	///       (taken at each acquire, released in unassignAllReasons). The port cursor `source_` holds
	///       its own separate current/prefetch leases; this independent lease is what keeps the chunk
	///       pinned for the reader's residency lifetime, including the non-stealing copy
	///       (TimeStretcher::olderPartReader) which has no `source_` of its own.
	DelugeSampleRegion region_{};

	/// @}

	/// @brief Open `source_` once for @p sample (re-opening if the reader is reused for a new sample).
	void ensureSource(Sample* sample);

private:
	/// @brief The port geometry for @p sample — the immutable per-sample fields parsed above the port.
	[[nodiscard]] static DelugeSampleGeometry geometryFor(const Sample& sample);

	/// @brief Acquire @p clusterIndex's region through the port and adopt it as this reader's `region_`.
	/// @return Ready once `region_` is populated and pinned; Loading if the chunk is reserved but its
	///         fill has not landed (the caller decides whether to retry); Unavailable on a genuine
	///         failure (no cursor, or the port could not reserve at all).
	RegionOutcome assignClusters(SamplePlaybackGuide* guide, Sample* sample, int32_t clusterIndex,
	                             int32_t priorityRating);
	/// @brief Fills interpolation slots `fromIndex` down to 0 with silence.
	///
	/// Used wherever there is no audio left to read. While there is no data there is also no play
	/// position - changeClusterIfNecessary() nulls it - so currentPlayPos is reused as the count of
	/// silent samples emitted, which is what the buffer-size bound tests.
	/// @return false once the whole interpolation buffer has been filled with silence.
	bool fillInterpolationBufferWithSilence(int32_t fromIndex, int32_t interpolationBufferSize);

	bool fillInterpolationBufferForward(SamplePlaybackGuide* guide, Sample* sample, int32_t interpolationBufferSize,
	                                    bool loopingAtLowLevel, int32_t numSpacesToFill, int32_t priorityRating);
};
