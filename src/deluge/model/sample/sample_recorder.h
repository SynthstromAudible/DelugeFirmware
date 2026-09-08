/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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
#include "dsp/envelope_follower/absolute_value.h"
#include "dsp/stereo_sample.h"
#include "io/stream.hpp"
#include "memory/fast_allocator.h"
#include "util/segmented_vector.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <gsl/gsl>
#include <optional>
#include <span>
#include <string>

enum class MonitoringAction {
	NONE = 0,
	REMOVE_RIGHT_CHANNEL = 1,
	SUBTRACT_RIGHT_CHANNEL = 2,
};

enum class RecorderStatus {
	CAPTURING_DATA = 0,
	CAPTURING_DATA_WAITING_TO_STOP = 1,
	FINISHED_CAPTURING_BUT_STILL_WRITING = 2,
	COMPLETE = 3,

	// Means RAM error only. SD errors are noted separately and won't affect operation, as long as RAM lasts
	ABORTED = 4,
	AWAITING_DELETION = 5,
};

class Sample;
class AudioClip;
class Output;
struct RecorderConfig {
	bool neverUseThreshold = false;
};

class SampleRecorder {
public:
	SampleRecorder() = default;
	~SampleRecorder();
	Error setup(int32_t newNumChannels, AudioInputChannel newMode, bool newKeepingReasons,
	            bool shouldRecordExtraMargins, AudioRecordingFolder newFolderID, int32_t buttonPressLatency,
	            Output* outputRecordingFrom, RecorderConfig config = {});
	void setRecordingThreshold(RecorderConfig config);
	void feedAudio(std::span<StereoSample> input, bool applyGain = false, uint8_t gainToApply = 5);
	Error cardRoutine();
	void endSyncedRecording(int32_t buttonLatencyForTempolessRecording);
	/// @brief Post-capture WAV alteration: downmix/normalize the recorded audio in place on disk.
	///
	/// Public (rather than an internal helper finalizeRecordedFile() alone calls) so the host-sim
	/// byte-exact characterization harness (host_recorder_roundtrip_main.cpp) can drive this
	/// post-capture file transform directly, with an explicit action/lshiftAmount/geometry, bypassing
	/// finalizeRecordedFile()'s auto-detection heuristics -- SUBTRACT_RIGHT_CHANNEL in particular needs
	/// a plugged-in line-input jack the host sim can't simulate.
	/// @param action                    Channel-combining/removal operation to apply.
	/// @param lshiftAmount              Normalization left-shift applied to each sample.
	/// @param idealFileSizeBeforeAction Total file size (bytes) before this alteration.
	/// @param dataLengthAfterAction     Audio data length (bytes) expected after this alteration.
	/// @return Error::NONE on success.
	Error alterFile(MonitoringAction action, int32_t lshiftAmount, uint32_t idealFileSizeBeforeAction,
	                uint64_t dataLengthAfterAction);
	bool inputLooksDifferential();
	bool inputHasNoRightChannel();
	void removeFromOutput() {
		if (status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
			abort();
		}
		outputRecordingFrom = nullptr;
	};
	void abort();

	SampleRecorder* next{};

	gsl::owner<Sample*> sample{};

	int32_t numSamplesToRunBeforeBeginningCapturing{};
	uint32_t numSamplesBeenRunning{};
	uint32_t numSamplesCaptured{};

	uint32_t numSamplesExtraToCaptureAtEndSyncingWise{};

	int32_t firstUnwrittenClusterIndex = 0;

	/// The cluster index whose write is in flight on the channel, or -1 when none is.
	///
	/// Its buffer stays in `bufferTable_` and must NOT be recycled or freed until the collect is
	/// terminal -- the channel holds a raw pointer into it rather than a copy. `cardRoutine()`
	/// settles this before anything else runs, which is the whole of the buffer-lifetime contract:
	/// every path through this recorder passes through there, including the abort path, whose
	/// `detachSample()` frees capture buffers well before the stream is closed.
	int32_t inFlightClusterIndex_ = -1;
	/// Byte count of the write in flight, so the collect can tell a complete write from a short one
	/// without the caller having to remember what it asked for.
	uint32_t inFlightNumBytes_ = 0;
	/// Consecutive ticks the in-flight write has read PENDING. Bounded by
	/// `deluge::storage::kMaxPendingTicks`; see `recorder_write_policy.h` for why a stall bound is
	/// not optional.
	int32_t writePendingTicks_ = 0;
	/// Set once this recorder gives up on the channel (a stall, or a request the channel never
	/// keyed). Latched for the rest of the recording rather than retried: a driver that stopped
	/// answering once will keep costing a full stall bound per cluster, and the synchronous arm is
	/// always correct.
	bool channelAbandoned_ = false;

	/// @brief Index of the cluster currently being written to. Starts at -1 so a recorder destructed
	///        before any recording began is still in a valid state.
	///
	/// @note Atomic: the producer (audio) publishes a completed buffer via a release store at
	///       createNextCluster; the consumer (fiber) reads it acquire as its drain bound -- the index
	///       into our own private bufferTable_ (see below), not a shared residency table. See
	///       docs/dev/known-concurrency-bugs.md (B3).
	std::atomic<int32_t> currentRecordClusterIndex = -1;

	uint32_t audioFileNumber{};
	AudioRecordingFolder folderID;

	char* writePos{};
	char* clusterEndPos{};

	// When this gets set, we add the Sample to the master list. This is stored here in addition to in the Sample,
	// so we can delete an aborted file even after the Sample has been detached / destructed.
	// This will be the temp file path if there is one.
	std::string filePathCreated{};

	/// @brief This recorder's current lifecycle state; see RecorderStatus.
	///
	/// @note Atomic: the finishCapturing (audio) -> ABORTED (either thread) transitions are release
	///       stores; the fiber's cardRoutine() decision reads are acquire loads, so seeing
	///       FINISHED_CAPTURING_BUT_STILL_WRITING (or ABORTED) also makes visible the producer's final
	///       currentRecordClusterIndex/payload writes before the fiber takes over as producer in
	///       finalizeRecordedFile(). See docs/dev/known-concurrency-bugs.md (B3).
	std::atomic<RecorderStatus> status = RecorderStatus::CAPTURING_DATA;
	static_assert(std::atomic<RecorderStatus>::is_always_lock_free);
	AudioInputChannel mode;
	Output* outputRecordingFrom{}; // for when recording from a specific output

	// Need to keep track of this, so we know whether to remove it. Well I guess we could just look and see if it's
	// there... but this is nice.
	bool haveAddedSampleToArray = false;

	bool allowFileAlterationAfter = false;
	bool allowNormalization = true;
	bool autoDeleteWhenDone = false;
	bool keepingReasonsForFirstClusters{};
	uint8_t recordingNumChannels{};
	bool hadCardError = false;
	bool reachedMaxFileSize = false;
	bool recordingExtraMargins = false;
	bool pointerHeldElsewhere = false;
	bool capturedTooMuch = false;
	bool thresholdRecording = false;
	uint32_t minThresholdMargin = 0;

	// Most of these are not captured in the case of BALANCED input for AudioClips
	bool recordingClippedRecently{};
	int32_t recordPeakL{};
	int32_t recordPeakR{};
	int32_t recordPeakLMinusR{};
	uint64_t recordSumL{};
	uint64_t recordSumR{};
	uint64_t recordSumLPlusR{};  // L and R are halved before these two are calculated
	uint64_t recordSumLMinusR{}; // --------

	int32_t recordMax{};
	int32_t recordMin{};

	uint32_t audioDataLengthBytesAsWrittenToFile{};
	uint32_t loopEndSampleAsWrittenToFile{};

	float startValueThreshold{};

	int32_t* sourcePos{};

	/// @warning A start-here for `highsiderr`-adjacent audio bugs. When this member's type last
	/// changed, SampleRecorder's object size and allocation timing changed with it, and one
	/// `highsiderr` TRACK-mode golden fixture showed small-magnitude PCM differences against an
	/// A/B (same file size and WAV structure, converging back to identical near the end). The
	/// write-dispatch logic was confirmed structurally identical across that change, so the
	/// differences were attributed to the layout/allocation-timing shift interacting with this
	/// fixture's documented fragility under unrelated code-shape changes, and knowingly accepted
	/// rather than bisected further -- the proposed isolation test (padding SampleRecorder back to
	/// its old size) was never run. `highsiderr` is separately known to flip on ANY codegen change,
	/// which is consistent with that reading but does not confirm it.
	std::optional<deluge::io::Stream> file;

public:
	/// @brief Whether a cluster write is in flight on the channel right now.
	[[nodiscard]] bool writePending() const { return inFlightClusterIndex_ >= 0; }

	/// @brief How many cluster writes have gone through the channel, process-wide.
	///
	/// The difference between the channel being used and the channel merely being compiled. Every
	/// fallback in this path is correct and silent, so a channel that never worked would leave every
	/// gate green and every recording intact.
	[[nodiscard]] static int32_t writeChannelServedOps();

private:
private:
	Error writeCluster(int32_t clusterIndex, size_t numBytes);
	/// @brief Settle the in-flight cluster write, if there is one.
	///
	/// Call as the FIRST thing in `cardRoutine()`. Nothing that could free a capture buffer or close
	/// the stream may run while a write is outstanding.
	/// @return true when the caller must return immediately -- the write has not landed yet.
	bool pumpInFlightWrite();
	/// @brief Write a cluster on the storage owner, blocking. The arm that shipped before the
	///        channel existed, and the fallback every refusal and stall returns to.
	Error writeClusterSynchronously(int32_t clusterIndex, size_t numBytes);
	/// @brief Recycle a written cluster's buffer and clear its table slot.
	void finishCluster(int32_t clusterIndex);
	Error finalizeRecordedFile();
	Error createNextCluster();
	Error writeAnyCompletedClusters();
	void finishCapturing();
	void updateDataLengthInHeader(std::span<std::byte> headerBuf);
	void totalSampleLengthNowKnown(uint32_t totalLength, uint32_t loopEndPointSamples = 0);
	void detachSample();
	Error truncateFileDownToSize(uint32_t newFileSize);
	Error writeOneCompletedCluster();

	/// @brief Minimal fixed-capacity lock-free SPSC ring of recycled buffer pointers.
	///
	/// Private to SampleRecorder rather than deluge::util::SpscRing -- that header declares `namespace
	/// deluge::util`, which collides with this codebase's separate top-level `namespace util`
	/// (util/misc.h) wherever both become visible in the same translation unit, and sample_recorder.h
	/// is included far too widely to risk that.
	/// @note Same release/acquire discipline as deluge::util::SpscRing (see that header's doc for the
	///       full reasoning): the producer (fiber, recycleBuffer()) release-stores tail_ after writing
	///       a slot; the consumer (audio thread, allocateBuffer()) acquire-loads tail_ before reading a
	///       slot and release-stores head_ after freeing it, which the producer acquire-loads to know
	///       the slot is free again.
	class RecycleRing {
	public:
		static constexpr std::size_t kCapacity = 16; // must be a power of two

		/// @brief Push @p value onto the ring.
		/// @param value The buffer pointer to recycle.
		/// @return True on success; false if the ring is full.
		[[nodiscard]] bool try_push(std::byte* value) {
			const std::size_t tail = tail_.load(std::memory_order_relaxed);
			const std::size_t head = head_.load(std::memory_order_acquire);
			if (tail - head == kCapacity) {
				return false; // full
			}
			slots_[tail & kMask] = value;
			tail_.store(tail + 1, std::memory_order_release);
			return true;
		}

		/// @brief Pop the oldest recycled buffer off the ring.
		/// @param out Set to the popped buffer pointer on success; left untouched on failure.
		/// @return True on success; false if the ring is empty.
		[[nodiscard]] bool try_pop(std::byte*& out) {
			const std::size_t head = head_.load(std::memory_order_relaxed);
			const std::size_t tail = tail_.load(std::memory_order_acquire);
			if (head == tail) {
				return false; // empty
			}
			out = slots_[head & kMask];
			head_.store(head + 1, std::memory_order_release);
			return true;
		}

	private:
		static constexpr std::size_t kMask = kCapacity - 1;
		std::array<std::byte*, kCapacity> slots_{};
		std::atomic<std::size_t> head_{0};
		std::atomic<std::size_t> tail_{0};
	};

	/// @brief The recorder's own private capture buffers -- Cluster::size (+ trailing overshoot slack)
	///        plain allocations, never registered with the shared residency table.
	///
	/// Maps a buffer's logical index (the same index space `currentRecordClusterIndex`/
	/// `firstUnwrittenClusterIndex` already walk) to its physical storage.
	/// @note Growth (audio-thread-only, single-writer) is pre-`reserve`d up front in setup() so it
	///       never reallocates the segment-pointer index concurrently with the fiber's `operator[]`
	///       reads -- same B2 discipline the shared table used to require.
	deluge::SegmentedVector<std::byte*, 256, deluge::memory::fast_allocator> bufferTable_{};

	/// @brief Recycles a flushed buffer's memory back for reuse: the fiber pushes after a successful
	///        write_at, the audio thread pops when it needs a fresh buffer.
	///
	/// @note Either side of this ring can "miss" without correctness cost -- a full push just frees
	///       the buffer outright, an empty pop just allocates fresh -- so buffer *reuse* is
	///       best-effort while buffer *availability* (the audio thread never blocks/drops) is
	///       unconditional.
	RecycleRing freeBuffers_{};
	std::byte* currentRecordBuffer = nullptr; ///< the buffer at bufferTable_[currentRecordClusterIndex]

	[[nodiscard]] std::byte* allocateBuffer();
	void recycleBuffer(std::byte* buffer);
	void releaseCaptureBuffers();

	AbsValueFollower envelopeFollower{};
};
