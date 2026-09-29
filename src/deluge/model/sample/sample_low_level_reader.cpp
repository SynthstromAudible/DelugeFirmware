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

#include "model/sample/sample_low_level_reader.h"
#include "definitions_cxx.hpp"
#include "dsp/interpolate/interpolate.h"
#include "dsp/stereo_sample.h"
#include "dsp/timestretch/time_stretcher.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "libdeluge/sample_source.h"
#include "model/sample/sample.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample_playback_guide.h"
#include "storage/cluster/cluster.h"
#ifdef DELUGE_HOST
#include "harness/streaming_underrun.h"
#endif

SampleLowLevelReader::~SampleLowLevelReader() {
	unassignAllReasons(false);
	// Close the region-port cursor last: it releases any leases still held on `source_`'s current /
	// prefetch chunks (held independently of the reader's own region lease, released just above).
	if (source_ != nullptr) {
		deluge_sample_source_close(source_);
		source_ = nullptr;
		source_backing_ = nullptr;
	}
}

// Open the per-reader region-port cursor once, lazily, at the first assignClusters() for this sample.
// Geometry is parsed here (above the port) from the immutable per-sample fields. A reader object can be
// reused for a different sample (voices are pooled), so re-open if the backing stream changed.
void SampleLowLevelReader::ensureSource(Sample* sample) {
	void* backing = &sample->stream();
	if (source_ != nullptr && source_backing_ != backing) {
		deluge_sample_source_close(source_);
		source_ = nullptr;
		source_backing_ = nullptr;
	}
	if (source_ == nullptr) {
		source_ = deluge_sample_source_open(backing, geometryFor(*sample));
		source_backing_ = backing;
	}
}

DelugeSampleGeometry SampleLowLevelReader::geometryFor(const Sample& sample) {
	return DelugeSampleGeometry{
	    .audio_data_start_bytes = sample.audioDataStartPosBytes,
	    .audio_data_length_bytes = sample.audioDataLengthBytes,
	    .cluster_size_bytes = static_cast<uint32_t>(Cluster::size),
	    .byte_depth = static_cast<uint8_t>(sample.byteDepth),
	    .num_channels = static_cast<uint8_t>(sample.numChannels),
	    .raw_data_format = static_cast<uint8_t>(sample.rawDataFormat),
	};
}

void SampleLowLevelReader::unassignAllReasons([[maybe_unused]] bool wontBeUsedAgain) {
	// Release the reader's single independent hard lease on its current region, if any. `region_.lease`
	// IS the chunk pointer (see deluge_sample_region_acquire_ex step 4); this lease was taken when
	// `region_` was populated (assignClusters / moveOnToNextCluster / the cache-resync) and is released
	// here. It may be the last lease on that chunk, so `region_.payload_base` can dangle afterwards —
	// clear `region_` so nothing reads a stale region before the next acquire repopulates it.
	if (region_.lease != 0) {
		deluge_sample_region_release(region_.lease);
	}
	region_ = {};
}

// Relative to audio file start, including WAV file header.
// May return negative number - I think particularly if we're going in reversed and just cancelled reading from cache
int32_t SampleLowLevelReader::getPlayByteLowLevel(Sample* sample, SamplePlaybackGuide* guide,
                                                  bool compensateForInterpolationBuffer) {
	if (hasCurrentRegion()) {
		// The reader's current region supplies both presence and the base/index for the arithmetic.
		uint32_t withinCluster = (currentPlayPos - reinterpret_cast<char*>(region_.payload_base)) + 4
		                         - sample->byteDepth; // Remove deliberate misalignment

		if (compensateForInterpolationBuffer && interpolationBufferSizeLastTime) {
			int32_t extraSamples = -(interpolationBufferSizeLastTime >> 1);
			// if (oscPos >= 8388608) extraSamples++; // This would be good, but we go one better and just copy this to
			// the new hop, in time stretching
			withinCluster += extraSamples * sample->numChannels * sample->byteDepth * guide->playDirection;
		}
		return (static_cast<int32_t>(region_.region_index) << Cluster::size_magnitude) + withinCluster;
	}
	// Hopefully this won't go negative, cos we're returning as unsigned...
	return (int32_t)guide->endPlaybackAtByte + (int32_t)(uintptr_t)currentPlayPos * guide->playDirection;
}

void SampleLowLevelReader::setupForPlayPosMovedIntoNewCluster(SamplePlaybackGuide* guide, Sample* sample,
                                                              char* clusterBase, int32_t bytePosWithinNewCluster,
                                                              [[maybe_unused]] int32_t byteDepth) {

#if ALPHA_OR_BETA_VERSION
	if (!hasCurrentRegion()) {
		FREEZE_WITH_ERROR("i022");
	}
#endif

	// Ok, now we've just moved the play-pos into a new Cluster, so do some setting up for that.
	// The resident cluster base is passed in as `clusterBase` -- the region port's `region.payload_base`,
	// which every caller sources from the region it just acquired.
	currentPlayPos = clusterBase + bytePosWithinNewCluster;

	setupReassessmentLocation(guide, sample);
}

void SampleLowLevelReader::misalignPlaybackParameters(Sample* sample) {
	reassessmentLocation = reassessmentLocation - 4 + sample->byteDepth;
	clusterStartLocation = clusterStartLocation - 4 + sample->byteDepth;
	currentPlayPos = currentPlayPos - 4 + sample->byteDepth;
}

void SampleLowLevelReader::realignPlaybackParameters(Sample* sample) {
	reassessmentLocation = reassessmentLocation + 4 - sample->byteDepth;
	currentPlayPos = currentPlayPos + 4 - sample->byteDepth;
}

// Returns false if fail, which can happen if we've actually ended up past the finalClusterIndex cos we were reading
// cache before. There is no guarantee that this won't put the reassessmentLocation back before the currentPlayPos,
// which is not generally allowed (though it'd be harmless for "natively" playing Samples). Caller must ensure safety
// here.
bool SampleLowLevelReader::reassessReassessmentLocation(SamplePlaybackGuide* guide, Sample* sample,
                                                        int32_t priorityRating) {
	// D_PRINTLN("reassessing");

	if (!hasCurrentRegion()) {
		return true; // Is this for if we've gone past the end of the audio data, while re-pitching / interpolating?
	}

	realignPlaybackParameters(sample);

	int32_t clusterIndex = static_cast<int32_t>(region_.region_index);

	// We may have ended up past the finalClusterIndex if we've just switched from using a cache.
	// This needs correcting, so "looping" can occur at next render. Must happen before setupReassessmentLocation() is
	// called.
	int32_t finalClusterIndex = guide->getFinalClusterIndex(sample, shouldObeyMarkers());
	if ((clusterIndex - finalClusterIndex) * guide->playDirection > 0) {
		D_PRINTLN("saving from being past finalCluster");
		// The play position overshot the final cluster during cache playback; the reader is moving to it.
		// Acquire the final region through the port (instead of reaching directly into the sample's raw
		// residency lookup, which a Rust backing would not mediate). A NotReady acquire is the old
		// "!finalCluster" give-up.
		int32_t bytePosWithinCluster = currentPlayPos - reinterpret_cast<char*>(region_.payload_base);
		bytePosWithinCluster += (clusterIndex - finalClusterIndex) * Cluster::size;

		DelugeSampleRegion finalRegion;
		if (!deluge_sample_region_acquire(source_, static_cast<uint32_t>(finalClusterIndex), guide->playDirection,
		                                  static_cast<uint32_t>(1), &finalRegion)) {
			return false;
		}
		// The acquire fuse-released the overshot current and pinned the final region; take the reader's
		// own independent pin on it and adopt it as region_ (mirrors assignClusters), so region_ and
		// currentPlayPos now describe the SAME chunk — closing the inconsistency the bypass left.
		deluge_sample_region_release(region_.lease);
		region_ = finalRegion;
		deluge_sample_region_retain(region_.lease);

		currentPlayPos = reinterpret_cast<char*>(region_.payload_base) + bytePosWithinCluster;
		clusterIndex = finalClusterIndex;
	}

	unassignAllReasons(false); // Can only do this after we've done the above stuff, which references clusters, which
	                           // this will clear
	if (assignClusters(guide, sample, clusterIndex, priorityRating) != RegionOutcome::Ready) {
		D_PRINTLN("reassessReassessmentLocation fail");
		return false;
	}
	setupReassessmentLocation(guide, sample);
	return true;
}

// There is no guarantee that this won't put the reassessmentLocation back before the currentPlayPos, which is not
// generally allowed (though it'd be harmless for "natively" playing Samples). Caller must ensure safety here. I only
// discovered this bug / requirement in Sept 2020. Going to assume that only reassessReassessmentLocation() really needs
// to do this...
void SampleLowLevelReader::setupReassessmentLocation(SamplePlaybackGuide* guide, Sample* sample) {

#if ALPHA_OR_BETA_VERSION
	if (!hasCurrentRegion()) {
		FREEZE_WITH_ERROR("i021");
	}
#endif

	int32_t bytesPerSample = (sample->byteDepth * sample->numChannels);

	// The current cluster index sources from the port's region_, retained at the acquire that just pinned
	// this region (assignClusters / moveOnToNextCluster).
	int32_t currentClusterIndex = region_.region_index;

	// The interpolation window's base pointer -- the reassessmentLocation trailing/front slack reach and
	// the clusterStartLocation look-behind floor below -- sources from the region port's
	// region_.payload_base, retained at the acquire (assignClusters / moveOnToNextCluster) that just
	// pinned this region. It is the resident chunk's payload().data() (the same payload whose >=4-byte
	// front slack and >=7-byte trailing slack were stitched at fill), so every offset computed off it --
	// and thus the interpolation reads into that slack in both play directions -- is byte-for-byte exact.
	char* regionBase = reinterpret_cast<char*>(region_.payload_base);

	int32_t endPlaybackAtByte;
	int32_t finalClusterIndex = guide->getFinalClusterIndex(sample, shouldObeyMarkers(), &endPlaybackAtByte);

	// Is this the final Cluster?
	if (currentClusterIndex == finalClusterIndex) {
		int32_t bytePosWithinClusterToStopAt = endPlaybackAtByte & (Cluster::size - 1);
		if (guide->playDirection == 1) {
			if (bytePosWithinClusterToStopAt == 0) {
				bytePosWithinClusterToStopAt = Cluster::size;
			}
		}

		else {
			if (bytePosWithinClusterToStopAt > Cluster::size - bytesPerSample) {
				bytePosWithinClusterToStopAt -= Cluster::size;
			}
		}

		reassessmentLocation = regionBase + bytePosWithinClusterToStopAt;
		reassessmentAction = REASSESSMENT_ACTION_STOP_OR_LOOP;
	}

	// Or if it's not the final Cluster...
	else {
		reassessmentAction = REASSESSMENT_ACTION_NEXT_CLUSTER;

		// Playing forwards
		if (guide->playDirection == 1) {

			uint32_t bytesBeforeCurrentClusterEnd =
			    (currentClusterIndex + 1) * Cluster::size - sample->audioDataStartPosBytes;
			int32_t excess = bytesBeforeCurrentClusterEnd % (uint8_t)bytesPerSample;
			if (excess == 0) {
				excess = bytesPerSample;
			}
			uint32_t endPosWithinCurrentCluster = Cluster::size + bytesPerSample - excess;

#if ALPHA_OR_BETA_VERSION
			if ((endPosWithinCurrentCluster + currentClusterIndex * Cluster::size - sample->audioDataStartPosBytes)
			    % bytesPerSample) {
				FREEZE_WITH_ERROR("E163");
			}
#endif
			reassessmentLocation = regionBase + endPosWithinCurrentCluster;
		}

		// Playing backwards
		else {

			uint32_t bytesBeforeCurrentClusterEnd =
			    currentClusterIndex * Cluster::size
			    - sample->audioDataStartPosBytes; // Well, it's really the "start" - the left-most edge
			int32_t excess = bytesBeforeCurrentClusterEnd % (uint8_t)bytesPerSample;
			if (excess == 0) {
				excess = bytesPerSample;
			}

			int32_t endPosWithinCurrentCluster = -excess;
			reassessmentLocation = regionBase + endPosWithinCurrentCluster;
		}
	}

	// Do the Cluster start location
	// Playing forwards
	if (guide->playDirection == 1) {
		int32_t firstClusterWithData = sample->getFirstClusterIndexWithAudioData();
		if (currentClusterIndex == firstClusterWithData) {
			clusterStartLocation = regionBase + (sample->audioDataStartPosBytes & (Cluster::size - 1));
		}
		else {
			clusterStartLocation = regionBase;
		}
	}

	// Playing backwards
	else {
		int32_t audioDataStopPos = sample->audioDataStartPosBytes + sample->audioDataLengthBytes;

		// There may actually be 1 less Cluster than this if the audio data ends right
		// at the Cluster end, but that won't cause problems
		int32_t highestClusterIndex = audioDataStopPos >> Cluster::size_magnitude;

		if (currentClusterIndex == highestClusterIndex) {
			clusterStartLocation = regionBase + ((audioDataStopPos - 1) & (Cluster::size - 1));
		}
		else {
			clusterStartLocation = regionBase + (Cluster::size - 1);
		}
	}

	misalignPlaybackParameters(sample);
}

// Make sure reasons are unassigned before you call this!
// Call changeClusterIfNecessary() after this if byteOvershoot isn't 0
RegionOutcome SampleLowLevelReader::setupClusersForInitialPlay(SamplePlaybackGuide* guide, Sample* sample,
                                                               int32_t byteOvershoot, bool justLooped,
                                                               int32_t priorityRating) {

	if (sample->unplayable) {
		return RegionOutcome::Unavailable; // TODO: this probably shouldn't be here
	}

	// Assign all the upcoming Clusters...
	uint32_t startPlaybackAtByte = guide->getBytePosToStartPlayback(justLooped);
	startPlaybackAtByte += byteOvershoot * guide->playDirection;

	RegionOutcome outcome = setupClustersForPlayFromByte(guide, sample, startPlaybackAtByte, priorityRating);

	if (outcome != RegionOutcome::Ready) {
		D_PRINTLN("setupClustersForInitialPlay fail");
	}

	return outcome;
}

// Make sure reasons are unassigned before you call this!
// Call changeClusterIfNecessary() after this if byteOvershoot isn't 0
RegionOutcome SampleLowLevelReader::setupClustersForPlayFromByte(SamplePlaybackGuide* guide, Sample* sample,
                                                                 int32_t startPlaybackAtByte, int32_t priorityRating) {

	// Change in Aug 2019 - we return false if stuff is out of range. Seems right? Previously we were constraining
	// ClusterIndex to the range, but not changing startPlaybackAtByte - didn't seem to make sense. Or, should it maybe
	// do the "outputting zeros" thing instead? Possibly not too important, as this only gets called for an "initial
	// play", and on time-stretch hop, which goes and will try some alternative stuff if this fails
	if (startPlaybackAtByte < sample->audioDataStartPosBytes
	    || startPlaybackAtByte >= sample->audioDataStartPosBytes + sample->audioDataLengthBytes) {
		return RegionOutcome::Unavailable; // Out of range — no amount of waiting fixes this.
	}

	int32_t clusterIndex = startPlaybackAtByte >> Cluster::size_magnitude;

	RegionOutcome outcome = assignClusters(guide, sample, clusterIndex, priorityRating);
	if (outcome != RegionOutcome::Ready) {
		D_PRINTLN("setupClustersForPlayFromByte fail");
		D_PRINTLN("byte:  %d", startPlaybackAtByte);
		return outcome;
	}

	int32_t bytePosWithinNewCluster = startPlaybackAtByte - clusterIndex * Cluster::size;

	// assignClusters() just acquired the start region through the port; source the play-pos base from
	// region_.payload_base (the resident chunk's payload().data()), matching moveOnToNextCluster's
	// region.payload_base base.
	setupForPlayPosMovedIntoNewCluster(guide, sample, reinterpret_cast<char*>(region_.payload_base),
	                                   bytePosWithinNewCluster, sample->byteDepth);

	// No check has been made that currentPlayPos is not already later than the new reassessmentLocation.
	// If caller isn't sure about this, call changeClustersIfNecessary().
	// changeClustersIfNecessary() itself calls this function when it changes current Cluster, so we absolutely couldn't
	// call it from here.
	return RegionOutcome::Ready;
}

// Unassign the old ones before you call this.
RegionOutcome SampleLowLevelReader::assignClusters(SamplePlaybackGuide* guide, Sample* sample, int32_t clusterIndex,
                                                   int32_t priorityRating) {
#if ALPHA_OR_BETA_VERSION
	// Precondition: callers unassign first, so `region_` is empty on entry. If it isn't, the retain
	// below overwrites an un-released independent lease -- a silent leak. Catch the violation in dev
	// builds instead of leaking; mirrors i019/i021/i022.
	if (hasCurrentRegion()) {
		FREEZE_WITH_ERROR("i024");
	}
#endif
	ensureSource(sample);

	// ensureSource() can leave source_ null when the source pool is exhausted: open() freezes with
	// FREEZE_WITH_ERROR("SSP1") but that is not a hard halt on hardware (OLED::freezeWithError blocks then
	// RESUMES), so it returns nullptr. Treat a null source as NotReady — drop the voice gracefully, the same
	// as an underrun — so nothing downstream dereferences a null source. (deluge_sample_region_acquire is
	// itself null-tolerant too; this is the explicit, self-documenting guard.)
	if (source_ == nullptr) {
		// Diagnostic: this and the region_acquire failure below both used to return a bare false, so a
		// caller's "setupClustersForPlayFromByte fail" could not distinguish "no source to read through"
		// from "the chunk would not come resident" — two unrelated causes with different fixes.
		D_PRINTLN("assignClusters fail: source_ null (source pool exhausted)");
		return RegionOutcome::Unavailable; // No cursor to load through — this can never become ready.
	}

	// Acquire the current region's residency through the region port. A `false` return is NotReady --
	// the port could neither find nor load the chunk (null, or present-but-not-yet-loaded) -- and leaves
	// `out` untouched.
	DelugeSampleRegion region;
	DelugeRegionState state =
	    deluge_sample_region_acquire_ex(source_, static_cast<uint32_t>(clusterIndex), guide->playDirection,
	                                    static_cast<uint32_t>(priorityRating), &region);
	RegionOutcome outcome = regionOutcomeFrom(state);
	if (outcome != RegionOutcome::Ready) {
		// The port refused, and WHICH refusal it was has opposite fixes: LOADING means the chunk was
		// reserved and enqueued but its fill had not landed yet; UNAVAILABLE means the port could not
		// reserve at all, or `clusterIndex >= num_clusters` (the geometry has no such cluster). The
		// index matters too — "cluster 0 will not come resident" and "cluster N mid-stream missed its
		// prefetch" are different problems.
		D_PRINTLN("assignClusters fail: acquire %s cluster %d dir %d",
		          state == DELUGE_REGION_LOADING ? "LOADING" : "UNAVAILABLE", clusterIndex,
		          (int32_t)guide->playDirection);
		return outcome; // Loading is NOT a failure — the caller decides whether to wait.
	}

	// Take the reader's single INDEPENDENT hard lease on the acquired chunk, tracked through
	// `region_.lease` (which IS the chunk pointer). The port cursor holds its own lease on
	// `source_->current`; this one is what pins the chunk for the reader's own residency lifetime,
	// released uniformly in unassignAllReasons. Precondition: `region_` is empty on entry (callers
	// unassign first), so this add is not overwriting an un-released lease.
	deluge_sample_region_retain(region.lease);

	// Retain the acquired region so the interpolation window's base pointer (clusterStartLocation /
	// reassessmentLocation, computed in setupReassessmentLocation) sources from region.payload_base, and
	// so `region_.lease` tracks the independent lease just taken.
	region_ = region;

	// The region port owns the look-ahead: acquire() above already prefetched the next cluster in
	// `playDirection`, and moveOnToNextCluster advances through the port, so the port holds the single
	// prefetch lease (one lease per neighbour).
	return RegionOutcome::Ready;
}

bool SampleLowLevelReader::moveOnToNextCluster(SamplePlaybackGuide* guide, Sample* sample, int32_t priorityRating) {

#if ALPHA_OR_BETA_VERSION
	if (!hasCurrentRegion()) {
		FREEZE_WITH_ERROR("i019");
	}
#endif

	// Read the exhausted cluster's index and base from the retained region BEFORE releasing its lease.
	int32_t oldClusterIndex = static_cast<int32_t>(region_.region_index);

	int32_t bytePosWithinOldCluster = currentPlayPos - reinterpret_cast<char*>(region_.payload_base);

	// Drop the exhausted current cluster's INDEPENDENT lease (tracked through region_.lease). The region
	// port holds its own lease on this same chunk; the acquire() below auto-releases the port's current as
	// it advances, so this pairs the port's fused old-current release for the reader's own lease.
	deluge_sample_region_release(region_.lease);

	// The boundary crossing advances through the region port: acquire() promotes the port's standing
	// prefetch to current and prefetches the following neighbour. A `false` return is NotReady -- the
	// next chunk is null or present-but-not-yet-loaded, the underrun drop. On the drop: no current
	// region, currentPlayPos cleared, false.
	int32_t newClusterIndex = oldClusterIndex + guide->playDirection;

	DelugeSampleRegion region;
	if (!deluge_sample_region_acquire(source_, static_cast<uint32_t>(newClusterIndex), guide->playDirection,
	                                  static_cast<uint32_t>(priorityRating), &region)) {
		D_PRINTLN("late or reached end of waveform. last Cluster was:  %d", oldClusterIndex);
		currentPlayPos = nullptr;

		// The old current's INDEPENDENT lease was already dropped above, and this failed acquire means the
		// port isn't handing us a replacement -- region_ still describes that now-released chunk. Clear it
		// so nothing downstream reads a stale payload_base before the next successful acquire.
		region_ = {};

#ifdef DELUGE_HOST
		// Streaming-underrun harness, UNASSIGN-class signal: ordinary (non-cache, non-time-stretch)
		// forward playback just crossed a Cluster boundary and the next Cluster isn't resident -- the
		// SD loader hasn't kept up with real-time consumption. This is the common-case
		// sustained-streaming underrun path, complementing `VoiceSample::stopReadingFromCache`'s
		// sibling site (which only fires for repitch-cache playback): most voices never use that
		// cache, so without this site the harness's counters stayed at zero even under genuinely
		// SD-latency-starved sustained streaming. The caller (`VoiceSample::render` ->
		// `Voice::render`) treats this `false` return as an instant voice unassign (`goto
		// instantUnassign`), the same severity as the cache-stop site, just reached from the far
		// more common code path. Fires on the port's NotReady.
		deluge::harness::noteUnderrunUnassign();
#endif
		return false;
	}

	// Pin the newly-resident cluster with the reader's own INDEPENDENT lease (mirroring the port's
	// current), tracked through region_.lease. The old current's lease was released above, so this is a
	// clean single-lease handover.
	deluge_sample_region_retain(region.lease);

	// Retain the newly-current region for setupReassessmentLocation's interpolation-window base (see
	// assignClusters), and so region_.lease tracks the lease just taken.
	region_ = region;

	// Remove the compensation we'd done on the play pos relating to the byte depth of samples
	bytePosWithinOldCluster = bytePosWithinOldCluster + 4 - sample->byteDepth;

	setupForPlayPosMovedIntoNewCluster(guide, sample, reinterpret_cast<char*>(region.payload_base),
	                                   bytePosWithinOldCluster - Cluster::size * guide->playDirection,
	                                   sample->byteDepth);

	return true;
}

// Returns false if stopping deliberately or clusters weren't loaded in time. In that case, caller may wish to output
// some zeros to work through the interpolation buffer. The current region and its lease will be unassigned / cleared
// in this case.
bool SampleLowLevelReader::changeClusterIfNecessary(SamplePlaybackGuide* guide, Sample* sample, bool loopingAtLowLevel,
                                                    int32_t priorityRating) {

#if ALPHA_OR_BETA_VERSION
	int32_t count = 0;
#endif

	while (true) {
		int32_t byteOvershoot = (currentPlayPos - reassessmentLocation) * guide->playDirection;

		if (byteOvershoot < 0) {
			break;
		}

		if (reassessmentAction == REASSESSMENT_ACTION_NEXT_CLUSTER) {
			bool success = moveOnToNextCluster(guide, sample, priorityRating);
			if (!success) {
				D_PRINTLN("next failed");
				return false;
			}
		}
		else { // LOOP_OR_STOP
			unassignAllReasons(false);
			if (loopingAtLowLevel) {
				if (setupClusersForInitialPlay(guide, sample, byteOvershoot, true, priorityRating)
				    != RegionOutcome::Ready) {
					D_PRINTLN("loop failed");
					// TODO: shouldn't we set currentPlayPos = 0 here too?
					return false;
				}
			}
			else {
				currentPlayPos = nullptr;
				return false;
			}
		}

#if ALPHA_OR_BETA_VERSION
		count++;
		if (count >= 1024) {
			// This happened one time! When stopping AudioClips from playing back, after recording and mucking around
			// with SD card reaching full
			FREEZE_WITH_ERROR("E227");
		}
#endif
	}
	return true;
}

void SampleLowLevelReader::fillInterpolationBufferRetrospectively(Sample* sample, int32_t bufferSize, int32_t startI,
                                                                  int32_t playDirection) {

	if (!hasCurrentRegion()) {
		// zero and return ASAP
		for (int32_t i = startI; i < bufferSize; i++) {
			interpolator_.buffer_l[i] = 0;
			interpolator_.buffer_r[i] = 0;
		}
		return;
	}

	// otherwise...
	// Fill up the furthest-back end of the interpolation buffer
	char* thisPlayPos = currentPlayPos;
	for (int32_t i = startI; i < bufferSize; i++) {
		// At each iteration through this loop, we need to jump one sample backwards in time.
		thisPlayPos = thisPlayPos - playDirection * sample->numChannels * sample->byteDepth;
		int32_t bytesPastClusterStart = (thisPlayPos - clusterStartLocation) * playDirection;

		// If there was valid audio data there...
		if (bytesPastClusterStart >= 0) {
			interpolator_.buffer_l[i] = *(int16_t*)(thisPlayPos + 2);

			if (sample->numChannels == 2) {
				interpolator_.buffer_r[i] = *(int16_t*)(thisPlayPos + 2 + sample->byteDepth);
			}
		}

		// Or if not, just write zeros
		else {
			interpolator_.buffer_l[i] = 0;
			interpolator_.buffer_r[i] = 0;
		}
	}
}

bool SampleLowLevelReader::fillInterpolationBufferForward(SamplePlaybackGuide* guide, Sample* sample,
                                                          int32_t interpolationBufferSize, bool loopingAtLowLevel,
                                                          int32_t numSpacesToFill, int32_t priorityRating) {

	if (!hasCurrentRegion()) {
		// zero and exit fast
		return fillInterpolationBufferWithSilence(numSpacesToFill - 1, interpolationBufferSize);
	}

	// otherwise...
	for (int32_t i = numSpacesToFill - 1; i >= 0; i--) {
		bool stillGoing = changeClusterIfNecessary(guide, sample, loopingAtLowLevel, priorityRating);
		if (!stillGoing) {
			// There is no more audio, so the rest of the buffer is silence - and we must not read
			// through currentPlayPos, which changeClusterIfNecessary() sets to null on its
			// not-looping exit. Reading it as a play position dereferenced address 3, because the
			// silence path increments it as a counter from null.
			//
			// Nor can we simply skip the read and go round again: currentPlayPos is a small count by
			// then, so changeClusterIfNecessary() would compute a hugely negative overshoot against
			// the reassessment pointer, report that we are still inside the cluster, and let the next
			// iteration do the read we just avoided.
			return fillInterpolationBufferWithSilence(i, interpolationBufferSize);
		}

		interpolator_.buffer_l[i] = *(int16_t*)(currentPlayPos + 2);
		if (sample->numChannels == 2) {
			interpolator_.buffer_r[i] = *(int16_t*)(currentPlayPos + 2 + sample->byteDepth);
		}

		// And move forward one more
		currentPlayPos += sample->numChannels * sample->byteDepth * guide->playDirection;
	}

	return true;
}

bool SampleLowLevelReader::fillInterpolationBufferWithSilence(int32_t fromIndex, int32_t interpolationBufferSize) {
	for (int32_t i = fromIndex; i >= 0; i--) {
		interpolator_.buffer_l[i] = 0;
		interpolator_.buffer_r[i] = 0;
		currentPlayPos++;
		if ((uintptr_t)currentPlayPos >= interpolationBufferSize) {
			return false;
		}
	}
	return true;
}

void SampleLowLevelReader::jumpBackSamples(Sample* sample, int32_t numToJumpBack, int32_t playDirection) {
	while (numToJumpBack--) { // Could probably be more efficient, but why bother - this whole function is rare

		// Jump back 1 sample
		char* newPlayPos = currentPlayPos - playDirection * sample->numChannels * sample->byteDepth;
		int32_t bytesPastClusterStart = (newPlayPos - clusterStartLocation) * playDirection;

		// If there was no valid audio data there...
		if (bytesPastClusterStart < 0) {
			D_PRINTLN("failed to go back!");
			break;
		}

		// Ok, cool, we can go there
		currentPlayPos = newPlayPos;
	}
}

// This sets up for the reading of some samples. If interpolating, it jumps the play-head forward one output-sample, in
// place of this happening for the first one in the fast rendering routine (it still happens there for all but the
// first). This is because the rendering of the first output-sample may require multiple source-samples to be fed into
// the interpolation buffer, and they might not all be in the same cluster - they might span across a cluster boundary.
// This function is equipped to deal with this, whereas the fast rendering routine is not. This function will be called
// again whenever any such cluster-changing situation arises.
//
// Non-interpolating playback, too, checks in this function (so at the start of the render) whether we need to change to
// the next cluster.
bool SampleLowLevelReader::considerUpcomingWindow(SamplePlaybackGuide* guide, Sample* sample, int32_t* numSamples,
                                                  int32_t phaseIncrement, bool loopingAtLowLevel,
                                                  int32_t interpolationBufferSize, bool allowEndlessSilenceAtEnd,
                                                  int32_t priorityRating) {

	if (ALPHA_OR_BETA_VERSION && phaseIncrement < 0) {
		FREEZE_WITH_ERROR("E228");
	}

	int32_t bytesPerSample = sample->numChannels * sample->byteDepth;

	// Interpolating
	if (phaseIncrement != kMaxSampleValue) {

		// But if we weren't interpolating last time...
		if (!interpolationBufferSizeLastTime) {
			interpolationBufferSizeLastTime = interpolationBufferSize;

			int32_t halfBufferSize = interpolationBufferSize >> 1;

			// Fill up the furthest-back end of the interpolation buffer
			fillInterpolationBufferRetrospectively(sample, interpolationBufferSize, halfBufferSize,
			                                       guide->playDirection);

			// And fill up to end of interpolation buffer
			bool success = fillInterpolationBufferForward(guide, sample, interpolationBufferSize, loopingAtLowLevel,
			                                              halfBufferSize, priorityRating);
			if (!success) {
				return false;
			}

			if (ALPHA_OR_BETA_VERSION && hasCurrentRegion()) {
				int32_t bytesLeftWhichMayBeRead = (reassessmentLocation - currentPlayPos) * guide->playDirection;
				if (bytesLeftWhichMayBeRead < 0) {
					FREEZE_WITH_ERROR("E222");
				}
			}
		}

		// Or, if interpolation buffer size has changed...
		else if (interpolationBufferSizeLastTime != interpolationBufferSize) {

			// Shrink buffer...
			if (interpolationBufferSize < interpolationBufferSizeLastTime) {

				if (ALPHA_OR_BETA_VERSION && hasCurrentRegion()) {
					int32_t bytesLeftWhichMayBeRead = (reassessmentLocation - currentPlayPos) * guide->playDirection;
					if (bytesLeftWhichMayBeRead < 0) {
						FREEZE_WITH_ERROR("E305");
					}
				}

				int32_t difference = interpolationBufferSizeLastTime - interpolationBufferSize;
				int32_t offset = difference >> 1;

				for (int32_t i = 0; i < interpolationBufferSize; i++) {
					interpolator_.buffer_l[i] = interpolator_.buffer_l[i + offset];
					if (sample->numChannels == 2) {
						interpolator_.buffer_r[i] = interpolator_.buffer_r[i + offset];
					}
				}

				jumpBackSamples(sample, offset, guide->playDirection);

				if (ALPHA_OR_BETA_VERSION && hasCurrentRegion()) {
					int32_t bytesLeftWhichMayBeRead = (reassessmentLocation - currentPlayPos) * guide->playDirection;
					if (bytesLeftWhichMayBeRead < 0) {
						FREEZE_WITH_ERROR("E306");
					}
				}
			}

			// Expand buffer...
			else {

				if (ALPHA_OR_BETA_VERSION && hasCurrentRegion()) {
					int32_t bytesLeftWhichMayBeRead = (reassessmentLocation - currentPlayPos) * guide->playDirection;
					if (bytesLeftWhichMayBeRead < 0) {
						FREEZE_WITH_ERROR("E308");
					}
				}

				int32_t difference = interpolationBufferSize - interpolationBufferSizeLastTime;
				int32_t offset = difference >> 1;

				for (int32_t i = 0; i < interpolationBufferSizeLastTime; i++) {
					interpolator_.buffer_l[i + offset] = interpolator_.buffer_l[i];
					if (sample->numChannels == 2) {
						interpolator_.buffer_r[i + offset] = interpolator_.buffer_r[i];
					}
				}

				// And fill up to end of interpolation buffer
				bool success = fillInterpolationBufferForward(guide, sample, interpolationBufferSize, loopingAtLowLevel,
				                                              offset, priorityRating);
				if (!success) {
					return false;
				}

				// If still here, fill far end with zeros. Not perfect, but it'll do.
				for (int32_t i = (interpolationBufferSize - offset); i < interpolationBufferSize; i++) {
					interpolator_.buffer_l[i] = 0;
					if (sample->numChannels == 2) {
						interpolator_.buffer_r[i] = 0;
					}
				}

				if (ALPHA_OR_BETA_VERSION && hasCurrentRegion()) {
					int32_t bytesLeftWhichMayBeRead = (reassessmentLocation - currentPlayPos) * guide->playDirection;
					if (bytesLeftWhichMayBeRead < 0) {
						FREEZE_WITH_ERROR("E221");
					}
				}
			}

			interpolationBufferSizeLastTime = interpolationBufferSize;
		}

		oscPos += phaseIncrement;
		int32_t numSamplesToJumpForward = oscPos >> 24;

		// If jumping forward at least 1...
		if (numSamplesToJumpForward) {
			oscPos &= 16777215;

			if (hasCurrentRegion()) {
				// If by more than INTERPOLATION_BUFFER_SIZE, we need to do a pre-jump to a buffer's-length before we're
				// jumping forward to, to fill up the buffer
				if (numSamplesToJumpForward > interpolationBufferSize) {
					currentPlayPos +=
					    (numSamplesToJumpForward - interpolationBufferSize) * bytesPerSample * guide->playDirection;
					numSamplesToJumpForward = interpolationBufferSize; // That's how much jumping is left to do
				}
			}

			while (numSamplesToJumpForward--) {

				if (!hasCurrentRegion()) {
doZeroes:
					bufferZeroForInterpolation(sample->numChannels);
					if (!allowEndlessSilenceAtEnd && (uintptr_t)currentPlayPos >= interpolationBufferSize) {
						return false;
					}
				}
				else {

					bool stillGoing = changeClusterIfNecessary(guide, sample, loopingAtLowLevel, priorityRating);
					if (!stillGoing) {
						// If we actually just reached the end, go do some zeros
						if (!hasCurrentRegion()) {
							goto doZeroes;
						}

						// Otherwise, a Cluster wasn't loaded in time. So just cut the sound
						return false;
					}

					if (ALPHA_OR_BETA_VERSION) {
						if (!hasCurrentRegion()) {
							FREEZE_WITH_ERROR("E225");
						}

						int32_t bytesLeftWhichMayBeRead =
						    (reassessmentLocation - currentPlayPos) * guide->playDirection;
						if (bytesLeftWhichMayBeRead <= 0) {
							FREEZE_WITH_ERROR("E226");
						}
					}

					// Grab the value of the sample we're now at, to use for interpolation
					bufferIndividualSampleForInterpolation(sample->numChannels, sample->byteDepth, currentPlayPos);

					// And move forward one more
					currentPlayPos += bytesPerSample * guide->playDirection;

					if (ALPHA_OR_BETA_VERSION) {
						int32_t bytesLeftWhichMayBeRead =
						    (reassessmentLocation - currentPlayPos) * guide->playDirection;
						if (bytesLeftWhichMayBeRead < 0) {
							FREEZE_WITH_ERROR("E185");
						}
					}
				}
			}
		}

		// Or if not jumping forward any samples...
		else {
			if (ALPHA_OR_BETA_VERSION && hasCurrentRegion()) {

				// That should mean we've already read this one, so we definitely shouldn't be beyond the
				// reassessmentLocation...
				int32_t bytesLeftWhichMayBeRead = (reassessmentLocation - currentPlayPos) * guide->playDirection;
				if (bytesLeftWhichMayBeRead < 0) {
					FREEZE_WITH_ERROR("E223");
				}
			}
		}

		// currentPlayPos may now actually be at or beyond the reassessmentLocation - that's ok
		// Wait, I can see how it could be at it, but surely it couldn't be beyond it anymore?

		// The rest of this window is going to require that we jump forward (*numSamples - 1) times
		if (*numSamples >= 2) {

			int32_t samplesWeWantToReadThisWindow = ((uint64_t)phaseIncrement * (*numSamples - 1) + oscPos) >> 24;

			uint32_t samplesLeftWhichMayBeRead;
			bool shouldShorten;

			// If finished waveform and just reading zeros
			if (!hasCurrentRegion()) {
				if (allowEndlessSilenceAtEnd) {
					return true;
				}
				samplesLeftWhichMayBeRead = interpolationBufferSize - (uintptr_t)currentPlayPos;
				shouldShorten = (samplesWeWantToReadThisWindow > samplesLeftWhichMayBeRead);
			}

			// Or if still going on waveform
			else {
				int32_t bytesLeftWhichMayBeRead = (reassessmentLocation - currentPlayPos) * guide->playDirection;
				// Overshoot guard: currentPlayPos can legitimately land just past reassessmentLocation
				// (negative length). Without clamping, the (uint16_t) cast below wraps it into a huge
				// value, inflating *numSamples into a window far larger than the output buffer → overrun.
				// Treat an overshoot as "no bytes left this window" so we shorten to a 1-sample window and
				// reassess on the next pass.
				if (bytesLeftWhichMayBeRead < 0) {
					bytesLeftWhichMayBeRead = 0;
				}

				int32_t bytesWeWantToRead = samplesWeWantToReadThisWindow * bytesPerSample;
				shouldShorten = (bytesWeWantToRead > bytesLeftWhichMayBeRead);
				if (shouldShorten) {
					samplesLeftWhichMayBeRead =
					    (uint16_t)bytesLeftWhichMayBeRead
					    / (uint8_t)bytesPerSample; // If we're here, we know bytesLeftWhichMayBeRead is quite small
				}
			}

			// If there aren't actually enough samples / bytes left...
			if (shouldShorten) {

				int64_t phaseIncrementingLeftWhichMayBeDone =
				    ((uint64_t)(samplesLeftWhichMayBeRead + 1) << 24) - oscPos - 1;

				// This really really should never happen.
				if (ALPHA_OR_BETA_VERSION && phaseIncrementingLeftWhichMayBeDone < 0) {
					if (!hasCurrentRegion()) {
						FREEZE_WITH_ERROR("E143");
					}
					else {
						FREEZE_WITH_ERROR("E000");
					}
				}

				uint32_t numPhaseIncrementsLeftWhichMayBeDone =
				    (uint64_t)phaseIncrementingLeftWhichMayBeDone / (uint32_t)phaseIncrement;

				// We add 1 because remember, we were just considering (numSamples - 1) the whole time - because we've
				// already done a jump-forward for the first sample to read
				*numSamples = numPhaseIncrementsLeftWhichMayBeDone + 1;
			}
		}
	}

	// No interpolating
	else {

		// But if we were interpolating last time...
		if (interpolationBufferSizeLastTime) {

			if (!hasCurrentRegion()) {
				return false;
			}

			int32_t numToJumpBack = (interpolationBufferSizeLastTime >> 1) - (oscPos >> 23);
			jumpBackSamples(sample, numToJumpBack, guide->playDirection);
			interpolationBufferSizeLastTime = 0;

			oscPos = 0;
		}

		// Check if we already ended up at the end of the Cluster after the last window
		if (!changeClusterIfNecessary(guide, sample, loopingAtLowLevel, priorityRating)) {
			return false;
		}

		// If the end is coming in this window, deal with it
		int32_t bytesLeftWhichMayBeRead = (reassessmentLocation - currentPlayPos) * guide->playDirection;

		// Overshoot guard (mirrors the interp path): clamp a negative length so the (uint32_t) cast
		// below can't wrap it into a window larger than the output buffer.
		if (bytesLeftWhichMayBeRead < 0) {
			bytesLeftWhichMayBeRead = 0;
		}

		if (ALPHA_OR_BETA_VERSION && bytesLeftWhichMayBeRead <= 0) {
			FREEZE_WITH_ERROR("E001");
		}

		// If there are actually less bytes remaining than we ideally wanted for this window...
		if (*numSamples * bytesPerSample > bytesLeftWhichMayBeRead) {
			*numSamples = (uint32_t)bytesLeftWhichMayBeRead / (uint8_t)bytesPerSample;

			if (ALPHA_OR_BETA_VERSION && *numSamples <= 0) {
				D_PRINTLN("bytesLeftWhichMayBeRead:  %d", bytesLeftWhichMayBeRead);
				FREEZE_WITH_ERROR("E147"); // Crazily, Michael B got in Nov 2022, when "closing" a recorded loop.
			}
		}
	}

	return true;
}

void SampleLowLevelReader::bufferIndividualSampleForInterpolation(int32_t numChannels, int32_t byteDepth,
                                                                  char* __restrict__ playPosNow) {
	interpolator_.pushL(*(int16_t*)(playPosNow + 2));
	if (numChannels == 2) {
		interpolator_.pushR(*(int16_t*)(playPosNow + 2 + byteDepth));
	}
}

void SampleLowLevelReader::bufferZeroForInterpolation(int32_t numChannels) {
	interpolator_.pushL(0);
	if (numChannels == 2) {
		interpolator_.pushR(0);
	}
	currentPlayPos++;
}

// This could be optimized, but why bother, it doesn't get called much
void SampleLowLevelReader::jumpForwardZeroes(int32_t bufferSize, int32_t numChannels, int32_t phaseIncrement) {

	oscPos += phaseIncrement;
	int32_t numSamplesToJumpForward = oscPos >> 24;
	if (numSamplesToJumpForward) {
		oscPos &= 16777215;

		while (numSamplesToJumpForward--) {
			// Grab the value of the sample we're now at, to use for interpolation
			bufferZeroForInterpolation(numChannels);
		}
	}
}

void SampleLowLevelReader::jumpForwardLinear(int32_t numChannels, int32_t byteDepth, uint32_t bitMask,
                                             int32_t jumpAmount, int32_t phaseIncrement) {

	oscPos += phaseIncrement;
	int32_t numSamplesToJumpForward = oscPos >> 24;
	if (numSamplesToJumpForward != 0) {
		oscPos &= 16777215;

		// If jumping forward by more than INTERPOLATION_BUFFER_SIZE, we first need to jump to the one before we're
		// jumping forward to, to grab its value
		if (numSamplesToJumpForward > 2) {
			currentPlayPos += (numSamplesToJumpForward - 2) * jumpAmount;
			// numSamplesToJumpForward = 2; // Not necessasry
		}

		if (numChannels == 2) {
			if (numSamplesToJumpForward >= 2) {
				interpolator_.buffer_l[1] = *(int16_t*)(currentPlayPos + 2);
				interpolator_.buffer_r[1] = *(int16_t*)(currentPlayPos + 2 + byteDepth);
				currentPlayPos += jumpAmount;
			}
			else {
				interpolator_.buffer_l[1] = interpolator_.buffer_l[0];
				interpolator_.buffer_r[1] = interpolator_.buffer_r[0];
			}
			interpolator_.buffer_r[0] = *(int16_t*)(currentPlayPos + 2 + byteDepth);
		}

		else {
			if (numSamplesToJumpForward >= 2) {
				interpolator_.buffer_l[1] = *(int16_t*)(currentPlayPos + 2);
				currentPlayPos += jumpAmount;
			}
			else {
				interpolator_.buffer_l[1] = interpolator_.buffer_l[0];
			}
		}

		// Putting these down here did speed things up!
		interpolator_.buffer_l[0] = *(int16_t*)(currentPlayPos + 2);
		currentPlayPos += jumpAmount;
	}
}

constexpr static size_t numBitsInTableSize = 8;
constexpr static size_t rshiftAmount = ((24 + kInterpolationMaxNumSamplesMagnitude) - 16 - numBitsInTableSize + 1);

// This stuff is in its own function here rather than in Voice because for some reason it's faster
void SampleLowLevelReader::readSamplesResampled(int32_t** __restrict__ oscBufferPos, int32_t numSamplesTotal,
                                                Sample* sample, int32_t jumpAmount, int32_t numChannels,
                                                int32_t numChannelsAfterCondensing, int32_t phaseIncrement,
                                                int32_t* amplitude, int32_t amplitudeIncrement,
                                                int32_t interpolationBufferSize, bool writingCache,
                                                char** __restrict__ cacheWritePos, bool* __restrict__ doneAnySamplesYet,
                                                TimeStretcher* timeStretcher, bool bufferingToTimeStretcher,
                                                int32_t whichKernel) {

	uint32_t const bitMask = sample->bitMask;
	int32_t const byteDepth = sample->byteDepth;

	int32_t* __restrict__ oscBufferPosNow = *oscBufferPos;

	// cacheWritePos is null unless writingCache; the value is only consumed under writingCache
	// below (and written back under the same `if (cacheWritePos)` guard at the end). Dereferencing
	// it unconditionally read mapped-but-unused garbage on the MCU but faults on the host.
	char* __restrict__ cacheWritePosNow = cacheWritePos ? (char*)*cacheWritePos : nullptr;

	int32_t const* const oscBufferEnd = oscBufferPosNow + numSamplesTotal * numChannelsAfterCondensing;

	// Windowed sinc interpolation
	if (interpolationBufferSize > 2) {

		char* __restrict__ currentPlayPosNow = currentPlayPos + 2;

		if (!*doneAnySamplesYet) {
			*doneAnySamplesYet = true;
			goto skipFirstSmooth;
		}

		do {

			if (hasCurrentRegion()) [[likely]] {

				oscPos += phaseIncrement;
				int32_t numSamplesToJumpForward = oscPos >> 24;
				if (numSamplesToJumpForward) {
					oscPos &= 16777215;

					// If jumping forward by more than kInterpolationMaxNumSamples, we first need to jump to the one
					// before we're jumping forward to, to grab its value
					if (numSamplesToJumpForward > kInterpolationMaxNumSamples) {
						currentPlayPosNow += (numSamplesToJumpForward - kInterpolationMaxNumSamples) * jumpAmount;
						numSamplesToJumpForward = kInterpolationMaxNumSamples;
					}

					int16_t sourceL = *(int16_t*)currentPlayPosNow;

					interpolator_.jumpForward(numSamplesToJumpForward);
					numSamplesToJumpForward--;

					if (numChannels == 2) {
						while (true) {
							interpolator_.buffer_l[numSamplesToJumpForward] = sourceL;
							interpolator_.buffer_r[numSamplesToJumpForward] =
							    *(int16_t*)(currentPlayPosNow + byteDepth);
							currentPlayPosNow += jumpAmount;
							if (!numSamplesToJumpForward) {
								goto skipFirstSmooth;
							}
							numSamplesToJumpForward--;
							sourceL = *(int16_t*)currentPlayPosNow;
						}
					}

					else {
						while (true) {
							currentPlayPosNow += jumpAmount;
							interpolator_.buffer_l[numSamplesToJumpForward] = sourceL;
							if (!numSamplesToJumpForward) {
								goto skipFirstSmooth;
							}
							sourceL = *(int16_t*)currentPlayPosNow;
							numSamplesToJumpForward--;
						}
					}
				}
			}
			else {
				jumpForwardZeroes(interpolationBufferSize, numChannels, phaseIncrement);
			}

skipFirstSmooth:
			auto sampleRead = interpolator_.interpolate(numChannels, whichKernel, oscPos);

			int32_t existingValueL = *oscBufferPosNow;

			// If caching, do that now
			if (writingCache) {
				for (int32_t i = 4 - kCacheByteDepth; i < 4; i++) {
					*cacheWritePosNow = ((char*)&sampleRead.l)[i];
					cacheWritePosNow++;
				}

				if (numChannels == 2) {
					for (int32_t i = 4 - kCacheByteDepth; i < 4; i++) {
						*cacheWritePosNow = ((char*)&sampleRead.r)[i];
						cacheWritePosNow++;
					}
				}
			}

			// If condensing to mono, do that now
			if (numChannels == 2 && numChannelsAfterCondensing == 1) {
				sampleRead.l = ((sampleRead.l / 2) + (sampleRead.r / 2));
			}

			*amplitude += amplitudeIncrement;

			// Mono / left channel (or stereo condensed to mono)
			*oscBufferPosNow = multiply_accumulate_32x32_rshift32_rounded(existingValueL, sampleRead.l, *amplitude);
			oscBufferPosNow++;

			// Right channel
			if (numChannelsAfterCondensing == 2) {
				int32_t existingValueR = *oscBufferPosNow;
				*oscBufferPosNow = multiply_accumulate_32x32_rshift32_rounded(existingValueR, sampleRead.r, *amplitude);
				oscBufferPosNow++;
			}
		} while (oscBufferPosNow != oscBufferEnd);

		currentPlayPos = currentPlayPosNow - 2;
	}

	// Linear interpolation
	else {
		if (!*doneAnySamplesYet) {
			*doneAnySamplesYet = true;
			goto skipFirstLinear;
		}

		do {
			if (hasCurrentRegion()) {
				jumpForwardLinear(numChannels, byteDepth, bitMask, jumpAmount, phaseIncrement);
			}
			else {
				jumpForwardZeroes(interpolationBufferSize, numChannels, phaseIncrement);
			}

skipFirstLinear:
			auto sampleRead = interpolator_.interpolateLinear(numChannels, oscPos);

			int32_t existingValueL = *oscBufferPosNow;

			// If condensing to mono, do that now
			if (numChannels == 2 && numChannelsAfterCondensing == 1) {
				sampleRead.l = ((sampleRead.l >> 1) + (sampleRead.r >> 1));
			}

			*amplitude += amplitudeIncrement;

			// Mono / left channel (or stereo condensed to mono)
			*oscBufferPosNow = multiply_accumulate_32x32_rshift32_rounded(existingValueL, sampleRead.l, *amplitude);
			oscBufferPosNow++;

			// Right channel
			if (numChannelsAfterCondensing == 2) {
				int32_t existingValueR = *oscBufferPosNow;
				*oscBufferPosNow = multiply_accumulate_32x32_rshift32_rounded(existingValueR, sampleRead.r, *amplitude);
				oscBufferPosNow++;
			}
		} while (oscBufferPosNow != oscBufferEnd);
	}

	*oscBufferPos = oscBufferPosNow;
	if (cacheWritePos) {
		*cacheWritePos = cacheWritePosNow;
	}
}

void SampleLowLevelReader::readSamplesNative(int32_t** __restrict__ bufferPos, int32_t numSamplesTotal, Sample* sample,
                                             int32_t jumpAmount, int32_t numChannels,
                                             int32_t numChannelsAfterCondensing, int32_t* __restrict__ amplitude,
                                             int32_t amplitudeIncrement, TimeStretcher* timeStretcher,
                                             bool bufferingToTimeStretcher) {

	char* __restrict__ currentPlayPosNow = currentPlayPos;
	int32_t* __restrict__ bufferPosNow = *bufferPos;
	int32_t const* const bufferEndNow = bufferPosNow + numSamplesTotal * numChannelsAfterCondensing;

	int32_t const byteDepth = sample->byteDepth;
	uint32_t const bitMask = sample->bitMask;

	do {
		int32_t sampleReadL = *(int32_t*)currentPlayPosNow;

		int32_t existingValueL = *bufferPosNow;
		*amplitude += amplitudeIncrement;
		sampleReadL &= bitMask;

		int32_t sampleReadR;
		if (numChannels == 2) {
			sampleReadR = *(int32_t*)(currentPlayPosNow + byteDepth) & bitMask;

			// If condensing to mono, do that now
			if (numChannelsAfterCondensing == 1) {
				sampleReadL = ((sampleReadL >> 1) + (sampleReadR >> 1));
			}
		}

		currentPlayPosNow += jumpAmount;

		// Mono / left channel (or stereo condensed to mono)
		*bufferPosNow = multiply_accumulate_32x32_rshift32_rounded(
		    existingValueL, sampleReadL,
		    *amplitude); // *amplitude is modified above; using accumulate made no difference
		bufferPosNow++;

		// Right channel
		if (numChannelsAfterCondensing == 2) {
			int32_t existingValueR = *bufferPosNow;
			*bufferPosNow = multiply_accumulate_32x32_rshift32_rounded(existingValueR, sampleReadR, *amplitude);
			bufferPosNow++;
		}
	} while (bufferPosNow != bufferEndNow);

	*bufferPos = bufferPosNow;
	currentPlayPos = currentPlayPosNow;
}

// Returns false if actual error. Not if it just reached the end. In that case it just sets
// timeStretcher->playHeadStillActive[whichPlayHead] to false
bool SampleLowLevelReader::readSamplesForTimeStretching(
    int32_t* outputBuffer, SamplePlaybackGuide* guide, Sample* sample, int32_t numSamples, int32_t numChannels,
    int32_t numChannelsAfterCondensing, int32_t phaseIncrement, int32_t amplitude, int32_t amplitudeIncrement,
    bool loopingAtLowLevel, int32_t jumpAmount, int32_t bufferSize, TimeStretcher* timeStretcher,
    bool bufferingToTimeStretcher, int32_t whichPlayHead, int32_t whichKernel, int32_t priorityRating) {

	do {
		int32_t samplesNow = numSamples;

		timeStretcher->playHeadStillActive[whichPlayHead] = considerUpcomingWindow(
		    guide, sample, &samplesNow, phaseIncrement, loopingAtLowLevel, bufferSize, 0, priorityRating);
		if (!timeStretcher->playHeadStillActive[whichPlayHead]) {

			// If we got false, that can just mean end of waveform. But if the current region is still held,
			// that means (SD card) error
			if (hasCurrentRegion()) {
				return false;
			}

			// D_PRINTLN("one head no longer active for timeStretcher");
			break;
		}

		// No resampling
		if (phaseIncrement == kMaxSampleValue) {
			readSamplesNative(&outputBuffer, samplesNow, sample, jumpAmount, numChannels, numChannelsAfterCondensing,
			                  &amplitude, amplitudeIncrement, timeStretcher, bufferingToTimeStretcher);
		}

		// Resampling
		else {
			bool doneAnySamplesYet = false;
			readSamplesResampled(&outputBuffer, samplesNow, sample, jumpAmount, numChannels, numChannelsAfterCondensing,
			                     phaseIncrement, &amplitude, amplitudeIncrement, bufferSize, false, nullptr,
			                     &doneAnySamplesYet, timeStretcher, bufferingToTimeStretcher, whichKernel);
		}

		numSamples -= samplesNow;
	} while (numSamples);

	return true;
}

void SampleLowLevelReader::adoptResidencyFrom(SampleLowLevelReader& other, bool stealReasons) {
	// `region_` (this reader's residency, including its independent lease via `region_.lease`) has already
	// been set from `other` by the caller: the copy/move ctors copy it in their initializer list, and the
	// move-assignment operator assigns it just before calling here. This function only reconciles the
	// LEASE OWNERSHIP that copy implies.
	if (stealReasons) {
		// Stealing move: `this` takes over `other`'s residency wholesale. The region-port cursor -- and
		// with it the leases the port holds on the current/prefetch chunks -- transfers from `other` to
		// `this`. The reader's own INDEPENDENT region lease transfers too: `region_.lease` was copied from
		// `other`, so `this` now owns that lease; clear `other.region_` so `other` (its destructor / next
		// unassign) won't release the lease `this` now owns. Exactly one of {this, other} owns it after:
		// `this`.
		if (source_ != nullptr) {
			deluge_sample_source_close(source_);
		}
		source_ = other.source_;
		source_backing_ = other.source_backing_;
		other.source_ = nullptr;
		other.source_backing_ = nullptr;
		other.region_ = {};
	}
	else {
		// Non-stealing copy (TimeStretcher::olderPartReader): `this` gets its OWN independent hard lease on
		// the chunk `region_` describes (copied from `other`). `other` keeps its own lease, so both readers
		// now pin the shared chunk -- the refcount rises by one. This reader's `source_` is left null; it
		// re-opens its own cursor lazily on the next assignClusters().
		// This independent lease is load-bearing: it is the older head's ONLY pin on the shared chunk (it
		// has no `source_`), so without it the chunk could be reclaimed under the older head while the newer
		// head advances -> use-after-free.
		if (region_.lease != 0) {
			deluge_sample_region_retain(region_.lease);
		}
	}
}
SampleLowLevelReader::SampleLowLevelReader(SampleLowLevelReader& other, bool stealReasons)
    : oscPos{other.oscPos}, currentPlayPos{other.currentPlayPos}, reassessmentLocation{other.reassessmentLocation},
      clusterStartLocation{other.clusterStartLocation}, reassessmentAction{other.reassessmentAction},
      interpolationBufferSizeLastTime{other.interpolationBufferSizeLastTime}, interpolator_{other.interpolator_},
      region_{other.region_} {

	adoptResidencyFrom(other, stealReasons);
}
SampleLowLevelReader::SampleLowLevelReader(SampleLowLevelReader&& other) noexcept
    : oscPos{other.oscPos}, currentPlayPos{other.currentPlayPos}, reassessmentLocation{other.reassessmentLocation},
      clusterStartLocation{other.clusterStartLocation}, reassessmentAction{other.reassessmentAction},
      interpolationBufferSizeLastTime{other.interpolationBufferSizeLastTime}, interpolator_{other.interpolator_},
      region_{other.region_} {
	adoptResidencyFrom(other, true);
}
SampleLowLevelReader& SampleLowLevelReader::operator=(SampleLowLevelReader&& other) noexcept {
	if (this == &other) {
		return *this;
	}
	oscPos = other.oscPos;
	currentPlayPos = other.currentPlayPos;
	reassessmentLocation = other.reassessmentLocation;
	clusterStartLocation = other.clusterStartLocation;
	reassessmentAction = other.reassessmentAction;
	interpolationBufferSizeLastTime = other.interpolationBufferSizeLastTime;
	interpolator_ = other.interpolator_;
	// Release this reader's OWN current region + its independent lease first (unassignAllReasons clears
	// region_). Then move `other`'s region in BEFORE adoptResidencyFrom, because the stealing
	// adoptResidencyFrom now clears `other.region_` (so `other` won't release the lease `this` takes
	// over) -- doing the
	// assignment after it would copy an already-emptied region. After this, `this` owns the region + its
	// single lease and `other` is emptied: exactly one owner, no leak, no double-free.
	unassignAllReasons(false);
	region_ = other.region_;
	adoptResidencyFrom(other, true);
	return *this;
}
