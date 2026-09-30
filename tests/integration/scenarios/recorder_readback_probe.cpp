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

#include "scenarios/recorder_readback_probe.h"

#include "definitions_cxx.hpp"
#include "dsp/stereo_sample.h"
#include "libdeluge/sample_source.h"
#include "libdeluge/streaming_fill.h" // deluge_streaming_request_drain
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "storage/cluster/cluster.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
/// Tick budget for the finalized probe's pump, which the caller drives.
int32_t g_finalizedPumpTicks = 0;

SampleRecorder* g_recorder = nullptr;
DelugeSampleSource* g_source = nullptr;

int32_t expected24(uint32_t frameIndex, uint32_t channel) {
	uint32_t v = (frameIndex * 97u + channel * 131u + 17u) % (1u << 24);
	return static_cast<int32_t>(v) - (1 << 23);
}

// Ask the async fill task for a drain, for the _poll() entry points below. Their callers yield between calls, and
// that yield is what lets the fill task run -- asking is enough.
void requestFillDrain() {
	deluge_streaming_request_drain();
}

int32_t rampSample(uint32_t frameIndex, uint32_t channel) {
	return expected24(frameIndex, channel) << 8;
}

void teardown() {
	if (g_source != nullptr) {
		deluge_sample_source_close(g_source);
		g_source = nullptr;
	}
	if (g_recorder != nullptr) {
		// Callers abort and tick _settle_tick() until settled first: after boot a stream-channel request can still be
		// in flight, and deleting the recorder under it would leave the channel writing into freed memory. abort()
		// again is harmless.
		g_recorder->abort();
		delete g_recorder;
		g_recorder = nullptr;
	}
}

// --- Finalized multi-cluster overview-cache sizing regression probe ----------------------------

SampleRecorder* g_finalizedRecorder = nullptr;
DelugeSampleSource* g_finalizedSource = nullptr;
uint32_t g_finalizedRegionIndex = 0;
uint32_t g_finalizedAudioDataStartPosBytes = 0;
uint32_t g_finalizedTableClusters = 0;
uint32_t g_finalizedExpectedClusters = 0;
std::vector<std::byte> g_finalizedExpectedAudioBytes;
DelugeSampleRegion g_finalizedLastRegion{};
bool g_finalizedLastReady = false;

void finalizedTeardown() {
	if (g_finalizedSource != nullptr) {
		deluge_sample_source_close(g_finalizedSource);
		g_finalizedSource = nullptr;
	}
	if (g_finalizedRecorder != nullptr) {
		// Unlike teardown() above, this recorder has already reached RecorderStatus::COMPLETE (its
		// file finalized and closed) by the time this normally runs -- no card-routine work is left
		// to drive, so a direct delete (which still runs ~SampleRecorder()'s unconditional
		// detachSample()) is sufficient, same as host_recorder_roundtrip_main.cpp's stack-allocated
		// SampleRecorder going out of scope.
		delete g_finalizedRecorder;
		g_finalizedRecorder = nullptr;
	}
	g_finalizedRegionIndex = 0;
	g_finalizedAudioDataStartPosBytes = 0;
	g_finalizedTableClusters = 0;
	g_finalizedExpectedClusters = 0;
	g_finalizedExpectedAudioBytes.clear();
	g_finalizedLastRegion = DelugeSampleRegion{};
	g_finalizedLastReady = false;
}

} // namespace

extern "C" {

uint8_t deluge_harness_recorder_probe(uint8_t numChannels, uint32_t numFrames) {
	teardown(); // idempotent; drop any previous probe first

	g_recorder = new SampleRecorder();
	Error err = g_recorder->setup(numChannels, AudioInputChannel::MIX, /*newKeepingReasons=*/false,
	                              /*shouldRecordExtraMargins=*/false, AudioRecordingFolder::RESAMPLE,
	                              /*buttonPressLatency=*/0, /*outputRecordingFrom=*/nullptr);
	if (err != Error::NONE) {
		teardown();
		return 0;
	}

	std::vector<StereoSample> input(numFrames);
	for (uint32_t i = 0; i < numFrames; i++) {
		input[i].l = rampSample(i, 0);
		input[i].r = (numChannels == 2) ? rampSample(i, 1) : 0;
	}
	g_recorder->feedAudio(std::span<StereoSample>(input));

	// Set up: the caller drains (..._drain_tick) and acquires (..._acquire), one step per yield. Deliberately no
	// endSyncedRecording()/finalize: status stays CAPTURING_DATA, so the Sample genuinely has no efatfs read handle
	// for the whole probe.
	return 1;
}

/// One tick of the drain. Returns nothing: what it flushes is observed by `_acquire()` below.
void deluge_harness_recorder_probe_drain_tick() {
	if (g_recorder != nullptr) {
		(void)g_recorder->cardRoutine();
	}
}

/// Open the region source over the still-recording Sample and take the first acquire.
uint8_t deluge_harness_recorder_probe_acquire() {
	if (g_recorder == nullptr) {
		return 0;
	}
	// Idempotent: the drain-then-acquire caller reaches this twice (once through the inline entry with
	// zero ticks, once after its own drain), and re-opening would leak the first source.
	if (g_source != nullptr) {
		DelugeSampleRegion again{};
		return static_cast<uint8_t>(
		    deluge_sample_region_acquire_ex(g_source, /*index=*/0, /*direction=*/+1, /*priority=*/0, &again));
	}
	Sample* sample = g_recorder->sample;
	DelugeSampleGeometry geometry{};
	geometry.audio_data_start_bytes = sample->audioDataStartPosBytes;
	geometry.audio_data_length_bytes = sample->audioDataLengthBytes; // the still-recording sentinel
	geometry.cluster_size_bytes = static_cast<uint32_t>(Cluster::size);
	geometry.byte_depth = sample->byteDepth;
	geometry.num_channels = sample->numChannels;
	geometry.raw_data_format = static_cast<uint8_t>(sample->rawDataFormat);

	g_source = deluge_sample_source_open(&sample->stream(), geometry);
	if (g_source == nullptr) {
		return 0;
	}

	DelugeSampleRegion out{};
	DelugeRegionState state = deluge_sample_region_acquire_ex(g_source, /*index=*/0, /*direction=*/+1,
	                                                          /*priority=*/0, &out);
	// A first CLUSTER_ENQUEUE-style acquire comes back LOADING (the fill is scheduled, not synchronous); the caller
	// polls with _poll(). A still-recording Sample has no efatfs read handle, so the read fails and the acquire stays
	// LOADING however often it is drained -- that non-resolution is the point being probed, not a harness bug.

	return static_cast<uint8_t>(state);
}

uint8_t deluge_harness_recorder_probe_poll() {
	if (g_source == nullptr) {
		return static_cast<uint8_t>(DELUGE_REGION_UNAVAILABLE);
	}
	// Ask for a drain and re-acquire (not just deluge_sample_region_state(), which never itself
	// progresses a fill) so a caller polling this in a loop gives EITHER drain mechanism -- the C++ pump
	// on sim, or the Rust async fill task on host_app -- a real chance to resolve the region.
	requestFillDrain();
	DelugeSampleRegion out{};
	DelugeRegionState state =
	    deluge_sample_region_acquire_ex(g_source, /*index=*/0, /*direction=*/+1, /*priority=*/0, &out);
	return static_cast<uint8_t>(state);
}

void deluge_harness_recorder_probe_abort() {
	if (g_source != nullptr) {
		deluge_sample_source_close(g_source);
		g_source = nullptr;
	}
	if (g_recorder != nullptr) {
		g_recorder->abort();
	}
}

uint8_t deluge_harness_recorder_probe_settle_tick() {
	if (g_recorder == nullptr || g_recorder->settled()) {
		return 1;
	}
	(void)g_recorder->cardRoutine();
	return 0;
}

void deluge_harness_recorder_probe_end() {
	teardown();
}

uint8_t deluge_harness_recorder_finalized_multicluster_probe(uint8_t numChannels, uint32_t numFrames,
                                                             uint32_t regionIndex) {
	finalizedTeardown(); // idempotent; drop any previous probe first

	g_finalizedRecorder = new SampleRecorder();
	// allowFileAlterationAfter defaults false and is never set here -- finalizeRecordedFile() is
	// therefore GUARANTEED to take its no-alteration else-branch (the only branch AudioClip
	// recording itself ever takes), independent of numChannels.
	Error err = g_finalizedRecorder->setup(numChannels, AudioInputChannel::MIX, /*newKeepingReasons=*/false,
	                                       /*shouldRecordExtraMargins=*/false, AudioRecordingFolder::RESAMPLE,
	                                       /*buttonPressLatency=*/0, /*outputRecordingFrom=*/nullptr);
	if (err != Error::NONE) {
		finalizedTeardown();
		return 0;
	}

	const uint32_t frameBytes = static_cast<uint32_t>(numChannels) * 3u;
	g_finalizedExpectedAudioBytes.assign(static_cast<size_t>(numFrames) * frameBytes, std::byte{0});
	std::vector<StereoSample> input(numFrames);
	for (uint32_t i = 0; i < numFrames; i++) {
		input[i].l = rampSample(i, 0);
		input[i].r = (numChannels == 2) ? rampSample(i, 1) : 0;
		for (uint32_t ch = 0; ch < numChannels; ch++) {
			uint32_t v24u = static_cast<uint32_t>(expected24(i, ch)) & 0xFFFFFFu;
			size_t off = static_cast<size_t>(i) * frameBytes + static_cast<size_t>(ch) * 3u;
			g_finalizedExpectedAudioBytes[off + 0] = static_cast<std::byte>(v24u & 0xFFu);
			g_finalizedExpectedAudioBytes[off + 1] = static_cast<std::byte>((v24u >> 8) & 0xFFu);
			g_finalizedExpectedAudioBytes[off + 2] = static_cast<std::byte>((v24u >> 16) & 0xFFu);
		}
	}
	g_finalizedRecorder->feedAudio(std::span<StereoSample>(input));

	// endSyncedRecording(0): MIX-mode with no extra margins and no button-latency compensation means
	// numMoreSamplesToCapture is 0, so finishCapturing() runs synchronously inside this call (same
	// pattern host_recorder_roundtrip_main.cpp's runCase() uses).
	g_finalizedRecorder->endSyncedRecording(0);

	// The pump is the CALLER's -- `..._probe_tick()` below, one cardRoutine() per call. A tight loop that
	// never yields only works while `cardRoutine()` blocks inline on the synchronous arm; on the channel
	// arm nothing answers a caller that never returns.
	g_finalizedPumpTicks = 0;
	return 1; // set up; caller ticks, then calls _finish()
}

/// One tick of the finalized probe's pump. 0 = still going, 1 = COMPLETE, 2 = failed/bounded out.
uint8_t deluge_harness_recorder_finalized_multicluster_probe_tick() {
	if (g_finalizedRecorder == nullptr) {
		return 2;
	}
	if (g_finalizedRecorder->status.load(std::memory_order_acquire) == RecorderStatus::COMPLETE) {
		return 1;
	}
	if (++g_finalizedPumpTicks > 200000) {
		return 2;
	}
	Error e = g_finalizedRecorder->cardRoutine();
	if (e != Error::NONE && e != Error::MAX_FILE_SIZE_REACHED) {
		return 2;
	}
	return 0;
}

/// Acquire and verify, once the pump has reported COMPLETE.
uint8_t deluge_harness_recorder_finalized_multicluster_probe_finish(uint32_t regionIndex) {
	if (g_finalizedRecorder == nullptr) {
		return 0;
	}

	Sample* sample = g_finalizedRecorder->sample;
	if (sample == nullptr) {
		finalizedTeardown();
		return 0;
	}

	// The exact formula finalizeRecordedFile()'s hoisted resize uses -- the PHYSICAL waveform overview
	// cache (overviewCacheSize(), captured right below) MUST be >= this for the grow to have fired.
	// overviewCache_ is the structure finalizeRecordedFile() grows to the final cluster count via a
	// grow-only guard. We deliberately read overviewCacheSize() and NOT num_clusters() here: num_clusters()
	// is derived from this same geometric formula, so reading it would make this probe tautological
	// and blind to a skipped grow (the exact regression this case exists to catch).
	uint32_t idealFileSizeAfterAction =
	    sample->audioDataStartPosBytes + static_cast<uint32_t>(sample->audioDataLengthBytes);
	g_finalizedExpectedClusters = ((idealFileSizeAfterAction - 1) >> Cluster::size_magnitude) + 1;
	g_finalizedTableClusters = static_cast<uint32_t>(sample->overviewCacheSize());

	// Open a FRESH read handle on the finalized file -- the recorder's own write context is already
	// closed by now (finalizeRecordedFile() closed it), and this Sample never went through
	// AudioFileManager::buildAudioFileFromCard, so efatfs_handle_ is still 0. This mirrors how a
	// real reload/playback of a just-recorded sample reaches it.
	if (sample->stream().open_read_stream(sample->filePath) != Error::NONE) {
		finalizedTeardown();
		return 0;
	}

	DelugeSampleGeometry geometry{};
	geometry.audio_data_start_bytes = sample->audioDataStartPosBytes;
	geometry.audio_data_length_bytes = sample->audioDataLengthBytes; // the real, finalized length
	geometry.cluster_size_bytes = static_cast<uint32_t>(Cluster::size);
	geometry.byte_depth = sample->byteDepth;
	geometry.num_channels = sample->numChannels;
	geometry.raw_data_format = static_cast<uint8_t>(sample->rawDataFormat);

	g_finalizedSource = deluge_sample_source_open(&sample->stream(), geometry);
	if (g_finalizedSource == nullptr) {
		finalizedTeardown();
		return 0;
	}

	g_finalizedRegionIndex = regionIndex;
	g_finalizedAudioDataStartPosBytes = sample->audioDataStartPosBytes;

	DelugeSampleRegion out{};
	DelugeRegionState state =
	    deluge_sample_region_acquire_ex(g_finalizedSource, regionIndex, /*direction=*/+1, /*priority=*/0, &out);
	// The caller polls a LOADING result with deluge_harness_recorder_finalized_multicluster_probe_poll().
	g_finalizedLastReady = (state == DELUGE_REGION_READY);
	if (g_finalizedLastReady) {
		g_finalizedLastRegion = out;
	}
	return static_cast<uint8_t>(state);
}

uint8_t deluge_harness_recorder_finalized_multicluster_probe_poll() {
	if (g_finalizedSource == nullptr) {
		return static_cast<uint8_t>(DELUGE_REGION_UNAVAILABLE);
	}
	requestFillDrain();
	DelugeSampleRegion out{};
	DelugeRegionState state = deluge_sample_region_acquire_ex(g_finalizedSource, g_finalizedRegionIndex,
	                                                          /*direction=*/+1, /*priority=*/0, &out);
	g_finalizedLastReady = (state == DELUGE_REGION_READY);
	if (g_finalizedLastReady) {
		g_finalizedLastRegion = out;
	}
	return static_cast<uint8_t>(state);
}

uint32_t deluge_harness_recorder_finalized_multicluster_probe_table_clusters() {
	return g_finalizedTableClusters;
}

uint32_t deluge_harness_recorder_finalized_multicluster_probe_expected_clusters() {
	return g_finalizedExpectedClusters;
}

uint8_t deluge_harness_recorder_finalized_multicluster_probe_bytes_ok() {
	if (!g_finalizedLastReady || g_finalizedLastRegion.payload_base == nullptr) {
		return 0;
	}
	uint64_t clusterByteOffset = static_cast<uint64_t>(g_finalizedRegionIndex) * Cluster::size;
	if (clusterByteOffset < g_finalizedAudioDataStartPosBytes) {
		return 0; // this region overlaps the header; not what this probe verifies
	}
	uint64_t dataByteOffset = clusterByteOffset - g_finalizedAudioDataStartPosBytes;
	if (dataByteOffset + g_finalizedLastRegion.resident_bytes > g_finalizedExpectedAudioBytes.size()) {
		return 0;
	}
	const auto* got = reinterpret_cast<const std::byte*>(g_finalizedLastRegion.payload_base);
	bool match =
	    std::memcmp(got, g_finalizedExpectedAudioBytes.data() + dataByteOffset, g_finalizedLastRegion.resident_bytes)
	    == 0;
	return match ? 1 : 0;
}

void deluge_harness_recorder_finalized_multicluster_probe_end() {
	finalizedTeardown();
}

} // extern "C"
