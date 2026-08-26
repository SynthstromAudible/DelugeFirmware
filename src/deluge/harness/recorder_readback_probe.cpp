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

#include "harness/recorder_readback_probe.h"

#ifdef DELUGE_HOST

#include "definitions_cxx.hpp"
#include "dsp/stereo_sample.h"
#include "io/file.hpp"
#include "libdeluge/sample_source.h"
#include "libdeluge/streaming_fill.h" // deluge_streaming_drain_queue_blocking
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "storage/cluster/cluster.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

SampleRecorder* g_recorder = nullptr;
DelugeSampleSource* g_source = nullptr;

int32_t expected24(uint32_t frameIndex, uint32_t channel) {
	uint32_t v = (frameIndex * 97u + channel * 131u + 17u) % (1u << 24);
	return static_cast<int32_t>(v) - (1 << 23);
}

// Drive the streaming fill so a pending region-acquire can progress LOADING->READY: yield the worker
// fiber and drain the whole loader queue via the async fill task. Valid because the probes run ON the
// storage-owner worker fiber (dispatched by the recorder-roundtrip scenario on the Embassy renderer),
// and because the bounded retry loops that call it have no other yield point - the fiber suspending
// inside the drain is the only thing that lets the fill task run at all.
void driveFillDrain() {
	deluge_streaming_drain_queue_blocking();
}

// The same demand, for the _poll() entry points below. Those are called repeatedly by the Rust harness,
// which awaits a timer between calls, so its loop is what lets the fill task run - asking is enough,
// and blocking here would suspend the fiber for a drain the caller is already driving.
void requestFillDrain() {
	deluge_streaming_request_drain();
}

int32_t rampSample(uint32_t frameIndex, uint32_t channel) {
	return expected24(frameIndex, channel) << 8;
}

// Host-sim-only wrinkle: some DELUGE_HOST binaries (e.g. deluge_recorder_roundtrip) back the
// STREAMING read path (open_read_stream()/EfatfsReadSource, via host_efatfs_passthrough.cpp) with
// a plain POSIX directory tree named by DELUGE_SD_ROOT -- separate from the mounted FAT image
// (DELUGE_SD_IMAGE) a SampleRecorder actually writes into via deluge::io::Stream/File. Mirror the
// just-finalized file's raw bytes into that directory (creating parent dirs as needed) so a probe
// on such a target can still open a genuine streaming-read cursor on it. A no-op wherever
// DELUGE_SD_ROOT isn't set: host_app / real hardware read the SAME storage the recorder wrote to,
// so no mirroring is needed there, and this function is simply never called in that case.
bool mirrorFinalizedFileToSdRoot(const std::string& delugePath, const char* sdRoot) {
	auto opened = deluge::io::File::open(delugePath, DELUGE_FILE_READ);
	if (!opened) {
		return false;
	}
	deluge::io::File file = std::move(opened.value());
	auto sizeResult = file.size();
	if (!sizeResult) {
		return false;
	}
	std::vector<std::byte> bytes(*sizeResult);
	size_t totalRead = 0;
	while (totalRead < bytes.size()) {
		auto chunk = file.read(std::span<std::byte>(bytes.data() + totalRead, bytes.size() - totalRead));
		if (!chunk || chunk->empty()) {
			break;
		}
		totalRead += chunk->size();
	}
	if (totalRead != bytes.size()) {
		return false;
	}

	std::filesystem::path outPath = std::filesystem::path(sdRoot) / delugePath;
	std::error_code ec;
	std::filesystem::create_directories(outPath.parent_path(), ec);
	std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
	if (!out) {
		return false;
	}
	out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	return static_cast<bool>(out);
}

void teardown() {
	if (g_source != nullptr) {
		deluge_sample_source_close(g_source);
		g_source = nullptr;
	}
	if (g_recorder != nullptr) {
		// abort() is the correct teardown for a recorder that's still CAPTURING_DATA -- it marks
		// the recorder for file/sample cleanup on its next cardRoutine() tick (see cardRoutine()'s
		// ABORTED branch); we don't drive that drain here, this is a one-shot diagnostic process.
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
		// file finalized and closed) by the time this normally runs -- no fiber cleanup work is left
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

uint8_t deluge_harness_recorder_probe(uint8_t numChannels, uint32_t numFrames, uint32_t pumpDrainTicks) {
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

	// Drive the fiber drain a few times BEFORE probing, same as the sim round-trip harness does --
	// this is what actually flushes the fed audio to the SD file (writeOneCompletedCluster()), which
	// is a precondition for RecordingReadSource to have anything to read on the sim's synchronous
	// path. Deliberately do NOT call endSyncedRecording()/finalize: status stays CAPTURING_DATA, so
	// the Sample genuinely has no efatfs read handle for the whole probe.
	for (uint32_t i = 0; i < pumpDrainTicks; i++) {
		g_recorder->cardRoutine();
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

	// A first CLUSTER_ENQUEUE-style acquire is expected to come back LOADING (the fill is scheduled,
	// not synchronous) -- retry a bounded number of times, draining the fill via the async task
	// (driveFillDrain -> deluge_streaming_drain_queue_blocking) in between. A still-recording Sample
	// has no efatfs read handle, so the read fails and the acquire stays LOADING here regardless of
	// draining -- that non-resolution is the point being probed, not a harness bug.
	for (int i = 0; i < 32 && state == DELUGE_REGION_LOADING; i++) {
		driveFillDrain();
		state = deluge_sample_region_acquire_ex(g_source, /*index=*/0, /*direction=*/+1, /*priority=*/0, &out);
	}

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

	// Pump cardRoutine() to RecorderStatus::COMPLETE -- this is what actually runs
	// finalizeRecordedFile(). Bounded so a stuck state machine fails the harness instead of hanging.
	bool completed = false;
	for (int32_t i = 0; i < 200000; i++) {
		if (g_finalizedRecorder->status.load(std::memory_order_acquire) == RecorderStatus::COMPLETE) {
			completed = true;
			break;
		}
		Error e = g_finalizedRecorder->cardRoutine();
		if (e != Error::NONE && e != Error::MAX_FILE_SIZE_REACHED) {
			break;
		}
	}
	if (!completed) {
		finalizedTeardown();
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

	// Host-sim wrinkle (see mirrorFinalizedFileToSdRoot()'s doc): if this binary's streaming read
	// path is backed by a POSIX DELUGE_SD_ROOT separate from the FAT image the recorder wrote into,
	// mirror the finalized file there first. A no-op (and harmless) everywhere else.
	if (const char* sdRoot = std::getenv("DELUGE_SD_ROOT"); sdRoot != nullptr && sdRoot[0] != '\0') {
		if (!mirrorFinalizedFileToSdRoot(sample->filePath, sdRoot)) {
			finalizedTeardown();
			return 0;
		}
	}

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
	// Same bounded sim-side drain-retry loop as deluge_harness_recorder_probe() above; a no-op under
	// async_streaming_loader (host_app), where deluge_harness_recorder_finalized_multicluster_probe_poll()
	// is the Rust-driven counterpart.
	for (int i = 0; i < 32 && state == DELUGE_REGION_LOADING; i++) {
		driveFillDrain();
		state = deluge_sample_region_acquire_ex(g_finalizedSource, regionIndex, +1, 0, &out);
	}
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

#endif // DELUGE_HOST
