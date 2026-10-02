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

#include "scenarios/recorder_roundtrip_scenario.h"
#include "libdeluge/file_directory.h"
#include "libdeluge/file_stream.h"

#include "definitions_cxx.hpp"
#include "dsp/stereo_sample.h"
#include "io/file.hpp"
#include "io/stream.hpp"
#include "libdeluge/sample_source.h" // DELUGE_REGION_LOADING
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/byte_channel.h"
#include "storage/cluster/cluster.h"

#include <memory>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Regression-gate probes (see harness/recorder_readback_probe.h), driven one step per call below.
extern "C" {
uint8_t deluge_harness_recorder_probe(uint8_t numChannels, uint32_t numFrames);
void deluge_harness_recorder_probe_drain_tick();
uint8_t deluge_harness_recorder_probe_acquire();
uint8_t deluge_harness_recorder_probe_poll();
void deluge_harness_recorder_probe_abort();
uint8_t deluge_harness_recorder_probe_settle_tick();
void deluge_harness_recorder_probe_end();

uint8_t deluge_harness_recorder_finalized_multicluster_probe(uint8_t numChannels, uint32_t numFrames,
                                                             uint32_t regionIndex);
uint8_t deluge_harness_recorder_finalized_multicluster_probe_tick();
uint8_t deluge_harness_recorder_finalized_multicluster_probe_finish(uint32_t regionIndex);
uint8_t deluge_harness_recorder_finalized_multicluster_probe_poll();
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

/// The case's state, out of the stack frame because the pump spans ticks.
///
/// A loop that pumps straight through without yielding only works on the recorder's SYNCHRONOUS arm:
/// ask the channel instead and the driver gets no chance to run between ticks, nothing is answered,
/// and the loop exhausts its bound. Shaped after `g_ticked` below, which holds its recorder the same
/// way for the same reason.
struct CaseRun {
	CaseStep step = CaseStep::Done;
	const Case* c = nullptr;
	std::optional<SampleRecorder> rec;
	std::vector<StereoSample> input;
	int32_t pumpTicks = 0;
	std::string path;
};
CaseRun g_case;
/// The case list and cursor, at file scope because the drive spans ticks.
std::vector<Case> g_caseList;
size_t g_caseCursor = 0;

/// One tick of the pump. Bounded, so a wedged state machine fails the gate rather than hanging it.
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

/// Byte-exact verification of a finished recording, factored out of runCase() so the `ticked_` cases
/// below check the SAME things rather than a weaker copy of them. A recording in one of their shapes
/// must be indistinguishable from one in the matrix -- that is the whole claim.
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
	// replaced wholesale. Same reason `g_ticked` resets its optional rather than reassigning.
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

/// The `ticked_` recording cases: a real `SampleRecorder` driven ENTIRELY by the caller's ticks,
/// the shape the device actually uses, with the shapes the eight cases above do not take -- the
/// alteration branch, an abort, a busy channel, a stalled write -- each with its own evidence line.
///
/// Tick-driven rather than a loop, because that is the point: the pump must return to its caller so
/// the Rust write task can make progress on the clusters it has been given.
struct TickedCase {
	std::unique_ptr<SampleRecorder> rec;
	/// Whether this run records stereo with a silent right channel and permits alteration, so
	/// finalize takes its REMOVE_RIGHT_CHANNEL branch. Without a case that does, the whole batch
	/// machine is dead code: no other case permits alteration.
	bool alter = false;
	/// Whether this run aborts mid-recording instead of finishing, so the abort machine runs and its
	/// close-before-unlink ordering is exercised.
	bool abortRun = false;
	/// Whether a competing stream-channel operation keeps taking the channel for the whole run. See
	/// driveNeighbour().
	bool busy = false;
	/// Whether a cluster write is held in flight past the recorder's bound. See driveStall().
	bool stall = false;
	/// Whether the recording-folder scan's directory open is held past the recorder's bound. See the dirstall case.
	bool dirStall = false;
	/// Whether the create's open fails with a status other than NOT_FOUND. See the openioerr case.
	bool openIoErr = false;
	/// Whether the openioerr case has unmounted the card under the held open.
	bool unmounted = false;
	/// Whether the openioerr case's recording folder existed before it started. The case proves nothing if it did.
	bool folderBefore = false;
	/// The status the openioerr case's create open failed with, read before the recorder is freed.
	DelugeStatus openError = DELUGE_OK;
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
	/// The abort-during-open cases: abort while the channel is opening the file -- for the create, or for
	/// finalize's reopen to alter it.
	bool abortOpen = false;
	bool abortSent = false;
	/// Ticks since the abort finished, given to an operation the abort left behind to land.
	int32_t settleTicks = 0;
	std::string path;
	int32_t ticks = 0;
	bool started = false;
};
TickedCase g_ticked;

/// Mono, no margins, and big enough to complete several clusters -- a single-cluster recording would
/// finish inside the finalize drain and never exercise the steady-state write at all.
constexpr uint32_t kTickedFrames = 33153;

/// `SampleRecorder::AbortPhase::Done`, as the diagnostic accessor reports it.
constexpr uint8_t kAbortPhaseDone = 3;

/// Synchronous card operations the recorder has begun on the interaction tier after boot, process-wide.
int32_t recorderTier() {
	return deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Recorder);
}

/// Each case's name, indexed by the `mode` passed to deluge_scenario_ticked_begin.
constexpr const char* kModeNames[] = {"recording", "alterfile",   "abort",      "busy",     "stall",
                                      "dirstall",  "abortcreate", "abortalter", "openioerr"};
/// The openioerr case's recording folder: one no earlier case records into, so it is absent when the case starts.
constexpr const char* kOpenIoErrFolder = "SAMPLES/RECORD";

/// The stream channel's latency in the abort-during-open cases and the openioerr case: long enough that the abort, or
/// the unmount, lands while the open is still waiting to run.
constexpr uint32_t kAbortOpenLatencyMs = 50;
/// Ticks given, after an abort-during-open case's cleanup finishes, for anything it left in flight to land -- far
/// past kAbortOpenLatencyMs, so a late open is on the card before the checks look.
constexpr int32_t kAbortOpenSettleTicks = 500;
/// The recorder's stream-write slots: a leaked handle leaves one fewer free.
constexpr int32_t kStreamWriteSlots = 8;

/// Whether @p path names a directory on the card. Synchronous: the scenario's own check, not the recorder's.
bool folderExists(const char* path) {
	DelugeHandle handle = DELUGE_HANDLE_NONE;
	if (deluge_efatfs_dir_open(path, &handle) != DELUGE_OK) {
		return false;
	}
	deluge_efatfs_dir_close(handle);
	return true;
}

/// How many of the recorder's stream-write slots are free, found by opening streams until one is refused. Every probe
/// stream is closed and its file removed before this returns.
int32_t freeStreamWriteSlots() {
	std::vector<deluge::io::Stream> held;
	std::vector<std::string> paths;
	for (int32_t i = 0; i < kStreamWriteSlots; i++) {
		std::string probePath = "RWSLOT" + std::to_string(i) + ".TMP";
		auto opened = deluge::io::Stream::open(probePath, DELUGE_STREAM_WRITE_CREATE);
		if (!opened) {
			break;
		}
		held.push_back(std::move(opened.value()));
		paths.push_back(std::move(probePath));
	}
	const auto free = static_cast<int32_t>(held.size());
	held.clear();
	for (const std::string& probePath : paths) {
		(void)deluge::io::unlink(probePath);
	}
	return free;
}

/// The stream channel's latency in the busy case: long enough that the recorder asks while the competing
/// operation is still in flight, and far inside the recorder's bound, so a refusal is asked again rather
/// than given up on.
constexpr uint32_t kBusyLatencyMs = 20;

/// The stream channel's latency once the stall case has had a cluster served: well past the recorder's bound
/// of 600 ticks at one scenario tick per virtual millisecond.
constexpr uint32_t kStallLatencyMs = 3000;

/// The directory channel's latency in the dirstall case: the recording-folder scan's open stays pending well past the
/// recorder's bound of 600 ticks, at one scenario tick per virtual millisecond.
constexpr uint32_t kDirStallLatencyMs = 3000;

/// The busy case's competing operation: a free-space query, asked again one tick after each answer. The
/// tick's gap is what lets the recorder in; without it the neighbour takes the channel the moment it frees
/// up and the recorder is refused until it gives up.
void driveNeighbour() {
	if (g_ticked.neighbourHeld) {
		uint32_t freeClusters = 0;
		if (deluge_file_stream_collect_stats(&freeClusters) == DELUGE_CHANNEL_PENDING) {
			return;
		}
		g_ticked.neighbourHeld = false;
		return; // Leave this tick's channel to the recorder.
	}
	g_ticked.neighbourHeld = deluge_file_stream_request_stats();
}

/// The stall case: slow the channel down once a cluster has been served, so the next cluster write is still
/// pending when the recorder reaches its bound. Latency is read when an operation starts, so the stalled
/// write keeps its 3000 ms after this puts the latency back.
void driveStall(const SampleRecorder& rec) {
	if (!g_ticked.stallArmed && SampleRecorder::writeChannelServedOps() > g_ticked.servedBefore) {
		deluge_file_stream_set_latency_ms(kStallLatencyMs);
		g_ticked.stallArmed = true;
	}
	if (rec.hadCardError) {
		if (rec.writePending()) {
			g_ticked.failedWhilePending = true;
		}
		deluge_file_stream_set_latency_ms(0);
	}
}

/// Report the busy or stall case once its recording is COMPLETE.
/// @return As deluge_scenario_ticked_tick.
int32_t finishGiveUpCase(SampleRecorder& rec) {
	if (g_ticked.neighbourHeld) {
		uint32_t freeClusters = 0;
		if (deluge_file_stream_collect_stats(&freeClusters) == DELUGE_CHANNEL_PENDING) {
			return 0; // Let the competing operation finish, so the next case starts on an idle channel.
		}
		g_ticked.neighbourHeld = false;
	}
	deluge_file_stream_set_latency_ms(0);
	const bool cardError = rec.hadCardError;
	const bool pendingAtEnd = rec.writePending();
	const bool fileOpen = rec.file.has_value();
	g_ticked.openError = rec.createOpenErrorForDiag();
	const std::string path = rec.filePathCreated;
	// Freed BEFORE the tier is read: a destructor left with a file to close is itself a violation.
	g_ticked.rec.reset();
	const int32_t tier = recorderTier() - g_ticked.tierBefore;
	const int32_t refusals = SampleRecorder::channelRefusals() - g_ticked.refusalsBefore;
	const int32_t served = SampleRecorder::writeChannelServedOps() - g_ticked.servedBefore;
	if (g_ticked.busy) {
		const bool readBack = !cardError && !path.empty()
		                      && verifyRecordedFile(path, /*channels=*/1, kTickedFrames, /*extraMargins=*/false);
		const bool ok = readBack && refusals > 0 && tier == 0;
		printf("RW-BUSY ok=%d readBack=%d refusals=%d served=%d tier=%d ticks=%d\n", (int)ok, (int)readBack,
		       (int)refusals, (int)served, (int)tier, g_ticked.ticks);
		fflush(nullptr);
		return ok ? 1 : 2;
	}
	if (g_ticked.openIoErr) {
		deluge_file_stream_set_mount_refused(false);
		(void)deluge_efatfs_mount();
		// A failed open is not worth a folder: only NOT_FOUND is. A retry on any failure makes the folder, then opens
		// on the card the byte channel mounted for it, and the recording succeeds.
		const bool folderAfter = folderExists(kOpenIoErrFolder);
		const bool ok = g_ticked.unmounted && !g_ticked.folderBefore && g_ticked.openError == DELUGE_ERR_NO_FILESYSTEM
		                && cardError && !folderAfter && !fileOpen && tier == 0;
		printf("RW-OPEN-IOERR ok=%d unmounted=%d folderBefore=%d openStatus=%d cardError=%d folderAfter=%d fileOpen=%d "
		       "tier=%d ticks=%d\n",
		       (int)ok, (int)g_ticked.unmounted, (int)g_ticked.folderBefore, (int)g_ticked.openError, (int)cardError,
		       (int)folderAfter, (int)fileOpen, (int)tier, g_ticked.ticks);
		fflush(nullptr);
		return ok ? 1 : 2;
	}
	if (g_ticked.dirStall) {
		deluge_file_directory_set_latency_ms(0);
		// The create failed at the path search, so there is no path, no file, and no synchronous open behind it.
		const bool ok = cardError && path.empty() && !fileOpen && tier == 0;
		printf("RW-DIR-STALL ok=%d cardError=%d path=%s fileOpen=%d tier=%d ticks=%d\n", (int)ok, (int)cardError,
		       path.empty() ? "-" : path.c_str(), (int)fileOpen, (int)tier, g_ticked.ticks);
		fflush(nullptr);
		return ok ? 1 : 2;
	}
	const bool ok = cardError && g_ticked.failedWhilePending && !pendingAtEnd && !fileOpen && tier == 0;
	printf("RW-STALL ok=%d cardError=%d failedWhilePending=%d pendingAtEnd=%d fileOpen=%d "
	       "served=%d tier=%d ticks=%d\n",
	       (int)ok, (int)cardError, (int)g_ticked.failedWhilePending, (int)pendingAtEnd, (int)fileOpen, (int)served,
	       (int)tier, g_ticked.ticks);
	fflush(nullptr);
	return ok ? 1 : 2;
}

} // namespace

extern "C" void deluge_scenario_ticked_begin(int32_t mode) {
	g_ticked.rec = std::make_unique<SampleRecorder>();
	g_ticked.ticks = 0;
	g_ticked.path.clear();
	g_ticked.started = false;
	g_ticked.alter = (mode == 1 || mode == 7);
	g_ticked.abortRun = (mode == 2);
	g_ticked.abortOpen = (mode == 6 || mode == 7);
	g_ticked.abortSent = false;
	g_ticked.settleTicks = 0;
	g_ticked.busy = (mode == 3);
	g_ticked.stall = (mode == 4);
	g_ticked.dirStall = (mode == 5);
	g_ticked.openIoErr = (mode == 8);
	g_ticked.unmounted = false;
	g_ticked.openError = DELUGE_OK;
	g_ticked.folderBefore = g_ticked.openIoErr && folderExists(kOpenIoErrFolder);
	g_ticked.neighbourHeld = false;
	g_ticked.stallArmed = false;
	g_ticked.failedWhilePending = false;
	g_ticked.servedBefore = SampleRecorder::writeChannelServedOps();
	g_ticked.refusalsBefore = SampleRecorder::channelRefusals();
	g_ticked.tierBefore = recorderTier();
	deluge_file_stream_set_latency_ms(g_ticked.busy                                ? kBusyLatencyMs
	                                  : (g_ticked.abortOpen || g_ticked.openIoErr) ? kAbortOpenLatencyMs
	                                                                               : 0);
	if (g_ticked.dirStall) {
		// The scan an earlier case already made is cached; force it again, and hold its open past the bound.
		audioFileManager.highestUsedAudioRecordingNumberNeedsReChecking.set(
		    static_cast<size_t>(AudioRecordingFolder::RESAMPLE));
		deluge_file_directory_set_latency_ms(kDirStallLatencyMs);
	}
	g_ticked.freeBefore = 0;
	{
		uint32_t total = 0;
		(void)deluge_efatfs_stats(&g_ticked.freeBefore, &total);
	}

	// stderr as well as stdout: this case runs AFTER the scenario's own fflush(nullptr), so a
	// stdout-only line can be lost at exit -- and a case whose output vanishes is a case nobody can
	// tell from one that never ran.
	const char* modeName = (mode >= 0 && mode <= 8) ? kModeNames[mode] : "unknown";
	printf("CASE ticked_%s (channels=%d frames=%u)\n", modeName, g_ticked.alter ? 2 : 1, kTickedFrames);
	fflush(nullptr);

	Error err = g_ticked.rec->setup(g_ticked.alter ? 2 : 1, AudioInputChannel::MIX, /*newKeepingReasons=*/false,
	                                /*shouldRecordExtraMargins=*/false,
	                                g_ticked.openIoErr ? AudioRecordingFolder::RECORD : AudioRecordingFolder::RESAMPLE,
	                                /*buttonPressLatency=*/0, /*outputRecordingFrom=*/nullptr);
	if (err != Error::NONE) {
		fprintf(stderr, "  FAIL: setup() returned error %d\n", static_cast<int>(err));
		g_ticked.rec.reset();
		return;
	}

	// Alteration case: permit it, and leave the right channel silent so inputHasNoRightChannel()
	// picks REMOVE_RIGHT_CHANNEL. The result is a MONO file carrying the left ramp, which is exactly
	// what the plain case's verification already checks for.
	g_ticked.rec->allowFileAlterationAfter = g_ticked.alter;

	std::vector<StereoSample> input(kTickedFrames);
	for (uint32_t i = 0; i < kTickedFrames; i++) {
		input[i].l = rampSample(i, 0);
		input[i].r = 0;
	}
	g_ticked.rec->feedAudio(std::span<StereoSample>(input));
	// The abort runs, and the create's open, end nothing here: they abort a recording still capturing.
	if (!g_ticked.abortRun && !(g_ticked.abortOpen && !g_ticked.alter)) {
		g_ticked.rec->endSyncedRecording(0);
	}
	// The abort run does NOT end the recording here. It lets a few ticks pass first so the file is
	// actually created and some clusters are on the card -- aborting before that leaves the machine
	// nothing to close or delete, which is the one shape that proves least.
	g_ticked.started = true;
}

extern "C" int32_t deluge_scenario_ticked_tick() {
	if (!g_ticked.started || !g_ticked.rec) {
		return 2; // failed to start
	}
	SampleRecorder& rec = *g_ticked.rec;

	if (g_ticked.abortOpen) {
		const char* name = g_ticked.alter ? "abortalter" : "abortcreate";
		if (!g_ticked.abortSent) {
			if (rec.openInFlightForDiag()) {
				printf("TK-OPEN-HELD case=%s ticks=%d\n", name, g_ticked.ticks);
				fflush(nullptr);
				rec.abort();
				g_ticked.abortSent = true;
				g_ticked.ticks = 0;
			}
			else if (++g_ticked.ticks > 20000) {
				fprintf(stderr, "  FAIL: %s never saw the channel opening the file\n", name);
				g_ticked.rec.reset();
				return 2;
			}
			(void)rec.cardRoutine();
			return 0;
		}
		const bool cleanupDone = rec.status.load(std::memory_order_acquire) == RecorderStatus::ABORTED
		                         && rec.abortPhaseForDiag() == kAbortPhaseDone;
		if (!cleanupDone) {
			if (++g_ticked.ticks > 20000) {
				fprintf(stderr, "  FAIL: %s cleanup never finished (phase=%d)\n", name, (int)rec.abortPhaseForDiag());
				g_ticked.rec.reset();
				return 2;
			}
			(void)rec.cardRoutine();
			return 0;
		}
		if (g_ticked.path.empty()) {
			g_ticked.path = rec.filePathCreated;
		}
		if (++g_ticked.settleTicks < kAbortOpenSettleTicks) {
			return 0;
		}
		deluge_file_stream_set_latency_ms(0);
		g_ticked.rec.reset();
		// Both checks read what the abort left once everything it issued has had time to land: an open it did not
		// wait out creates or reopens the file AFTER the unlink, and keeps a stream-write slot nobody will close.
		std::vector<std::byte> leftover;
		const bool gone = g_ticked.path.empty() || !readWholeFile(g_ticked.path, leftover);
		const int32_t freeSlots = freeStreamWriteSlots();
		const bool ok = gone && freeSlots == kStreamWriteSlots;
		printf("TK-ABORT-OPEN case=%s gone=%d freeSlots=%d of=%d ok=%d\n", name, (int)gone, freeSlots,
		       kStreamWriteSlots, (int)ok);
		fflush(nullptr);
		if (!gone) {
			fprintf(stderr, "  FAIL: %s left its file behind\n", name);
		}
		if (freeSlots != kStreamWriteSlots) {
			fprintf(stderr, "  FAIL: %s leaked a stream-write handle\n", name);
		}
		return ok ? 1 : 2;
	}

	if (g_ticked.abortRun) {
		if (rec.status.load(std::memory_order_acquire) == RecorderStatus::CAPTURING_DATA && g_ticked.ticks >= 8) {
			// File created, clusters written: now abandon it.
			rec.abort();
		}
		// Wait for the abort MACHINE to finish, not for AWAITING_DELETION: this recorder is held by
		// the harness, so `pointerHeldElsewhere` is set and the status legitimately stays ABORTED.
		// What is under test is that the close completed and the unlink then ran.
		const bool cleanupDone = rec.status.load(std::memory_order_acquire) == RecorderStatus::ABORTED
		                         && rec.abortPhaseForDiag() == kAbortPhaseDone;
		if (!cleanupDone) {
			if (++g_ticked.ticks > 2000) {
				fprintf(stderr, "  FAIL: abort cleanup never finished (status=%d phase=%d held=%d)\n",
				        (int)rec.status.load(std::memory_order_acquire), (int)rec.abortPhaseForDiag(),
				        (int)rec.pointerHeldElsewhere);
				g_ticked.rec.reset();
				return 2;
			}
			(void)rec.cardRoutine();
			return 0;
		}
		const std::string path = rec.filePathCreated;
		g_ticked.rec.reset();
		uint32_t freeAfter = 0;
		uint32_t total = 0;
		(void)deluge_efatfs_stats(&freeAfter, &total);
		// The clusters must come back. An abort that unlinks before its close has COMPLETED leaks the
		// chain, and no byte comparison can see that -- the file is gone either way.
		std::vector<std::byte> leftover;
		const bool gone = path.empty() || !readWholeFile(path, leftover);
		const bool noLeak = (freeAfter >= g_ticked.freeBefore);
		printf("TK-LEAK freeBefore=%u freeAfter=%u gone=%d ok=%d\n", g_ticked.freeBefore, freeAfter, (int)gone,
		       (int)(gone && noLeak));
		fflush(nullptr);
		if (!noLeak) {
			fprintf(stderr, "  FAIL: aborted recording leaked clusters (%u -> %u)\n", g_ticked.freeBefore, freeAfter);
		}
		if (!gone) {
			fprintf(stderr, "  FAIL: aborted recording left its file behind\n");
		}
		return (gone && noLeak) ? 1 : 2;
	}

	// The create's open is held by the channel's latency: unmount under it, and refuse the channel the mount, so the
	// open fails with NO_FILESYSTEM rather than NOT_FOUND.
	if (g_ticked.openIoErr && !g_ticked.unmounted && rec.openInFlightForDiag()) {
		deluge_file_stream_set_mount_refused(true);
		deluge_efatfs_unmount();
		g_ticked.unmounted = true;
	}
	// Before the recorder's own tick, so the neighbour asks first and the recorder is the one refused.
	if (g_ticked.busy) {
		driveNeighbour();
	}
	if (g_ticked.stall) {
		driveStall(rec);
	}

	if (rec.status.load(std::memory_order_acquire) == RecorderStatus::COMPLETE) {
		if (g_ticked.busy || g_ticked.stall || g_ticked.dirStall || g_ticked.openIoErr) {
			return finishGiveUpCase(rec);
		}
		const std::string path = rec.filePathCreated;
		g_ticked.rec.reset(); // closes the stream and releases the capture buffers
		if (path.empty()) {
			fprintf(stderr, "  FAIL: no file path recorded\n");
			return 2;
		}
		const bool ok = verifyRecordedFile(path, /*channels=*/1, kTickedFrames, /*extraMargins=*/false);
		// The evidence for THIS case, taken here rather than from RW-SUMMARY: that line is printed by
		// the oracle, which has already returned by the time this case runs, so it cannot
		// see anything the `ticked_` recording did.
		printf("TK-RESULT ok=%d served=%d channelTicks=%d channelFinalizeTicks=%d channelCreateTicks=%d ticks=%d\n",
		       (int)ok, (int)SampleRecorder::writeChannelServedOps(), (int)AudioEngine::recorderDrainTicks(),
		       (int)AudioEngine::finalizeTicks(), (int)AudioEngine::createTicks(), g_ticked.ticks);
		fflush(nullptr);
		return ok ? 1 : 2;
	}

	if (++g_ticked.ticks > 400000) {
		fprintf(stderr, "  FAIL: ticked recording never reached COMPLETE (%d ticks)\n", g_ticked.ticks);
		g_ticked.rec.reset();
		return 2;
	}

	// The real caller's entry, not a direct call into the write path: cardRoutine() runs here, and after
	// boot every card operation it starts goes through a channel.
	Error e = rec.cardRoutine();
	if (e != Error::NONE && e != Error::MAX_FILE_SIZE_REACHED) {
		fprintf(stderr, "  FAIL: ticked cardRoutine() returned error %d\n", static_cast<int>(e));
		g_ticked.rec.reset();
		return 2;
	}
	return 0;
}

extern "C" void deluge_scenario_ticked_report() {
	printf("RW-TIER violations=%d refusals=%d\n", (int)recorderTier(), (int)SampleRecorder::channelRefusals());
	fflush(nullptr);
}

extern "C" int32_t deluge_scenario_run_recorder_roundtrip() {
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

	std::vector<Case>& cases = g_caseList;
	cases.clear();
	g_caseCursor = 0;
	// Begin, not the default Done: the first tick must START case 0, not treat it as one that finished.
	g_case.step = CaseStep::Begin;

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

	// The cases are driven by deluge_scenario_recorder_cases_tick(), one step per call, so the caller
	// can let the storage driver run in between. That is what lets them work on the CHANNEL arm --
	// which is the arm they are on after boot.
	return 0;
}

/// Advance the case run by one step. 0 while cases remain, 1 when they are all done.
extern "C" int32_t deluge_scenario_recorder_cases_tick() {
	if (g_caseCursor >= g_caseList.size()) {
		return 1;
	}
	if (g_case.step == CaseStep::Begin || g_case.step == CaseStep::Done || g_case.step == CaseStep::Failed) {
		if (g_case.step != CaseStep::Begin) {
			// The previous case finished; count it and move on.
			if (g_case.step == CaseStep::Failed) {
				g_failures++;
			}
			g_caseCursor++;
			if (g_caseCursor >= g_caseList.size()) {
				printf("%d/%d cases passed\n", g_cases - g_failures, g_cases);
				return 1;
			}
		}
		if (!beginCase(g_caseList[g_caseCursor])) {
			g_case.step = CaseStep::Failed;
		}
		return 0;
	}
	(void)stepCase();
	return 0;
}

/// Start the finalized multi-cluster probe. Its pump is ticked by the caller below.
extern "C" void deluge_scenario_recorder_finalized_begin() {
	const uint32_t clusterSize = static_cast<uint32_t>(Cluster::size);
	const uint32_t headerLen = 44;
	const uint32_t avail = (3 * clusterSize > headerLen) ? (3 * clusterSize - headerLen) : 0;
	const uint32_t regressionFrames = avail / 3 + 400;
	g_cases++;
	printf("CASE finalized_multicluster_regression (channels=1 margins=0 frames=%u regionIndex=1)\n", regressionFrames);
	(void)deluge_harness_recorder_finalized_multicluster_probe(/*numChannels=*/1, regressionFrames,
	                                                           /*regionIndex=*/1);
}

/// One tick of that probe's pump. 0 = still going, non-zero = settled.
extern "C" int32_t deluge_scenario_recorder_finalized_tick() {
	return deluge_harness_recorder_finalized_multicluster_probe_tick();
}

namespace {

/// The oracle's tail, one step per call: the live-readback probe, then the finalized probe's acquire, then the
/// summary. Run one step per call rather than straight through, since a drain must never block the caller.
enum class RestStep : uint8_t {
	LiveDrain,     ///< ONE cardRoutine() per call on the still-recording probe, kLiveDrainTicks of them.
	LivePoll,      ///< Re-acquire, asking for a drain each time, while LOADING: it must never resolve.
	LiveSettle,    ///< The aborted probe's cardRoutine(), one per call, until the recorder has settled.
	FinalizedPoll, ///< Region 1 of the finalized recording, asking for a drain each call, until not LOADING.
	Done,
};
/// The one-call probe's drain: ten cardRoutine() ticks before the first acquire.
constexpr uint32_t kLiveDrainTicks = 10;
/// The one-call probe's watch: thirty-two drain-and-retry rounds, each a drain request and a yield.
constexpr uint32_t kLivePolls = 32;

struct RestRun {
	RestStep step = RestStep::Done;
	uint32_t ticks = 0;
	uint8_t liveState = 0;
	uint8_t finalizedState = 0;
};
RestRun g_rest;

void reportLive() {
	printf("recorder live-readback probe: state=%u (1=READY 2=LOADING 3=UNAVAILABLE 0=harness-error)\n",
	       g_rest.liveState);
	if (g_rest.liveState != DELUGE_REGION_LOADING) {
		fprintf(stderr, "  NOTE: still-recording live-readback probe did not resolve LOADING (state=%u)\n",
		        g_rest.liveState);
		g_failures++;
	}
}

void reportFinalizedAndSummary() {
	uint32_t tableClusters = deluge_harness_recorder_finalized_multicluster_probe_table_clusters();
	uint32_t expectedClusters = deluge_harness_recorder_finalized_multicluster_probe_expected_clusters();
	uint8_t bytesOk = deluge_harness_recorder_finalized_multicluster_probe_bytes_ok();
	printf("  table_clusters()=%u expected_clusters()=%u acquire_state=%u (1=READY) bytes_ok=%u\n", tableClusters,
	       expectedClusters, g_rest.finalizedState, bytesOk);
	deluge_harness_recorder_finalized_multicluster_probe_end();
	bool regressionOk =
	    (tableClusters != 0) && (tableClusters >= expectedClusters) && (g_rest.finalizedState == 1) && (bytesOk == 1);
	if (!regressionOk) {
		fprintf(stderr,
		        "  FAIL: finalized multi-cluster regression gate -- table_clusters=%u expected_clusters=%u "
		        "acquire_state=%u bytes_ok=%u\n",
		        tableClusters, expectedClusters, g_rest.finalizedState, bytesOk);
		g_failures++;
	}
	printf("  %s\n", regressionOk ? "PASS" : "FAIL");

	// The evidence this gate exists to show. `served` says the async arm executed; `channelTicks`
	// (AudioEngine::recorderDrainTicks()) says the drain ran after boot, with its writes on the write
	// channel. Served alone says writes were served, not which drain issued them.
	printf("RW-SUMMARY served=%d channelTicks=%d\n", (int)SampleRecorder::writeChannelServedOps(),
	       (int)AudioEngine::recorderDrainTicks());
	// A terminator, so a run that aborted part-way is a gate FAILURE rather than a silently
	// truncated log: a stage that never ran asserts nothing, and the failure count cannot show that.
	printf("RW-RESULT failures=%d\n", (int)g_failures);
	fflush(nullptr);
}

} // namespace

/// Start the oracle's tail. Probe live read-back of a still-recording sample: RecordingReadSource is gone, so it must
/// NOT resolve READY anywhere -- a still-recording Sample has no valid reader. The failed read is re-queued at lowest
/// priority rather than failed outright (the async fill task's fill_once, streaming_loader.rs), so the acquire is
/// expected to stay LOADING, not settle on UNAVAILABLE.
extern "C" void deluge_scenario_recorder_rest_begin() {
	g_rest = RestRun{};
	if (deluge_harness_recorder_probe(/*numChannels=*/1, /*numFrames=*/50) == 0) {
		g_rest.liveState = 0; // the harness error the report names
		reportLive();
		g_rest.step = RestStep::LiveSettle;
		return;
	}
	g_rest.step = RestStep::LiveDrain;
}

/// One step of the tail. 0 while it runs, 1 once RW-RESULT has printed.
///
/// After the live probe, the regression gate: a real, FINALIZED (COMPLETE) multi-cluster mono recording --
/// finalizeRecordedFile()'s no-alteration else-branch, the only branch AudioClip recording ever takes -- must leave the
/// overview cache sized to the real cluster count, and region index 1+ must resolve READY with correct bytes through
/// the same port real playback uses.
extern "C" int32_t deluge_scenario_recorder_rest_tick() {
	switch (g_rest.step) {
	case RestStep::LiveDrain:
		deluge_harness_recorder_probe_drain_tick();
		if (++g_rest.ticks >= kLiveDrainTicks) {
			g_rest.liveState = deluge_harness_recorder_probe_acquire();
			g_rest.ticks = 0;
			g_rest.step = RestStep::LivePoll;
		}
		return 0;
	case RestStep::LivePoll:
		if (g_rest.liveState == DELUGE_REGION_LOADING && g_rest.ticks < kLivePolls) {
			g_rest.ticks++;
			g_rest.liveState = deluge_harness_recorder_probe_poll();
			return 0;
		}
		reportLive();
		deluge_harness_recorder_probe_abort();
		g_rest.step = RestStep::LiveSettle;
		return 0;
	case RestStep::LiveSettle:
		if (deluge_harness_recorder_probe_settle_tick() == 0) {
			return 0;
		}
		deluge_harness_recorder_probe_end();
		g_rest.finalizedState = deluge_harness_recorder_finalized_multicluster_probe_finish(/*regionIndex=*/1);
		g_rest.step = RestStep::FinalizedPoll;
		return 0;
	case RestStep::FinalizedPoll:
		if (g_rest.finalizedState == DELUGE_REGION_LOADING) {
			g_rest.finalizedState = deluge_harness_recorder_finalized_multicluster_probe_poll();
			return 0;
		}
		reportFinalizedAndSummary();
		g_rest.step = RestStep::Done;
		return 1;
	case RestStep::Done:
		return 1;
	}
	return 1;
}

extern "C" int32_t deluge_scenario_recorder_roundtrip_failures() {
	return g_failures;
}
