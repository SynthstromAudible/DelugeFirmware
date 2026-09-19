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

#include "harness/recorder_roundtrip_scenario.h"
#include "libdeluge/file_directory.h"
#include "libdeluge/file_write.h"

#ifdef DELUGE_HOST

#include "definitions_cxx.hpp"
#include "dsp/stereo_sample.h"
#include "io/file.hpp"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/byte_channel.h"
#include "storage/cluster/cluster.h"
#include "storage/owner.h"

#include <memory>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Regression-gate probes (see harness/recorder_readback_probe.h). Their fill drain runs on the
// async fill task via deluge_streaming_drain_queue_blocking() (the probes run on the worker fiber).
extern "C" {
uint8_t deluge_harness_recorder_probe(uint8_t numChannels, uint32_t numFrames, uint32_t pumpDrainTicks);
void deluge_harness_recorder_probe_end();

uint8_t deluge_harness_recorder_finalized_multicluster_probe(uint8_t numChannels, uint32_t numFrames,
                                                             uint32_t regionIndex);
uint32_t deluge_harness_recorder_finalized_multicluster_probe_table_clusters();
uint32_t deluge_harness_recorder_finalized_multicluster_probe_expected_clusters();
uint8_t deluge_harness_recorder_finalized_multicluster_probe_bytes_ok();
void deluge_harness_recorder_finalized_multicluster_probe_end();
}

namespace {

int g_failures = 0;
int g_cases = 0;

/// Deterministic 24-bit ramp, distinct per channel, spanning near the full signed 24-bit range —
/// pseudo-random-looking (so it can't pass by coincidence, e.g. all-zero bytes) but perfectly
/// reproducible. Returned already left-shifted into a q31_t's top 24 bits (bits 31..8), because
/// SampleRecorder::feedAudio() writes exactly bytes [1,2,3] of the little-endian int32 sample (i.e.
/// bits 8..31) to the file, discarding the low byte — pre-zeroing that low byte makes the stored
/// 3-byte value byte-identical to `expected24`, with no truncation ambiguity to account for.
int32_t expected24(uint32_t frameIndex, uint32_t channel) {
	uint32_t v = (frameIndex * 97u + channel * 131u + 17u) % (1u << 24);
	return static_cast<int32_t>(v) - (1 << 23); // centre around 0, full signed 24-bit span
}

int32_t rampSample(uint32_t frameIndex, uint32_t channel) {
	return expected24(frameIndex, channel) << 8;
}

struct Case {
	const char* name;
	uint8_t channels;
	bool extraMargins;
	uint32_t frames;
};

// Read the whole file back via deluge::io::File (open/seek/read — the general-purpose, read-capable
// port; deluge::io::Stream's READ mode reads through read_at_via()) into a plain host buffer.
bool readWholeFile(const std::string& path, std::vector<std::byte>& out) {
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (!opened) {
		fprintf(stderr, "  FAIL: could not reopen '%s' for read (status=%d)\n", path.c_str(),
		        static_cast<int>(opened.error()));
		return false;
	}
	deluge::io::File file = std::move(opened.value());
	auto sizeResult = file.size();
	if (!sizeResult) {
		fprintf(stderr, "  FAIL: size() failed on '%s'\n", path.c_str());
		return false;
	}
	out.assign(*sizeResult, std::byte{0});
	size_t totalRead = 0;
	while (totalRead < out.size()) {
		auto chunk = file.read(std::span<std::byte>(out.data() + totalRead, out.size() - totalRead));
		if (!chunk) {
			fprintf(stderr, "  FAIL: read() failed on '%s' at offset %zu\n", path.c_str(), totalRead);
			return false;
		}
		if (chunk->empty()) {
			break; // EOF
		}
		totalRead += chunk->size();
	}
	if (totalRead != out.size()) {
		fprintf(stderr, "  FAIL: short read on '%s' (%zu of %zu bytes)\n", path.c_str(), totalRead, out.size());
		return false;
	}
	return true;
}

uint32_t readU32LE(const std::vector<std::byte>& b, size_t off) {
	return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8)
	       | (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}

bool tagEquals(const std::vector<std::byte>& b, size_t off, const char* tag) {
	return std::memcmp(b.data() + off, tag, 4) == 0;
}

/// How the case that is running now is getting on. One `cardRoutine()` per step, never a loop.
enum class CaseStep : uint8_t {
	Begin,  ///< Build the ramp, set the recorder up, feed it, end the recording.
	Pump,   ///< ONE cardRoutine() per call, until COMPLETE or the bound.
	Verify, ///< Read the file back.
	Done,   ///< Terminal; the caller moves to the next case.
	Failed, ///< Terminal.
};

/// The case's state, out of the stack frame because the pump now spans ticks.
///
/// It used to be `SampleRecorder rec;` plus a 200000-iteration `pumpToComplete()` loop, straight
/// through. That loop never yielded, so it only ever worked on the recorder's SYNCHRONOUS arm: ask
/// the channel instead and the driver gets no chance to run between ticks, nothing is answered, and
/// the loop exhausts its bound. Shaped after `g_offOwner` below, which already holds its recorder
/// this way for the same reason.
struct CaseRun {
	CaseStep step = CaseStep::Done;
	const Case* c = nullptr;
	std::optional<SampleRecorder> rec;
	std::vector<StereoSample> input;
	int32_t pumpTicks = 0;
	std::string path;
};
CaseRun g_case;

/// One tick of the pump. Bounded exactly as `pumpToComplete()` was, and reporting the same two
/// failures, so a wedged state machine still fails the gate rather than hanging it.
bool pumpOnce(SampleRecorder& rec) {
	if (rec.status.load(std::memory_order_acquire) == RecorderStatus::COMPLETE) {
		g_case.step = CaseStep::Verify;
		return true;
	}
	if (++g_case.pumpTicks > 200000) {
		fprintf(stderr, "  FAIL: cardRoutine() never reached COMPLETE (phase=%d issued=%d status=%d)\n",
		        (int)rec.finalizePhaseForDiag(), (int)rec.finalizeIssuedForDiag(),
		        (int)rec.status.load(std::memory_order_acquire));
		return false;
	}
	Error e = rec.cardRoutine();
	if (e != Error::NONE && e != Error::MAX_FILE_SIZE_REACHED) {
		fprintf(stderr, "  FAIL: cardRoutine() returned error %d\n", static_cast<int>(e));
		return false;
	}
	return true;
}

/// Byte-exact verification of a finished recording, factored out of runCase() so the off-owner case
/// below checks the SAME things rather than a weaker copy of them. A recording driven off the
/// storage owner must be indistinguishable from one driven on it -- that is the whole claim.
bool verifyRecordedFile(const std::string& path, uint8_t channels, uint32_t frames, bool extraMargins) {
	const Case c{/*name=*/"", channels, extraMargins, frames};
	std::vector<std::byte> bytes;
	if (!readWholeFile(path, bytes)) {
		return false;
	}

	const uint32_t headerLen = c.extraMargins ? 112 : 44;
	const uint32_t frameBytes = static_cast<uint32_t>(c.channels) * 3;
	// recordingExtraMargins genuinely appends kAudioClipMarginSizePostEnd extra samples past the
	// ramp (the post-end margin the feature exists to capture) — silence here, since the extra
	// frames fed above (after endSyncedRecording) are zeroed. Non-margin cases capture exactly
	// c.frames (endSyncedRecording's numMoreSamplesToCapture is 0 for those — see the call site).
	const uint32_t totalFrames = c.frames + (c.extraMargins ? static_cast<uint32_t>(kAudioClipMarginSizePostEnd) : 0);
	const uint32_t dataLen = totalFrames * frameBytes;

	bool ok = true;
	if (bytes.size() != headerLen + dataLen) {
		fprintf(stderr, "  FAIL: file size %zu != expected %u\n", bytes.size(), headerLen + dataLen);
		ok = false;
	}
	if (!tagEquals(bytes, 0, "RIFF")) {
		fprintf(stderr, "  FAIL: missing RIFF tag\n");
		ok = false;
	}
	if (!tagEquals(bytes, 8, "WAVE")) {
		fprintf(stderr, "  FAIL: missing WAVE tag\n");
		ok = false;
	}
	if (!tagEquals(bytes, 12, "fmt ")) {
		fprintf(stderr, "  FAIL: missing fmt tag\n");
		ok = false;
	}
	uint32_t riffSize = readU32LE(bytes, 4);
	if (riffSize != dataLen + headerLen - 8) {
		fprintf(stderr, "  FAIL: RIFF chunk size %u != expected %u\n", riffSize, dataLen + headerLen - 8);
		ok = false;
	}
	uint16_t fmtChannels = static_cast<uint16_t>(bytes[22]) | (static_cast<uint16_t>(bytes[23]) << 8);
	if (fmtChannels != c.channels) {
		fprintf(stderr, "  FAIL: fmt numChannels %u != expected %u\n", fmtChannels, c.channels);
		ok = false;
	}
	uint16_t bitsPerSample = static_cast<uint16_t>(bytes[34]) | (static_cast<uint16_t>(bytes[35]) << 8);
	if (bitsPerSample != 24) {
		fprintf(stderr, "  FAIL: bitsPerSample %u != 24\n", bitsPerSample);
		ok = false;
	}
	if (!tagEquals(bytes, headerLen - 8, "data")) {
		fprintf(stderr, "  FAIL: missing data tag at offset %u\n", headerLen - 8);
		ok = false;
	}
	uint32_t dataChunkSize = readU32LE(bytes, headerLen - 4);
	if (dataChunkSize != dataLen) {
		fprintf(stderr, "  FAIL: data chunk size %u != expected %u\n", dataChunkSize, dataLen);
		ok = false;
	}

	// Audio data, byte-for-byte, against the input ramp (frames >= c.frames are the silent
	// post-end margin for the extraMargins cases; expected value 0 there).
	if (bytes.size() >= static_cast<size_t>(headerLen) + dataLen) {
		size_t mismatches = 0;
		for (uint32_t i = 0; i < totalFrames && mismatches < 5; i++) {
			for (uint32_t ch = 0; ch < c.channels; ch++) {
				size_t off = headerLen + static_cast<size_t>(i) * frameBytes + static_cast<size_t>(ch) * 3;
				int32_t expected = (i < c.frames) ? expected24(i, ch) : 0;
				uint32_t got = static_cast<uint32_t>(bytes[off]) | (static_cast<uint32_t>(bytes[off + 1]) << 8)
				               | (static_cast<uint32_t>(bytes[off + 2]) << 16);
				// Sign-extend the 24-bit value read off disk for comparison against expected24()'s
				// signed range.
				int32_t gotSigned = static_cast<int32_t>(got << 8) >> 8;
				if (gotSigned != expected) {
					fprintf(stderr, "  FAIL: frame %u ch %u: got %d expected %d (offset %zu)\n", i, ch, gotSigned,
					        expected, off);
					mismatches++;
					ok = false;
				}
			}
		}
	}

	printf("  %s\n", ok ? "PASS" : "FAIL");
	return ok;
}

/// Start a case: everything up to the pump. The recorder and its input live in `g_case` from here,
/// because the pump below returns between ticks.
bool beginCase(const Case& c) {
	printf("CASE %s (channels=%u margins=%d frames=%u)\n", c.name, c.channels, c.extraMargins, c.frames);
	g_cases++;

	// Field-by-field: SampleRecorder is neither movable nor assignable, so the struct cannot be
	// replaced wholesale. Same reason `g_offOwner` resets its optional rather than reassigning.
	g_case.rec.reset();
	g_case.input.clear();
	g_case.path.clear();
	g_case.pumpTicks = 0;
	g_case.step = CaseStep::Begin;
	g_case.c = &c;

	// Build the input ramp.
	std::vector<StereoSample>& input = g_case.input;
	input.assign(c.frames, StereoSample{0, 0});
	for (uint32_t i = 0; i < c.frames; i++) {
		input[i].l = rampSample(i, 0);
		input[i].r = (c.channels == 2) ? rampSample(i, 1) : 0;
	}

	g_case.rec.emplace();
	SampleRecorder& rec = *g_case.rec;
	Error err = rec.setup(c.channels, AudioInputChannel::MIX, /*newKeepingReasons=*/false,
	                      /*shouldRecordExtraMargins=*/c.extraMargins, AudioRecordingFolder::RESAMPLE,
	                      /*buttonPressLatency=*/0, /*outputRecordingFrom=*/nullptr);
	if (err != Error::NONE) {
		fprintf(stderr, "  FAIL: setup() returned error %d\n", static_cast<int>(err));
		return false;
	}

	rec.feedAudio(std::span<StereoSample>(input));

	// endSyncedRecording(0): with MIX-mode (numSamplesExtraToCaptureAtEndSyncingWise == 0) and no
	// button-latency compensation, numMoreSamplesToCapture is 0 when extraMargins is off (so
	// finishCapturing() runs synchronously inside this call) and kAudioClipMarginSizePostEnd when
	// extraMargins is on (status goes to CAPTURING_DATA_WAITING_TO_STOP; feed that many more silent
	// frames to push it through to finishCapturing() — mirrors the real end-of-recording sequence).
	rec.endSyncedRecording(0);
	if (rec.status.load(std::memory_order_acquire) == RecorderStatus::CAPTURING_DATA_WAITING_TO_STOP) {
		// finishCapturing() only fires once feedAudio() observes numSamplesCaptured has reached (or
		// passed) sample->lengthInSamples -- i.e. it needs a call whose span EXTENDS PAST the target,
		// not one that lands exactly on it (matching how real callers feed fixed-size audio blocks
		// and the final block naturally overshoots by less than one block). Feed a few extra silent
		// frames beyond the margin so a later iteration inside feedAudio's own do-while loop re-checks
		// the stop condition and finds it satisfied.
		std::vector<StereoSample> silence(kAudioClipMarginSizePostEnd + 64, StereoSample{0, 0});
		rec.feedAudio(std::span<StereoSample>(silence));
	}

	g_case.pumpTicks = 0;
	g_case.step = CaseStep::Pump;
	return true;
}

/// Finish a case once its pump has reached COMPLETE.
bool verifyCase() {
	SampleRecorder& rec = *g_case.rec;
	const Case& c = *g_case.c;
	g_case.path = rec.filePathCreated;
	if (g_case.path.empty()) {
		fprintf(stderr, "  FAIL: no file path recorded\n");
		return false;
	}
	return verifyRecordedFile(g_case.path, c.channels, c.frames, c.extraMargins);
}

/// Advance the case that is running by ONE step. True while it is still going.
bool stepCase() {
	switch (g_case.step) {
	case CaseStep::Pump:
		if (!pumpOnce(*g_case.rec)) {
			g_case.step = CaseStep::Failed;
			return false;
		}
		return g_case.step != CaseStep::Verify || true;
	case CaseStep::Verify:
		g_case.step = verifyCase() ? CaseStep::Done : CaseStep::Failed;
		return false;
	case CaseStep::Begin:
	case CaseStep::Done:
	case CaseStep::Failed:
		return false;
	}
	return false;
}

} // namespace

namespace {

/// The off-owner recording case: a real `SampleRecorder` driven ENTIRELY from a non-fiber context,
/// which is the shape the device actually uses and the one the eight cases above cannot reach.
///
/// They cannot reach it because `deluge_scenario_start_recorder_roundtrip` dispatches the whole
/// oracle onto the fiber -- it has to, because the finalized probe's streaming drain must block and
/// resume against the fill task there. So every one of those cases measures `on_owner=1`, the
/// cluster write takes its synchronous arm, and two things go untested: the async write path itself,
/// and `cardRoutine`'s dispatch of its BLOCKING ticks (create, finalize, abort) back to the owner.
/// Deleting that dispatch left all eleven cases passing.
///
/// Tick-driven rather than a loop, because that is the point: the pump must return to its caller so
/// the storage owner can run the ticks this case hands it, and so the Rust write task can make
/// progress on the clusters it has been given.
struct OffOwnerCase {
	std::unique_ptr<SampleRecorder> rec;
	/// Whether this run records stereo with a silent right channel and permits alteration, so
	/// finalize takes its REMOVE_RIGHT_CHANNEL branch. Without a case that does, the whole batch
	/// machine is dead code: no other case permits alteration.
	bool alter = false;
	/// Whether this run aborts mid-recording instead of finishing, so the abort machine runs and its
	/// close-before-unlink ordering is exercised.
	bool abortRun = false;
	/// Whether a competing write-channel operation keeps taking the channel for the whole run. See
	/// driveNeighbour().
	bool busy = false;
	/// Whether a cluster write is held in flight past the recorder's bound. See driveStall().
	bool stall = false;
	/// Whether the recording-folder scan's directory open is held past the recorder's bound. See the dirstall case.
	bool dirStall = false;
	/// Whether the busy case's competing operation is in flight.
	bool neighbourHeld = false;
	/// Whether the stall case has slowed the channel down yet.
	bool stallArmed = false;
	/// Whether the stall case saw the card error while the stalled write was still outstanding: the "fail now"
	/// half of the ruling, which a take that failed only once the answer came would not show.
	bool failedWhilePending = false;
	/// Process-wide counters at the start of the case, so its line reports its own share.
	int32_t servedBefore = 0;
	int32_t refusalsBefore = 0;
	int32_t tierBefore = 0;
	/// Free clusters before the recording started. An abort that unlinks before its close completes
	/// leaks the chain, which no byte comparison can see -- the file is gone either way.
	uint32_t freeBefore = 0;
	std::string path;
	int32_t ticks = 0;
	bool started = false;
};
OffOwnerCase g_offOwner;

/// Mono, no margins, and big enough to complete several clusters -- a single-cluster recording would
/// finish inside the finalize drain and never exercise the steady-state write at all.
constexpr uint32_t kOffOwnerFrames = 33153;

/// `SampleRecorder::AbortPhase::Done`, as the diagnostic accessor reports it.
constexpr uint8_t kAbortPhaseDone = 3;

/// Synchronous card operations the recorder has begun off the storage owner after boot, process-wide.
int32_t recorderTier() {
	return deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Recorder);
}

/// Each case's name, indexed by the `mode` passed to deluge_scenario_offowner_begin.
constexpr const char* kModeNames[] = {"recording", "alterfile", "abort", "busy", "stall", "dirstall"};

/// The write channel's latency in the busy case: long enough that the recorder asks while the competing
/// operation is still in flight, and far inside the recorder's bound, so a refusal is asked again rather
/// than given up on.
constexpr uint32_t kBusyLatencyMs = 20;

/// The write channel's latency once the stall case has had a cluster served: well past the recorder's bound
/// of 600 ticks at one scenario tick per virtual millisecond.
constexpr uint32_t kStallLatencyMs = 3000;

/// The directory channel's latency in the dirstall case: the recording-folder scan's open stays pending well past the
/// recorder's bound of 600 ticks, at one scenario tick per virtual millisecond.
constexpr uint32_t kDirStallLatencyMs = 3000;

/// The busy case's competing operation: a free-space query, asked again one tick after each answer. The
/// tick's gap is what lets the recorder in; without it the neighbour takes the channel the moment it frees
/// up and the recorder is refused until it gives up.
void driveNeighbour() {
	if (g_offOwner.neighbourHeld) {
		uint32_t freeClusters = 0;
		if (deluge_file_write_collect_stats(&freeClusters) == DELUGE_FILE_WRITE_PENDING) {
			return;
		}
		g_offOwner.neighbourHeld = false;
		return; // Leave this tick's channel to the recorder.
	}
	g_offOwner.neighbourHeld = deluge_file_write_request_stats();
}

/// The stall case: slow the channel down once a cluster has been served, so the next cluster write is still
/// pending when the recorder reaches its bound. Latency is read when an operation starts, so the stalled
/// write keeps its 3000 ms after this puts the latency back.
void driveStall(const SampleRecorder& rec) {
	if (!g_offOwner.stallArmed && SampleRecorder::writeChannelServedOps() > g_offOwner.servedBefore) {
		deluge_file_write_set_latency_ms(kStallLatencyMs);
		g_offOwner.stallArmed = true;
	}
	if (rec.hadCardError) {
		if (rec.writePending()) {
			g_offOwner.failedWhilePending = true;
		}
		deluge_file_write_set_latency_ms(0);
	}
}

/// Report the busy or stall case once its recording is COMPLETE.
/// @return As deluge_scenario_offowner_tick.
int32_t finishGiveUpCase(SampleRecorder& rec) {
	if (g_offOwner.neighbourHeld) {
		uint32_t freeClusters = 0;
		if (deluge_file_write_collect_stats(&freeClusters) == DELUGE_FILE_WRITE_PENDING) {
			return 0; // Let the competing operation finish, so the next case starts on an idle channel.
		}
		g_offOwner.neighbourHeld = false;
	}
	deluge_file_write_set_latency_ms(0);
	const bool cardError = rec.hadCardError;
	const bool pendingAtEnd = rec.writePending();
	const bool fileOpen = rec.file.has_value();
	const std::string path = rec.filePathCreated;
	// Freed BEFORE the tier is read: a destructor left with a file to close is itself a violation.
	g_offOwner.rec.reset();
	const int32_t tier = recorderTier() - g_offOwner.tierBefore;
	const int32_t refusals = SampleRecorder::channelRefusals() - g_offOwner.refusalsBefore;
	const int32_t served = SampleRecorder::writeChannelServedOps() - g_offOwner.servedBefore;
	if (g_offOwner.busy) {
		const bool readBack = !cardError && !path.empty()
		                      && verifyRecordedFile(path, /*channels=*/1, kOffOwnerFrames, /*extraMargins=*/false);
		const bool ok = readBack && refusals > 0 && tier == 0;
		printf("RW-BUSY ok=%d readBack=%d refusals=%d served=%d tier=%d ticks=%d\n", (int)ok, (int)readBack,
		       (int)refusals, (int)served, (int)tier, g_offOwner.ticks);
		fflush(nullptr);
		return ok ? 1 : 2;
	}
	if (g_offOwner.dirStall) {
		deluge_file_directory_set_latency_ms(0);
		// The create failed at the path search, so there is no path, no file, and no synchronous open behind it.
		const bool ok = cardError && path.empty() && !fileOpen && tier == 0;
		printf("RW-DIR-STALL ok=%d cardError=%d path=%s fileOpen=%d tier=%d ticks=%d\n", (int)ok, (int)cardError,
		       path.empty() ? "-" : path.c_str(), (int)fileOpen, (int)tier, g_offOwner.ticks);
		fflush(nullptr);
		return ok ? 1 : 2;
	}
	const bool ok = cardError && g_offOwner.failedWhilePending && !pendingAtEnd && !fileOpen && tier == 0;
	printf("RW-STALL ok=%d cardError=%d failedWhilePending=%d pendingAtEnd=%d fileOpen=%d "
	       "served=%d tier=%d ticks=%d\n",
	       (int)ok, (int)cardError, (int)g_offOwner.failedWhilePending, (int)pendingAtEnd, (int)fileOpen, (int)served,
	       (int)tier, g_offOwner.ticks);
	fflush(nullptr);
	return ok ? 1 : 2;
}

} // namespace

extern "C" void deluge_scenario_offowner_begin(int32_t mode) {
	g_offOwner.rec = std::make_unique<SampleRecorder>();
	g_offOwner.ticks = 0;
	g_offOwner.path.clear();
	g_offOwner.started = false;
	g_offOwner.alter = (mode == 1);
	g_offOwner.abortRun = (mode == 2);
	g_offOwner.busy = (mode == 3);
	g_offOwner.stall = (mode == 4);
	g_offOwner.dirStall = (mode == 5);
	g_offOwner.neighbourHeld = false;
	g_offOwner.stallArmed = false;
	g_offOwner.failedWhilePending = false;
	g_offOwner.servedBefore = SampleRecorder::writeChannelServedOps();
	g_offOwner.refusalsBefore = SampleRecorder::channelRefusals();
	g_offOwner.tierBefore = recorderTier();
	deluge_file_write_set_latency_ms(g_offOwner.busy ? kBusyLatencyMs : 0);
	if (g_offOwner.dirStall) {
		// The scan an earlier case already made is cached; force it again, and hold its open past the bound.
		audioFileManager.highestUsedAudioRecordingNumberNeedsReChecking.set(
		    static_cast<size_t>(AudioRecordingFolder::RESAMPLE));
		deluge_file_directory_set_latency_ms(kDirStallLatencyMs);
	}
	g_offOwner.freeBefore = 0;
	{
		uint32_t total = 0;
		(void)deluge_efatfs_stats(&g_offOwner.freeBefore, &total);
	}

	// stderr as well as stdout: this case runs AFTER the scenario's own fflush(nullptr), so a
	// stdout-only line can be lost at exit -- and a case whose output vanishes is a case nobody can
	// tell from one that never ran.
	const char* modeName = (mode >= 0 && mode <= 5) ? kModeNames[mode] : "unknown";
	printf("CASE offowner_%s (channels=%d frames=%u onOwner=%d)\n", modeName, g_offOwner.alter ? 2 : 1, kOffOwnerFrames,
	       (int)deluge::storage::Owner::on_owner());
	fflush(nullptr);

	Error err = g_offOwner.rec->setup(g_offOwner.alter ? 2 : 1, AudioInputChannel::MIX, /*newKeepingReasons=*/false,
	                                  /*shouldRecordExtraMargins=*/false, AudioRecordingFolder::RESAMPLE,
	                                  /*buttonPressLatency=*/0, /*outputRecordingFrom=*/nullptr);
	if (err != Error::NONE) {
		fprintf(stderr, "  FAIL: setup() returned error %d\n", static_cast<int>(err));
		g_offOwner.rec.reset();
		return;
	}

	// Alteration case: permit it, and leave the right channel silent so inputHasNoRightChannel()
	// picks REMOVE_RIGHT_CHANNEL. The result is a MONO file carrying the left ramp, which is exactly
	// what the plain case's verification already checks for.
	g_offOwner.rec->allowFileAlterationAfter = g_offOwner.alter;

	std::vector<StereoSample> input(kOffOwnerFrames);
	for (uint32_t i = 0; i < kOffOwnerFrames; i++) {
		input[i].l = rampSample(i, 0);
		input[i].r = 0;
	}
	g_offOwner.rec->feedAudio(std::span<StereoSample>(input));
	if (!g_offOwner.abortRun) {
		g_offOwner.rec->endSyncedRecording(0);
	}
	// The abort run does NOT end the recording here. It lets a few ticks pass first so the file is
	// actually created and some clusters are on the card -- aborting before that leaves the machine
	// nothing to close or delete, which is the one shape that proves least.
	g_offOwner.started = true;
}

extern "C" int32_t deluge_scenario_offowner_tick() {
	if (!g_offOwner.started || !g_offOwner.rec) {
		return 2; // failed to start
	}
	SampleRecorder& rec = *g_offOwner.rec;

	if (g_offOwner.abortRun) {
		if (rec.status.load(std::memory_order_acquire) == RecorderStatus::CAPTURING_DATA && g_offOwner.ticks >= 8) {
			// File created, clusters written: now abandon it.
			rec.abort();
		}
		// Wait for the abort MACHINE to finish, not for AWAITING_DELETION: this recorder is held by
		// the harness, so `pointerHeldElsewhere` is set and the status legitimately stays ABORTED.
		// What is under test is that the close completed and the unlink then ran.
		const bool cleanupDone = rec.status.load(std::memory_order_acquire) == RecorderStatus::ABORTED
		                         && rec.abortPhaseForDiag() == kAbortPhaseDone;
		if (!cleanupDone) {
			if (++g_offOwner.ticks > 2000) {
				fprintf(stderr, "  FAIL: abort cleanup never finished (status=%d phase=%d held=%d)\n",
				        (int)rec.status.load(std::memory_order_acquire), (int)rec.abortPhaseForDiag(),
				        (int)rec.pointerHeldElsewhere);
				g_offOwner.rec.reset();
				return 2;
			}
			(void)rec.cardRoutine();
			return 0;
		}
		const std::string path = rec.filePathCreated;
		g_offOwner.rec.reset();
		uint32_t freeAfter = 0;
		uint32_t total = 0;
		(void)deluge_efatfs_stats(&freeAfter, &total);
		// The clusters must come back. An abort that unlinks before its close has COMPLETED leaks the
		// chain, and no byte comparison can see that -- the file is gone either way.
		std::vector<std::byte> leftover;
		const bool gone = path.empty() || !readWholeFile(path, leftover);
		const bool noLeak = (freeAfter >= g_offOwner.freeBefore);
		printf("OO-LEAK freeBefore=%u freeAfter=%u gone=%d ok=%d\n", g_offOwner.freeBefore, freeAfter, (int)gone,
		       (int)(gone && noLeak));
		fflush(nullptr);
		if (!noLeak) {
			fprintf(stderr, "  FAIL: aborted recording leaked clusters (%u -> %u)\n", g_offOwner.freeBefore, freeAfter);
		}
		if (!gone) {
			fprintf(stderr, "  FAIL: aborted recording left its file behind\n");
		}
		return (gone && noLeak) ? 1 : 2;
	}

	// Before the recorder's own tick, so the neighbour asks first and the recorder is the one refused.
	if (g_offOwner.busy) {
		driveNeighbour();
	}
	if (g_offOwner.stall) {
		driveStall(rec);
	}

	if (rec.status.load(std::memory_order_acquire) == RecorderStatus::COMPLETE) {
		if (g_offOwner.busy || g_offOwner.stall || g_offOwner.dirStall) {
			return finishGiveUpCase(rec);
		}
		const std::string path = rec.filePathCreated;
		g_offOwner.rec.reset(); // closes the stream and releases the capture buffers
		if (path.empty()) {
			fprintf(stderr, "  FAIL: no file path recorded\n");
			return 2;
		}
		const bool ok = verifyRecordedFile(path, /*channels=*/1, kOffOwnerFrames, /*extraMargins=*/false);
		// The evidence for THIS case, taken here rather than from RW-SUMMARY: that line is printed by
		// the on-fiber oracle, which has already returned by the time this case runs, so it cannot
		// see anything the off-owner recording did.
		printf("OO-RESULT ok=%d served=%d offOwnerTicks=%d offOwnerFinalizeTicks=%d offOwnerCreateTicks=%d ticks=%d\n",
		       (int)ok, (int)SampleRecorder::writeChannelServedOps(), (int)AudioEngine::recorderOffOwnerTicks(),
		       (int)AudioEngine::offOwnerFinalizeTicks(), (int)AudioEngine::offOwnerCreateTicks(), g_offOwner.ticks);
		fflush(nullptr);
		return ok ? 1 : 2;
	}

	if (++g_offOwner.ticks > 400000) {
		fprintf(stderr, "  FAIL: off-owner recording never reached COMPLETE (%d ticks)\n", g_offOwner.ticks);
		g_offOwner.rec.reset();
		return 2;
	}

	// The real caller's decision, not a direct call: off the owner with the channel live this runs
	// here, and the blocking ticks inside dispatch themselves back to the owner.
	Error e = rec.cardRoutine();
	if (e != Error::NONE && e != Error::MAX_FILE_SIZE_REACHED) {
		fprintf(stderr, "  FAIL: off-owner cardRoutine() returned error %d\n", static_cast<int>(e));
		g_offOwner.rec.reset();
		return 2;
	}
	return 0;
}

extern "C" void deluge_scenario_offowner_report() {
	printf("RW-TIER violations=%d refusals=%d\n", (int)recorderTier(), (int)SampleRecorder::channelRefusals());
	fflush(nullptr);
}

extern "C" int32_t deluge_scenario_run_recorder_roundtrip() {
	// Whether this binary can reach the async write driver at all. A module missing from
	// golden_vt_render's `#[path]` list resolves to the C++ weak fallback, every write takes the
	// synchronous arm, and every check below still passes -- so the wiring is measured, not assumed.
	printf("RW-ACTIVE asyncActive=%d\n", (int)deluge_file_write_async_active());
	g_failures = 0;
	g_cases = 0;

	printf("Cluster::size = %zu\n", Cluster::size);

	const uint32_t clusterSize = static_cast<uint32_t>(Cluster::size);

	// Frame counts chosen relative to the runtime Cluster::size so the matrix genuinely spans
	// multiple clusters plus a partial final cluster, and also hits an exact cluster-byte-boundary
	// file length, without hardcoding a cluster size the host FAT geometry might not actually use.
	auto framesForBytes = [](uint32_t headerLen, uint32_t frameBytes, uint32_t targetBytes) -> uint32_t {
		uint32_t avail = (targetBytes > headerLen) ? (targetBytes - headerLen) : 0;
		return avail / frameBytes;
	};
	// Smallest frame count >= minFrames such that (headerLen + frames*frameBytes) lands exactly on a
	// cluster boundary (a multiple of clusterSize). frameBytes is always odd (3 or 6 is even, but 3
	// itself is coprime to the power-of-two clusterSize; 6 = 2*3, still coprime to the odd part —
	// exhaustive search over one clusterSize period always finds a solution).
	auto exactBoundaryFrames = [&](uint32_t headerLen, uint32_t frameBytes, uint32_t minFrames) -> uint32_t {
		uint32_t frames = minFrames;
		for (uint32_t tries = 0; tries < clusterSize; tries++, frames++) {
			if ((static_cast<uint64_t>(headerLen) + static_cast<uint64_t>(frames) * frameBytes) % clusterSize == 0) {
				return frames;
			}
		}
		return minFrames; // Shouldn't happen; falls back to a non-exact (still valid) case.
	};

	std::vector<Case> cases;

	// Small, well under one cluster.
	cases.push_back({"mono_small", 1, false, 137});
	cases.push_back({"stereo_small", 2, false, 211});

	// Multi-cluster with a partial final cluster (the common case).
	cases.push_back({"mono_multi_cluster_partial", 1, false, framesForBytes(44, 3, 3 * clusterSize) + 97});
	cases.push_back({"stereo_multi_cluster_partial", 2, false, framesForBytes(44, 6, 3 * clusterSize) + 53});

	// Exact cluster-boundary file length (no partial final cluster).
	cases.push_back(
	    {"mono_exact_cluster_boundary", 1, false, exactBoundaryFrames(44, 3, framesForBytes(44, 3, 2 * clusterSize))});
	cases.push_back({"stereo_exact_cluster_boundary", 2, false,
	                 exactBoundaryFrames(44, 6, framesForBytes(44, 6, 2 * clusterSize))});

	// recordingExtraMargins (112-byte header) variant, spanning multiple clusters + partial.
	cases.push_back({"mono_extra_margins", 1, true, framesForBytes(112, 3, 2 * clusterSize) + 61});
	cases.push_back({"stereo_extra_margins", 2, true, framesForBytes(112, 6, 2 * clusterSize) + 61});

	for (const Case& c : cases) {
		// Drives the case's steps here, which is BEHAVIOURALLY WHAT pumpToComplete() did: no yield
		// between ticks. The split above is what makes the next step possible -- driving these from the
		// scenario's dispatch, so the storage driver runs between ticks and the channel arm can answer.
		// Until then this stays a spinner, and the predicate collapse stays blocked.
		if (!beginCase(c)) {
			g_case.step = CaseStep::Failed;
		}
		while (stepCase()) {}
		if (g_case.step != CaseStep::Done) {
			g_failures++;
		}
	}

	printf("%d/%d cases passed\n", g_cases - g_failures, g_cases);

	// Probe live read-back of a still-recording sample on THIS target. RecordingReadSource
	// is gone, so this must NOT resolve READY (state=1) anywhere -- a still-recording Sample has no
	// valid reader. The failed read gets re-queued at lowest priority rather than failed outright (the
	// async fill task's fill_once, streaming_loader.rs), so the acquire is expected to stay LOADING
	// (state=2), not settle on UNAVAILABLE.
	uint8_t state = deluge_harness_recorder_probe(/*numChannels=*/1, /*numFrames=*/50, /*pumpDrainTicks=*/10);
	printf("recorder live-readback probe: state=%u (1=READY 2=LOADING 3=UNAVAILABLE 0=harness-error)\n", state);
	deluge_harness_recorder_probe_end();
	if (state != 2) {
		fprintf(stderr, "  NOTE: still-recording live-readback probe did not resolve LOADING (state=%u)\n", state);
		g_failures++;
	}

	// Regression gate: a real, FINALIZED (COMPLETE) multi-cluster mono recording --
	// finalizeRecordedFile()'s no-alteration else-branch, the only branch AudioClip recording ever
	// takes -- must leave the overview cache sized to the real cluster count, and region index 1+
	// must resolve READY with correct bytes through the same port real playback uses.
	g_cases++;
	uint32_t regressionFrames = framesForBytes(44, 3, 3 * clusterSize) + 400;
	printf("CASE finalized_multicluster_regression (channels=1 margins=0 frames=%u regionIndex=1)\n", regressionFrames);
	uint8_t regressionState =
	    deluge_harness_recorder_finalized_multicluster_probe(/*numChannels=*/1, regressionFrames, /*regionIndex=*/1);
	uint32_t tableClusters = deluge_harness_recorder_finalized_multicluster_probe_table_clusters();
	uint32_t expectedClusters = deluge_harness_recorder_finalized_multicluster_probe_expected_clusters();
	uint8_t bytesOk = deluge_harness_recorder_finalized_multicluster_probe_bytes_ok();
	printf("  table_clusters()=%u expected_clusters()=%u acquire_state=%u (1=READY) bytes_ok=%u\n", tableClusters,
	       expectedClusters, regressionState, bytesOk);
	deluge_harness_recorder_finalized_multicluster_probe_end();
	bool regressionOk =
	    (tableClusters != 0) && (tableClusters >= expectedClusters) && (regressionState == 1) && (bytesOk == 1);
	if (!regressionOk) {
		fprintf(stderr,
		        "  FAIL: finalized multi-cluster regression gate -- table_clusters=%u expected_clusters=%u "
		        "acquire_state=%u bytes_ok=%u\n",
		        tableClusters, expectedClusters, regressionState, bytesOk);
		g_failures++;
	}
	printf("  %s\n", regressionOk ? "PASS" : "FAIL");

	// The rung's whole evidence. `served` says the async arm executed; `offOwnerTicks` says the drain
	// actually left the storage owner. Served alone can be non-zero while every write was still
	// handled on the owner, which would be a rename rather than a relocation.
	printf("RW-SUMMARY served=%d offOwnerTicks=%d\n", (int)SampleRecorder::writeChannelServedOps(),
	       (int)AudioEngine::recorderOffOwnerTicks());
	// A terminator, so a run that aborted part-way is a gate FAILURE rather than a silently
	// truncated log: a stage that never ran asserts nothing, and the failure count cannot show that.
	printf("RW-RESULT failures=%d\n", (int)g_failures);

	fflush(nullptr);
	return g_failures;
}

#endif // DELUGE_HOST
