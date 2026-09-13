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

#include "model/sample/sample_recorder.h"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/root_ui.h"
#include "gui/ui_timer_manager.h"
#include "io/file.hpp"
#include "io/stream.hpp"
#include "libdeluge/control_surface.h"
#include "libdeluge/file_io.h"
#include "libdeluge/file_write.h"
#include "libdeluge/stream_io.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/sample/recorder_write_policy.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/stem_export/stem_export.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/byte_channel.h"
#include "storage/cluster/cluster.h"
#include "storage/owner.h"
#include "util/exceptions.h"
#include "util/fixedpoint.h"
#include "util/functions.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <new>

static_assert(std::atomic<int32_t>::is_always_lock_free,
              "recorder hand-off relies on a lock-free atomic index on this target");

#define MAX_FILE_SIZE_MAGNITUDE 32

namespace {
/// How many of the recorder's channel requests were refused, process-wide. See
/// SampleRecorder::channelRefusals.
int32_t g_channelRefusals = 0;

/// Count one synchronous card operation by the recorder. Off the storage owner after boot that is a
/// violation; see deluge::storage::bytes::noteIoTier.
void noteRecorderIo() {
	deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Recorder);
}
} // namespace

SampleRecorder::~SampleRecorder() {
	D_PRINTLN("~SampleRecorder()");
	if (file) {
		// The stream's destructor closes it synchronously. A recorder freed with its file still open skipped
		// its own close.
		noteRecorderIo();
	}
	if (sample != nullptr) {
		detachSample();
	}
	// Only SPECIFIC_OUTPUT recorders track a source Output; MIX/OFFLINE_OUTPUT stem
	// recorders leave outputRecordingFrom null. The deref was benign on the MCU (addr 0
	// mapped) but faults on the host.
	if (outputRecordingFrom != nullptr) {
		outputRecordingFrom->removeRecorder();
	}
}

// This can be called when this SampleRecorder is destructed routinely - or earlier if we've aborted and the sample file
// is being deleted IMPORTANT!!!! You have to set sample to NULL after calling this, if not destructing
void SampleRecorder::detachSample() {

	// Our capture buffers are privately owned (never registered with the shared residency
	// table), so there are no "reasons"/leases to drop here -- just free whatever we still hold:
	// any undrained buffers (an abort mid-recording) plus anything sitting in the recycle ring.
	releaseCaptureBuffers();

	sample->removeReason("E400");
}

// Frees every capture buffer we still own -- undrained bufferTable_ entries (firstUnwrittenClusterIndex
// through the last assigned index, which covers a live currentRecordBuffer too, since it's always the
// last entry assigned) plus anything sitting in the recycle ring. Called from detachSample(), which by
// its own contract only ever runs once the fiber is done touching this recorder (status has reached
// ABORTED or >= COMPLETE) -- so this is single-threaded, no concurrent push/pop to race.
void SampleRecorder::releaseCaptureBuffers() {
	for (int32_t i = firstUnwrittenClusterIndex; static_cast<size_t>(i) < bufferTable_.size(); i++) {
		std::byte* buffer = bufferTable_[i];
		if (buffer != nullptr) {
			delugeDealloc(buffer);
		}
	}

	std::byte* recycled = nullptr;
	while (freeBuffers_.try_pop(recycled)) {
		delugeDealloc(recycled);
	}
}

// [audio thread] Get a capture buffer -- a recycled one if the fiber's returned any, otherwise
// grow by allocating fresh. Either way this never blocks and never fails silently by dropping audio;
// the only failure mode is genuine RAM exhaustion (nullptr), same as the shared-table allocation this
// replaces. Sized Cluster::size plus a few trailing bytes of overshoot slack -- a sample frame can
// straddle the boundary between one buffer and the next (see createNextCluster()'s overshoot copy).
std::byte* SampleRecorder::allocateBuffer() {
	std::byte* recycled = nullptr;
	if (freeBuffers_.try_pop(recycled)) {
		return recycled;
	}
	return static_cast<std::byte*>(deluge::memory::alloc_external(Cluster::size + kTrailingSlackBytes, 16));
}

// [fiber] Return a flushed buffer's memory for reuse. Best-effort -- if the recycle ring is
// full (the audio thread has fallen far behind draining it, unlikely given its small capacity here
// only bounds *reuse*, not availability), just free the buffer outright; the audio thread will
// allocate fresh next time it needs one. Either way, no audio data is at risk: this only runs after
// the buffer's contents are already safely on disk.
void SampleRecorder::recycleBuffer(std::byte* buffer) {
	if (!freeBuffers_.try_push(buffer)) {
		delugeDealloc(buffer);
	}
}

// config stuff
Error SampleRecorder::setup(int32_t newNumChannels, AudioInputChannel newMode, bool newKeepingReasons,
                            bool shouldRecordExtraMargins, AudioRecordingFolder newFolderID, int32_t buttonPressLatency,
                            Output* outputRecordingFrom_, RecorderConfig config) {

	outputRecordingFrom = outputRecordingFrom_;
	// The recorder no longer touches the shared residency table at all, so this no longer gates any
	// shared-cluster "reason" bookkeeping -- kept/stored for future watermark-based read-bound wiring
	// (a still-recording AudioClip's live-loop monitor wants its first few buffers to stay quickly
	// available).
	keepingReasonsForFirstClusters = newKeepingReasons;
	recordingExtraMargins = shouldRecordExtraMargins;
	folderID = newFolderID;

	// Didn't seem to make a difference forcing this into local RAM
	void* sample_memory = deluge::memory::alloc_external(sizeof(Sample), 16);
	if (sample_memory == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}

	sample = new (sample_memory) Sample;

	// Reserve our own buffer table's segment-pointer index to the max recording size up front
	// (single-threaded, before any concurrent audio-thread growth in createNextCluster), so that
	// growth never reallocates the index under the fiber's concurrent operator[] reads (B2: the
	// SegmentedVector keeps element addresses stable, but its pointer index must be pre-reserved
	// to stay stable under concurrent growth). maxClusters is derived from the runtime cluster size,
	// so this imposes no recording-length limit beyond the existing MAX_FILE_SIZE cap.
	bufferTable_.reserve(1 << (MAX_FILE_SIZE_MAGNITUDE - Cluster::size_magnitude));

	audioFileManager.adoptAudioFileObject(sample); // resource-manager evictable object (before addReason)
	sample->addReason(); // Must call this so it's protected from stealing, before we call initialize().
	Error error = sample->initialize(1);
	if (error != Error::NONE) {
gotError:
		audioFileManager.destroyAudioFileObject(*sample); // ~Sample + free (routed through the manager if adopted)
		return error;
	}

	currentRecordBuffer = allocateBuffer();
	if (!currentRecordBuffer) {
		error = Error::INSUFFICIENT_RAM;
		goto gotError;
	}
	try {
		bufferTable_.resize(1);
	} catch (deluge::exception&) {
		delugeDealloc(currentRecordBuffer);
		currentRecordBuffer = nullptr;
		error = Error::INSUFFICIENT_RAM;
		goto gotError;
	}
	bufferTable_[0] = currentRecordBuffer;

	// Give the sample some stuff
	sample->audioDataStartPosBytes = recordingExtraMargins ? 112 : 44;
	sample->byteDepth = 3;
	sample->numChannels = newNumChannels;
	sample->lengthInSamples = Sample::kUnknownLengthSentinel;
	sample->audioDataLengthBytes = Sample::kUnknownLengthSentinel; // If you ever change this value, update
	                                                               // Sample::kUnknownLengthSentinel and its users
	sample->sampleRate = kSampleRate;
	sample->workOutBitMask();

	pointerHeldElsewhere = true;
	mode = newMode;
	currentRecordClusterIndex.store(0, std::memory_order_relaxed);

	numSamplesToRunBeforeBeginningCapturing = numSamplesExtraToCaptureAtEndSyncingWise =
	    (mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) ? kAudioRecordLagCompensation : 0;

	// Apart from the MIX option, all other audio sources are fed to us during the "outputting" routine. Occasionally,
	// there'll be some more of that going to happen for the previous render, so we have to compensate for that
	if (mode != AudioInputChannel::MIX) {
		numSamplesToRunBeforeBeginningCapturing += AudioEngine::getNumSamplesLeftToOutputFromPreviousRender();
	}

	// External sources
	if (mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {

		sourcePos = (int32_t*)AudioEngine::inputRingPos;

		numSamplesToRunBeforeBeginningCapturing -=
		    buttonPressLatency; // Compensate for button press latency. We only do this for external sources

		// If doing extra margins...
		if (recordingExtraMargins) {

			// Everything will be fine, so long as the button press latency we compensated for isn't, like, as big as
			// the RX buffer
			sample->fileLoopStartSamples =
			    SSI_RX_BUFFER_NUM_SAMPLES - (SSI_TX_BUFFER_NUM_SAMPLES << 1) + numSamplesToRunBeforeBeginningCapturing;
			numSamplesToRunBeforeBeginningCapturing = 0;

			sourcePos += (SSI_TX_BUFFER_NUM_SAMPLES
			              << (NUM_MONO_INPUT_CHANNELS_MAGNITUDE + 1)); // I think the +1 was just because it needs to
			                                                           // move two tx buffers' length for some reason...
			if (sourcePos >= AudioEngine::inputRingEnd()) {
				sourcePos -= SSI_RX_BUFFER_NUM_SAMPLES << NUM_MONO_INPUT_CHANNELS_MAGNITUDE;
			}
		}

		// Or if not doing extra margins...
		else {

			// If the button press latency we're compensating for is more than the audio latency, we have to adjust
			// stuff, to grab some audio from just back in time
			if (numSamplesToRunBeforeBeginningCapturing < 0) {

				sourcePos +=
				    numSamplesToRunBeforeBeginningCapturing * NUM_MONO_INPUT_CHANNELS; // This might be negative!
				if (sourcePos < AudioEngine::inputRingStart()) {
					sourcePos += SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
				}

				numSamplesToRunBeforeBeginningCapturing = 0;
			}
		}
	}

	// Set some other stuff up

	recordPeakL = recordPeakR = recordPeakLMinusR = 0;
	recordingClippedRecently = false;

	recordSumL = 0;
	recordSumR = 0;
	recordSumLPlusR = 0;
	recordSumLMinusR = 0;

	recordMax = -2147483648;
	recordMin = 2147483647;

	writePos = reinterpret_cast<char*>(currentRecordBuffer);
	clusterEndPos = reinterpret_cast<char*>(currentRecordBuffer + Cluster::size);

	numSamplesBeenRunning = 0;
	numSamplesCaptured = 0;

	capturedTooMuch = false;

	recordingNumChannels = newNumChannels;
	int32_t byteDepth = 3;
	int32_t lengthSec =
	    5; // Mark it as 5 seconds long initially. We'll update that later when we know how long it actually is
	int32_t lengthSamples = lengthSec * sample->sampleRate;
	audioDataLengthBytesAsWrittenToFile = lengthSamples * 3 * recordingNumChannels;

	setRecordingThreshold(config);

	// Riff chunk -------------------------------------------------------
	writeInt32(&writePos, 0x46464952);                                                               // "RIFF"
	writeInt32(&writePos, audioDataLengthBytesAsWrittenToFile + sample->audioDataStartPosBytes - 8); // Chunk size
	writeInt32(&writePos, 0x45564157);                                                               // "WAVE"

	// Format chunk --------------------------------------------------------
	writeInt32(&writePos, 0x20746d66);                                            // "fmt "
	writeInt32(&writePos, 16);                                                    // Chunk size
	writeInt16(&writePos, 0x0001);                                                // Format - PCM
	writeInt16(&writePos, recordingNumChannels);                                  // Num channels
	writeInt32(&writePos, sample->sampleRate);                                    // Sample rate
	writeInt32(&writePos, sample->sampleRate * recordingNumChannels * byteDepth); // Data rate
	writeInt16(&writePos, recordingNumChannels * byteDepth);                      // Data block size
	writeInt16(&writePos, byteDepth * 8);                                         // Bits per sample

	if (recordingExtraMargins) {

		loopEndSampleAsWrittenToFile = lengthSamples;

		// Sample chunk ------------------------------------------------------
		writeInt32(&writePos, 0x6c706d73); // "smpl"
		writeInt32(&writePos, 60);         // Chunk size
		writeInt32(&writePos, 0);          // Manufacturer - 0 means none
		writeInt32(&writePos, 0);          // Product - 0 means none
		writeInt32(&writePos, (1000000000 + (sample->sampleRate >> 1)) / sample->sampleRate); // Nanoseconds per sample
		writeInt32(&writePos, 0); // MIDI note - 0 conventionally seems to mean none
		writeInt32(&writePos, 0); // MIDI pitch fraction
		writeInt32(&writePos, 0); // SMPTE format - 0 means none / no offset
		writeInt32(&writePos, 0); // SMPTE offset
		writeInt32(&writePos, 1); // Number of loops
		writeInt32(&writePos, 0); // Number of additional sampler data bytes

		// Loop definition ----------------------------------------------------
		writeInt32(&writePos, 0);                            // Cue point ID
		writeInt32(&writePos, 0);                            // Type - 0 means loop forward
		writeInt32(&writePos, sample->fileLoopStartSamples); // Start point
		writeInt32(&writePos, loopEndSampleAsWrittenToFile); // End point
		writeInt32(&writePos, 0);                            // Loop point sample fraction
		writeInt32(&writePos, 0);                            // Play count - 0 means continuous
	}

	// Data chunk ------------------------------------------------------
	writeInt32(&writePos, 0x61746164);                          // "data"
	writeInt32(&writePos, audioDataLengthBytesAsWrittenToFile); // Chunk size

	return Error::NONE;
}

void SampleRecorder::setRecordingThreshold(RecorderConfig config) {
	if (config.neverUseThreshold) {
		D_PRINTLN("ignoring threshold");
	}
	else {
		D_PRINTLN("following settings");
	}
	// don't use threshold recording if we're resampling internal input
	if (config.neverUseThreshold || currentSong->thresholdRecordingMode == ThresholdRecordingMode::OFF
	    || mode >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {
		startValueThreshold = 0;
		thresholdRecording = false;
		minThresholdMargin = 0;
	}
	else {
		// max possible sample value = 1 << kBitDepth = 16777216

		// these thresholds were determined through iterative testing to find
		// the right sample value to start recording audio input based on different input
		// levels

		switch (currentSong->thresholdRecordingMode) {
		// used for input sources with low input volume
		case ThresholdRecordingMode::LOW:
			startValueThreshold = 11.5;
			break;

		// used for input sources with medium-high input volume
		case ThresholdRecordingMode::MEDIUM:
			startValueThreshold = 14.5;
			break;

		// used for input sources with high input volume (good for microphones with gain)
		case ThresholdRecordingMode::HIGH:
			startValueThreshold = 17.5;
			break;

		default:
			break;
		}

		thresholdRecording = true;
		minThresholdMargin = std::max<uint32_t>(sample->fileLoopStartSamples, 256);
	}
}

// Beware! This could get called during card routine - e.g. if user stopped playback. So we'll just store a changed
// status, then do the descrutcion and file deletion when we know we're out of the card routine. Also, this gets called
// in audio routine! So don't do anything drastic.
void SampleRecorder::abort() {
	// RELEASE: abort() is callable cross-thread (audio or fiber); pairs with the acquire loads in
	// cardRoutine()/finalizeRecordedFile() so a fiber that observes ABORTED also observes any writes
	// that preceded this call on whichever thread invoked it.
	status.store(RecorderStatus::ABORTED, std::memory_order_release); // Note: it may already equal this!
}

// Returns error if one occurred just now - not if one was already noted before
Error SampleRecorder::cardRoutine() {

	// Settle any in-flight cluster write FIRST. The channel holds a raw pointer into
	// `bufferTable_`, and almost everything below can invalidate it: the abort path calls
	// detachSample() -> releaseCaptureBuffers(), which frees capture buffers well before the stream
	// is closed; finalize closes the stream outright. Every path through this recorder passes
	// through here, so settling here is the whole of the buffer-lifetime contract -- there is no
	// second place that needs to know about it.
	if (pumpInFlightWrite()) {
		return Error::NONE; // Still outstanding; come back next tick.
	}

	// No dispatch to the owner any more. Creating the file was the last tick that blocked, and it is
	// a machine now like the rest -- so the escape hatch that used to carry those ticks has nothing
	// left to carry and is gone with them.

	// If aborted, delete the file.
	// ACQUIRE: synchronizes-with abort()'s release store.
	if (status.load(std::memory_order_acquire) == RecorderStatus::ABORTED) {

aborted:
		// The cleanup spans ticks now: closing the stream and deleting the file are both requests,
		// and the close must be COMPLETE before the unlink goes out. Everything below lives in
		// advanceAbort().
		if (advanceAbort()) {
			return Error::NONE; // Still working; come back next tick.
		}

		// Normally we now just await deletion - except if a pointer is still being held elsewhere - which I think can
		// only happen from AudioRecorder. Or if the abort comes from a failure within this class and the AudioClip
		// hasn't realised yet?
		if (!pointerHeldElsewhere) {
			// RELAXED: fiber-owned write; no other thread reads AWAITING_DELETION as a hand-off signal.
			status.store(RecorderStatus::AWAITING_DELETION, std::memory_order_relaxed);
		}
		return Error::NONE;
	}

	// ACQUIRE: cardRoutine() decision read.
	if (status.load(std::memory_order_acquire) >= RecorderStatus::COMPLETE) {
		return Error::NONE;
	}

	Error error = Error::NONE;

	if (!hadCardError) {

		// If file not created yet, do that. Four phases now rather than a goto web; see
		// advanceCreate(). It returns Pending until the file exists, so the tick simply comes back.
		if (filePathCreated.empty() || createPhase_ != deluge::storage::CreatePhase::Done) {
			switch (advanceCreate()) {
			case CreateStep::Pending:
				return Error::NONE; // Come back next tick.
			case CreateStep::Failed:
				filePathCreated.clear();
				goto gotError;
			case CreateStep::Done:
				break;
			}

			// ACQUIRE: cardRoutine() decision read.
			if (status.load(std::memory_order_acquire) == RecorderStatus::ABORTED) {
				goto aborted; // In case aborted during
			}

			// Ok, the Sample still exists.
			sample->filePath = createFilePath_;                     // Can't fail!
			sample->tempFilePathForRecording = createTempFilePath_; // Can't fail!

			auto inserted = audioFileManager.audioFiles.insertElement(sample);
			if (!inserted) {
				error = inserted.error();
				goto gotError;
			}

			haveAddedSampleToArray = true;
		}

		// Might want to write just one cluster
		if (firstUnwrittenClusterIndex < currentRecordClusterIndex.load(std::memory_order_acquire)) {
			error = writeOneCompletedCluster(/*mayUseChannel=*/true);

			if (error != Error::NONE) {
gotError:
				hadCardError = true;
			}

			else {
				// If more clusters still to write, come back later to do them
				if (true || firstUnwrittenClusterIndex < currentRecordClusterIndex.load(std::memory_order_relaxed)) {
					goto allDoneForNow;
				}
			}
		}
	}

	// If we've actually finished recording...
	// ACQUIRE: synchronizes-with finishCapturing()'s release store, publishing the audio thread's final
	// currentRecordClusterIndex/payload writes before we take over as producer in finalizeRecordedFile().
	if (status.load(std::memory_order_acquire) == RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
		if (!hadCardError) {
			switch (advanceFinalize()) {
			case FinalizeStep::Pending:
				// One operation issued, or one phase advanced. Come back next tick; the status
				// transitions below must not run until finalize is actually finished.
				return Error::NONE;
			case FinalizeStep::Failed:
				hadCardError = true;
				error = Error::SD_CARD;
				break;
			case FinalizeStep::Done:
				break; // Fall through to the status transitions, which are unchanged.
			}
		}

		// A failed take still gives its file back, and through the channel: the stream's destructor would close
		// it synchronously, on whatever tier frees this recorder.
		if (hadCardError && closeFailedTake()) {
			return error;
		}

		if (reachedMaxFileSize) {
			if (autoDeleteWhenDone) {
				abort();
			}
			else {
				// RELAXED: fiber-owned write.
				status.store(RecorderStatus::COMPLETE, std::memory_order_relaxed);
			}
			error = Error::MAX_FILE_SIZE_REACHED;
		}
		else {
			// RELAXED: fiber-owned write.
			status.store(autoDeleteWhenDone ? RecorderStatus::AWAITING_DELETION : RecorderStatus::COMPLETE,
			             std::memory_order_relaxed);
		}
	}

allDoneForNow:
	return error;
}

Error SampleRecorder::writeOneCompletedCluster(bool mayUseChannel) {
	int32_t writingClusterIndex = firstUnwrittenClusterIndex;

	firstUnwrittenClusterIndex++; // Have to increment this before writing, cos while writing, the audio routine will be
	                              // called, and we need to be counting this cluster as "written", as in too late for it
	                              // to be modified (by writing a final length to it)

	// writeCluster() recycles this buffer's memory back to the free-list once the write succeeds --
	// no separate "reason" to drop; we privately own it outright.
	return writeCluster(writingClusterIndex, Cluster::size, mayUseChannel);
}

namespace {
/// The header sector this recorder reads back and patches. 512 is the first-sector size the old
/// synchronous path used; `audioDataStartPosBytes` is always 44 or 112, well inside it.
constexpr uint32_t kHeaderSectorBytes = 512;
} // namespace

// --- finalize, as phases -------------------------------------------------------------------------
//
// What was one long blocking call is now one operation per tick. Each phase either ISSUES its
// operation and returns Pending, or COLLECTS the answer it issued last time and asks
// `nextFinalizePhase` where to go. `finalizeIssued_` is what tells those two apart.

/// The overhang cluster, run once when the machine enters DrainClusters. Pure memory and
/// bookkeeping apart from the cluster writes it may queue, which the drain phase then picks up.
void SampleRecorder::finalizePrologueOnce() {
	if (finalizePrologueDone_) {
		return;
	}
	finalizePrologueDone_ = true;
	D_PRINTLN("finalizing");

	// In the very rare case where we've already got between 1 and 5 bytes overhanging the end of our current cluster,
	// we need to allocate a new one right now
	int32_t bytesTilClusterEnd = clusterEndPos - writePos;
	if (bytesTilClusterEnd < 0) {
		Error error = createNextCluster();

		if (error == Error::MAX_FILE_SIZE_REACHED) {
		} // So incredibly unlikely. But no real problem - we maybe just lose a byte or two

		else if (error != Error::NONE) {
			hadCardError = true;
		}
		else { // No error
			   // Having just created a new cluster, there'll be one more completed one to write
			   // The drain phase writes these one per tick; nothing to do here but let it.
		}
	}
}

/// The partially-filled final cluster.
Error SampleRecorder::finalizeWriteFinalPartial() {
	// And we probably need to write some of the final buffer to file. (If it's NULL, it means that it couldn't be
	// created, cos or RAM or file size limit.)
	if (currentRecordBuffer) {

		int32_t bytesToWrite = writePos - reinterpret_cast<char*>(currentRecordBuffer);
		if (bytesToWrite > 0) { // Will always be true
			// Through the channel wherever the channel is usable. The header read-back that follows waits for
			// it: `pumpInFlightWrite()` at the top of cardRoutine() settles this write before finalize runs
			// again, and a failed one ends the take there.
			Error error = writeCluster(currentRecordClusterIndex.load(std::memory_order_relaxed), bytesToWrite,
			                           /*mayUseChannel=*/true);
			if (error != Error::NONE) {
				return error;
			}
		}
		else {
			// writeCluster() (above) is what recycles this buffer -- if we didn't call it, recycle
			// directly so this buffer's memory isn't leaked.
			recycleBuffer(currentRecordBuffer);
			// Same drained-slot nulling writeCluster() does -- see its comment.
			// currentRecordClusterIndex still refers to this now-recycled buffer's slot here.
			bufferTable_[currentRecordClusterIndex.load(std::memory_order_relaxed)] = nullptr;
		}

		firstUnwrittenClusterIndex++;
		currentRecordClusterIndex.fetch_add(1, std::memory_order_relaxed); // We've finished with that cluster
		currentRecordBuffer = nullptr; // But currentRecordClusterIndex now refers to a cluster that'll never exist
	}
	return Error::NONE;
}

/// PURE: what processing the recording needs, and what the file's final length will be.
void SampleRecorder::finalizeDecide() {
	uint32_t idealFileSizeBeforeAction = sample->audioDataStartPosBytes + sample->audioDataLengthBytes;
	uint32_t dataLengthBeforeAction = sample->audioDataLengthBytes;

	// Figure out what processing needs to happen on the recorded audio
	MonitoringAction action = MonitoringAction::NONE;
	int32_t lshiftAmount = 0;

	if (allowFileAlterationAfter
	    && idealFileSizeBeforeAction <= 67108864) { // Arbitrarily, don't alter files bigger than 64MB
		if (recordingNumChannels == 1) {
			action = MonitoringAction::NONE;
		}
		else {
			// If R is really quiet or is nearly identical to L, delete R
			if (inputHasNoRightChannel() || recordSumLMinusR < (recordSumL >> 6)) {
				D_PRINTLN("removing right channel");
				action = MonitoringAction::REMOVE_RIGHT_CHANNEL;
			}

			// Or, if R is the differential signal of L, do that
			else if (mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION && AudioEngine::lineInPluggedIn
			         && inputLooksDifferential()) {
				D_PRINTLN("subtracting right channel");
				action = MonitoringAction::SUBTRACT_RIGHT_CHANNEL;
			}

			else {
				D_PRINTLN("keeping right channel");
				action = MonitoringAction::NONE;
			}
		}

		uint32_t maxPeak;
		if (action == MonitoringAction::SUBTRACT_RIGHT_CHANNEL) {
			maxPeak = -1 - recordPeakLMinusR;
		}
		else {
			maxPeak = -1 - std::min(recordPeakL, recordPeakR);
		}

		if (allowNormalization) {
			for (lshiftAmount = 0; ((uint32_t)2147483648 >> (lshiftAmount + 1)) > maxPeak; lshiftAmount++) {}
		}
	}
	uint32_t dataLengthAfterAction =
	    action != MonitoringAction::NONE ? (dataLengthBeforeAction >> 1) : dataLengthBeforeAction;

	// TODO: in a perfect world, where we're not deleting a channel, we'd go backwards from the last Cluster, because
	// that's the most likely to still be in memory

	finalizeAction_ = action;
	finalizeLshift_ = lshiftAmount;
	finalizeAltering_ = (lshiftAmount != 0 || action != MonitoringAction::NONE);
	finalizeDataLenAfter_ = action != MonitoringAction::NONE ? (dataLengthBeforeAction >> 1) : dataLengthBeforeAction;
	finalizeIdealSizeBefore_ = idealFileSizeBeforeAction;
	if (finalizeAltering_) {
		// The alteration rewrites the audio in place, so its final length comes from the action, not
		// from what was captured -- and its truncate is conditional on the same thing the old code
		// used: an action that halves the data, or a capture that overran.
		finalizeTruncateTo_ = static_cast<uint32_t>(finalizeDataLenAfter_) + sample->audioDataStartPosBytes;
		finalizeNeedTruncate_ = (action != MonitoringAction::NONE) || capturedTooMuch;
		// Input region: `idealFileSizeBeforeAction` bytes from the start, in frames of 3 (mono in,
		// for NONE) or 6 (stereo in, for the two channel-combining actions).
		alterInputFrameBytes_ = (action == MonitoringAction::NONE) ? 3u : 6u;
		alterNumFrames_ = (idealFileSizeBeforeAction - sample->audioDataStartPosBytes) / alterInputFrameBytes_;
		alterReadCursor_ = sample->audioDataStartPosBytes;
		alterWriteCursor_ = sample->audioDataStartPosBytes;
		alterFramesDone_ = 0;
		// The alteration always rewrites the header: its length changed, and for a channel-combining
		// action so did its channel count.
		finalizeNeedHeaderPatch_ = true;
		audioDataLengthBytesAsWrittenToFile = finalizeDataLenAfter_;
		loopEndSampleAsWrittenToFile = sample->fileLoopEndSamples;
		finalizeHeaderLen_ = sample->audioDataStartPosBytes;
		return;
	}
	finalizeTruncateTo_ = sample->audioDataStartPosBytes + static_cast<uint32_t>(sample->audioDataLengthBytes);
	finalizeNeedTruncate_ = capturedTooMuch;
	// The header was written before the true length was known, which is the usual case rather than
	// the exception.
	finalizeNeedHeaderPatch_ = sample->audioDataLengthBytes != audioDataLengthBytesAsWrittenToFile
	                           || (recordingExtraMargins && sample->fileLoopEndSamples != loopEndSampleAsWrittenToFile);
	if (finalizeNeedHeaderPatch_) {
		audioDataLengthBytesAsWrittenToFile = sample->audioDataLengthBytes;
		loopEndSampleAsWrittenToFile = sample->fileLoopEndSamples;
	}
}

/// PURE: final geometry and the overview-cache grow.
Error SampleRecorder::finalizeBookkeeping() {
	const MonitoringAction action = finalizeAction_;
	const uint64_t dataLengthAfterAction = finalizeDataLenAfter_;
	sample->numChannels = (action != MonitoringAction::NONE || recordingNumChannels == 1) ? 1 : 2;
	sample->lengthInSamples = dataLengthAfterAction / (sample->byteDepth * sample->numChannels);
	sample->audioDataLengthBytes =
	    sample->lengthInSamples
	    * (sample->byteDepth
	       * sample->numChannels); // Ensure whole number of samples (surely it already would be though?)

	// Ensure the waveform overview cache (Sample::overviewCache_, one entry per cluster) is sized to
	// the real, final cluster count once the recording's true length is known, so later zoomed-out
	// waveform rendering can index every cluster of the finished file. This runs for BOTH finalize
	// outcomes:
	//   - the alterFile branch above never touches overviewCache_ -- alterFile() is a positional
	//     file->file transform over deluge::io::Stream (read_at_via/write_at), with no overview-cache
	//     involvement whatsoever. So overviewCache_ is still sitting at the single entry
	//     Sample::initialize(1) set in setup() when we reach here, and this resize is an ACTUAL grow
	//     (1 -> finalClusterCount), not a no-op.
	//   - the common, no-alteration else-branch -- the ONLY path AudioClip recording takes -- never
	//     touches overviewCache_ either, so without this it likewise stays that single entry.
	//
	// Grow-only (never shrink here): SegmentedVector::resize() to a SMALLER size destroys the
	// removed tail, which would be wrong here -- shrinking is truncateFileDownToSize()'s job, not
	// finalize's.
	{
		uint32_t idealFileSizeAfterAction =
		    sample->audioDataStartPosBytes + static_cast<uint32_t>(sample->audioDataLengthBytes);
		uint32_t finalClusterCount = ((idealFileSizeAfterAction - 1) >> Cluster::size_magnitude) + 1;
		// Guard on the PHYSICAL cache size, not num_clusters(): num_clusters() is derived from this
		// same geometric formula, so guarding on it would be tautologically false and skip this grow,
		// leaving overviewCache_ at its initialize(1) size while derived num_clusters() reports the
		// true count -- an OOB when later waveform rendering indexes the cache up to that count.
		if (finalClusterCount > sample->overviewCacheSize()) {
			try {
				sample->resizeOverviewCache(finalClusterCount);
			} catch (deluge::exception&) {
				return Error::INSUFFICIENT_RAM;
			}
		}
	}

	if (sample->tempFilePathForRecording.empty()) {
		sampleBrowser.lastFilePathLoaded = sample->filePath;
	}

	return Error::NONE;
}

// --- abort, as phases ----------------------------------------------------------------------------
//
// One rule matters here above all the others, and it is the transition from CloseStream to Unlink:
// the close must have COMPLETED, not merely been issued. Closing is what flushes the directory entry
// this recorder deferred for the whole recording; unlinking first leaves a stale near-empty entry
// with first_cluster still ~0, frees no clusters, and lets a later file that recycles the freed
// short-name slot receive this handle's deferred flush.

bool SampleRecorder::advanceAbort() {
	// The channel is usable only from a context that will yield before asking again -- see
	// advanceFinalize() for why. On the owner the synchronous arm is both legal and the only one
	// that can make progress.
	const bool mayUseChannel = !deluge::storage::Owner::on_owner() && deluge::storage::Owner::started();

	switch (abortPhase_) {

	case AbortPhase::Detach: {
		// Memory only, and still FIRST, exactly as before: a song-swap abort is about to search for
		// temp samples to delete and must not trip over this one (previously an E281). Its safety
		// against the channel comes from pumpInFlightWrite() at the top of cardRoutine, which has
		// already settled anything outstanding by the time we get here.
		if (sample != nullptr) { // This might get called multiple times, so check we haven't already detached it.

			// Note: if this abort() is due to a song-swap (loading a different song),
			// then samples is about to be searched for temp ones to delete, and we'll need to have deleted it here
			// before that trips over us. Previously caused an E281. So, for that to happen,
			// SampleManager::deleteAnyTempRecordedSamplesFromMemory() (indirectly) calls us here first.

			detachSample(); // Does not set sample to NULL - we do that below

#if ALPHA_OR_BETA_VERSION
			// It should be impossible that anyone else still holds a "reason" to this Sample, as we can only be
			// "aborted" before AudioClip::finishLinearRecording() is called, and it's only then at the AudioClip
			// becomes a "reason".
			if (sample->isProjectReferenced()) {
				FREEZE_WITH_ERROR("E282");
			}
#endif

			if (haveAddedSampleToArray) { // We only add it to the array when the file is created.
				audioFileManager.deleteUnusedAudioFileFromMemoryIndexUnknown(*sample);
			}

			sample = nullptr; // So we don't try to detach it again when we're destructed
		}

		abortPhase_ = AbortPhase::CloseStream;
		return true;
	}

	case AbortPhase::CloseStream: {
		// The unlink follows the close, unless no file was ever created.
		const AbortPhase afterClose = filePathCreated.empty() ? AbortPhase::Done : AbortPhase::Unlink;
		if (!file) {
			// Nothing was ever opened, or it is already closed.
			abortPhase_ = afterClose;
			return abortPhase_ != AbortPhase::Done;
		}
		if (!abortIssued_) {
			if (mayUseChannel) {
				// Refused, however often: ask again. The only ways past a close the channel will not take are
				// closing here, which blocks this tier on the card, and unlinking under an open handle, which is
				// the hazard this ordering exists to prevent. An abort has nothing to show, so it waits.
				if (file->request_close()) {
					abortIssued_ = true;
				}
				else {
					g_channelRefusals++;
				}
				return true;
			}
			noteRecorderIo();
			auto closeResult = file->close();
			file.reset();
			// A failed close still proceeds to the unlink: a file we are deleting must not survive
			// because its close reported an error. This is the opposite of finalize, deliberately.
			(void)closeResult;
			abortPhase_ = afterClose;
			abortIssued_ = false;
			return abortPhase_ != AbortPhase::Done;
		}
		const uint8_t collected = file->collect_close();
		if (collected == DELUGE_FILE_WRITE_PENDING) {
			return true; // NOT terminal yet -- the unlink must not go out.
		}
		if (collected == DELUGE_FILE_WRITE_UNKNOWN) {
			// Never keyed, or its answer was displaced: the handle is still open, so ask again.
			abortIssued_ = false;
			return true;
		}
		file.reset();
		abortPhase_ = afterClose;
		abortIssued_ = false;
		return abortPhase_ != AbortPhase::Done;
	}

	case AbortPhase::Unlink: {
		bool unlinked = false;
		using deluge::storage::bytes::ByteIo;
		if (!abortIssued_) {
			deluge_file_invalidate_cache();
			if (mayUseChannel) {
				const ByteIo io = deluge::storage::bytes::requestUnlink(filePathCreated.c_str());
				if (io == ByteIo::Pending) {
					abortIssued_ = true;
					abortUnproductiveTicks_ = 0;
					return true;
				}
				// Refused: ask again, until the bound. Giving up leaves the file where it is, exactly as a failed
				// delete does; an unkeyable path cannot be deleted at all.
				if (io == ByteIo::Unknown && abortUnproductiveTicks_++ < deluge::storage::kMaxUnproductiveTicks) {
					g_channelRefusals++;
					return true;
				}
			}
			else {
				noteRecorderIo();
				unlinked = deluge::io::unlink(filePathCreated).has_value();
			}
		}
		else {
			DelugeStatus outStatus = DELUGE_OK;
			const ByteIo io = deluge::storage::bytes::collectUnlink(filePathCreated.c_str(), &outStatus);
			if (io == ByteIo::Pending) {
				// Waited out however long it takes: the path is in the driver's hands, and an abort has nothing
				// to show.
				return true;
			}
			if (io == ByteIo::Unknown && abortUnproductiveTicks_++ < deluge::storage::kMaxUnproductiveTicks) {
				// Never keyed, or its answer was displaced -- ask again next tick rather than treating it as a
				// failed delete.
				abortIssued_ = false;
				return true;
			}
			unlinked = (io == ByteIo::Ready);
		}

		// If this was the most recent recording in this category, tick the counter backwards - so long as
		// either the delete was successful or it was for an AudioClip, which means the file is in the TEMP folder
		// and can be overwritten anyway
		if (unlinked || folderID == AudioRecordingFolder::CLIPS) {
			if (audioFileManager.highestUsedAudioRecordingNumber[util::to_underlying(folderID)] == audioFileNumber) {
				audioFileManager.highestUsedAudioRecordingNumber[util::to_underlying(folderID)]--;
				D_PRINTLN("ticked file counter backwards");
			}
		}
		filePathCreated.clear();
		abortPhase_ = AbortPhase::Done;
		abortIssued_ = false;
		return false;
	}

	case AbortPhase::Done:
		return false;
	}
	return false;
}

bool SampleRecorder::closeFailedTake() {
	if (!file) {
		return false;
	}
	const bool mayUseChannel = !deluge::storage::Owner::on_owner() && deluge::storage::Owner::started();
	if (!failedCloseIssued_) {
		if (mayUseChannel) {
			// Refused, however often: ask again, for the reason advanceAbort() gives. The take has already failed
			// and said so; this only gives its file back.
			if (file->request_close()) {
				failedCloseIssued_ = true;
			}
			else {
				g_channelRefusals++;
			}
			return true;
		}
		noteRecorderIo();
		(void)file->close();
		file.reset();
		return false;
	}
	const uint8_t collected = file->collect_close();
	if (collected == DELUGE_FILE_WRITE_PENDING) {
		return true;
	}
	failedCloseIssued_ = false;
	if (collected == DELUGE_FILE_WRITE_UNKNOWN) {
		return true; // Never keyed, or displaced: the handle is still open, so ask again.
	}
	file.reset();
	return false;
}

namespace {
/// Create `path` and every missing level above it, synchronously.
///
/// The owner arm's counterpart to the byte channel's `mkdir_all`, which walks the segments in Rust.
/// This is the same one-level-at-a-time retry the create block used to express with
/// `cutRecordingFolderPathAndTryCreating`, minus the goto: shorten the path at each separator until
/// a mkdir succeeds, then create back down.
bool mkdirAllSynchronously(const std::string& path) {
	if (path.empty()) {
		return false;
	}
	auto made = deluge::io::mkdir(path.c_str());
	if (made || made.error() == deluge::io::Status::EXISTS) {
		return true;
	}
	if (made.error() != deluge::io::Status::NOT_FOUND) {
		return false;
	}
	const size_t slashPos = path.find_last_of('/');
	if (slashPos == std::string::npos) {
		return false;
	}
	if (!mkdirAllSynchronously(path.substr(0, slashPos))) {
		return false;
	}
	auto retry = deluge::io::mkdir(path.c_str());
	return retry.has_value() || retry.error() == deluge::io::Status::EXISTS;
}
} // namespace

// --- creating the recording's file, as phases ----------------------------------------------------
//
// What was a block threaded with `goto tryOpenRecordingStream` and
// `goto cutRecordingFolderPathAndTryCreating` is now four phases. The retry those labels expressed
// -- create the parent folder once, then try the open again -- lives in `nextCreatePhase`, where a
// spec can check it without a filesystem.

SampleRecorder::CreateStep SampleRecorder::advanceCreate() {
	using deluge::storage::CreatePhase;

	// The channel is usable only from a context that will YIELD before asking again -- see
	// advanceFinalize() for why. On the owner the caller spins and the Rust driver never runs.
	const bool mayUseChannel = !deluge::storage::Owner::on_owner() && deluge::storage::Owner::started();

	if (!deluge::storage::Owner::on_owner()) {
		AudioEngine::noteOffOwnerCreateTick();
	}

	uint8_t collected = DELUGE_FILE_WRITE_UNKNOWN;
	bool spaceExhausted = false;
	bool pathReady = false;

	if (!createIssued_) {
		switch (createPhase_) {

		case CreatePhase::Stats: {
			// initSD stays, and only on this arm. The channel arm mounts lazily (file_write.rs's
			// open_mounted); the OWNER arm reaches efatfs directly through deluge_efatfs_stream_open
			// and needs the card already mounted. Deleting it would leave the rig green -- it mounts
			// at boot -- and break a card inserted afterwards. Running it on the channel arm would
			// block for something about to happen anyway.
			if (!mayUseChannel) {
				noteRecorderIo();
				Error initError = StorageManager::initSD();
				if (initError != Error::NONE) {
					return CreateStep::Failed;
				}
				uint32_t freeClusters = 0;
				uint32_t totalClusters = 0;
				// Best-effort, exactly as checkSpaceOnCard has always been: a query that cannot run
				// is not a full card.
				if (deluge_efatfs_stats(&freeClusters, &totalClusters) && freeClusters == 0) {
					spaceExhausted = true;
				}
				break;
			}
			if (deluge_file_write_request_stats()) {
				return createAccepted();
			}
			g_channelRefusals++;
			break; // Refused: treat as "space available" and carry on. A query that cannot run is not a full card.
		}

		case CreatePhase::PathSearch: {
			Error error = Error::NONE;
			if (stemExport.processStarted) {
				error = stemExport.getUnusedStemRecordingFilePath(&createFilePath_, folderID);
			}
			else {
				const char* name;
				if (mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION && !AudioEngine::lineInPluggedIn) {
					if (AudioEngine::micPluggedIn) {
						name = "ExtMic";
					}
					else {
						name = "IntMic";
					}
				}
				else if (mode == AudioInputChannel::SPECIFIC_OUTPUT) {
					if (outputRecordingFrom) {
						name = outputRecordingFrom->name.c_str();
					}
				}
				else {
					name = inputChannelToString(mode);
				}
				error = audioFileManager.getUnusedAudioRecordingFilePath(
				    createFilePath_, &createTempFilePath_, folderID, &audioFileNumber, name, &currentSong->name);
			}
			// ACQUIRE: cardRoutine() decision read.
			if (status.load(std::memory_order_acquire) == RecorderStatus::ABORTED) {
				return CreateStep::Failed; // The abort machine takes it from here.
			}
			if (error == Error::LISTING_DEFERRED) {
				// The search is waiting on its own channel answer: come back next tick, but not for good. Counted like
				// a refused request, so a folder scan whose open the channel never answers fails the create at the
				// bound.
				const CreateStep step = createTransition(DELUGE_FILE_WRITE_UNKNOWN);
				createUnproductiveTicks_++;
				return step;
			}
			if (error != Error::NONE) {
				return CreateStep::Failed;
			}
			// Store our own copy of the actually (possibly temp) filename.
			bool mayOverwrite = true;
			if (!createTempFilePath_.empty()) {
				filePathCreated = createTempFilePath_.c_str(); // Can't fail!
			}
			else {
				filePathCreated = createFilePath_.c_str(); // Can't fail!
				mayOverwrite = false;
			}
			createStreamMode_ = mayOverwrite ? DELUGE_STREAM_WRITE_CREATE : DELUGE_STREAM_WRITE_CREATE_NEW;
			pathReady = true;
			break;
		}

		case CreatePhase::Open: {
			// Bypasses the Tier 2 locator cache (same as the raw FatFS open createFileRaw used to
			// do) -- must invalidate it, or a stale cached locator for this exact path (from an
			// earlier directory listing) could survive a mayOverwrite create-always reopen and feed
			// a later cache-hit read the pre-overwrite file's stale sclust/objsize.
			deluge_file_invalidate_cache();
			createOpenNotFound_ = false;
			if (mayUseChannel) {
				// Refused: nothing was queued, so ask again next tick, until the bound. Opening here instead would
				// block this tier on the card.
				const bool accepted = deluge::io::Stream::request_open(filePathCreated, createStreamMode_);
				return accepted ? createAccepted() : createRefused();
			}
			noteRecorderIo();
			auto openedStream = deluge::io::Stream::open(filePathCreated, createStreamMode_);
			if (openedStream) {
				file = std::move(openedStream.value());
				break;
			}
			// deluge::io::Stream::open does no folder-creation retry of its own -- but the
			// containing folder legitimately might not exist yet (AudioClip's CLIPS/TEMP subfolder,
			// or a per-song recording subfolder under RECORD/RESAMPLE). That is the one open failure
			// worth acting on.
			createOpenNotFound_ = (openedStream.error() == deluge::io::Status::NOT_FOUND);
			collected = DELUGE_FILE_WRITE_FAILED;
			break;
		}

		case CreatePhase::MakeFolder: {
			createTriedMakeFolder_ = true;
			createFolderPath_ = filePathCreated;
			const size_t slashPos = createFolderPath_.find_last_of('/');
			if (slashPos == std::string::npos) {
				return CreateStep::Failed;
			}
			createFolderPath_.resize(slashPos);
			// ONE mkdir_all, not one level at a time: the byte channel walks the segments in Rust,
			// which is the same collapse the browser's folder creation got.
			if (mayUseChannel) {
				switch (deluge::storage::bytes::requestMkdirAll(createFolderPath_.c_str())) {
				case deluge::storage::bytes::ByteIo::Pending:
					return createAccepted();
				case deluge::storage::bytes::ByteIo::Unknown:
					return createRefused(); // Busy: nothing was queued.
				default:
					return CreateStep::Failed; // The path cannot be keyed at all.
				}
			}
			noteRecorderIo();
			if (!mkdirAllSynchronously(createFolderPath_)) {
				return CreateStep::Failed;
			}
			break;
		}

		case CreatePhase::Done:
			return CreateStep::Done;
		case CreatePhase::Failed:
			return CreateStep::Failed;
		}
		if (collected == DELUGE_FILE_WRITE_UNKNOWN) {
			collected = DELUGE_FILE_WRITE_READY;
		}
	}
	else {
		uint32_t got = 0;
		switch (createPhase_) {
		case CreatePhase::Stats:
			collected = deluge_file_write_collect_stats(&got);
			if (collected == DELUGE_FILE_WRITE_READY && got == 0) {
				spaceExhausted = true;
			}
			else if (collected == DELUGE_FILE_WRITE_FAILED) {
				// A query that could not run is not a full card -- carry on, as the synchronous arm
				// always has.
				collected = DELUGE_FILE_WRITE_READY;
			}
			break;
		case CreatePhase::Open: {
			std::optional<deluge::io::Stream> opened;
			collected = deluge::io::Stream::collect_open(&opened);
			if (collected == DELUGE_FILE_WRITE_READY && opened.has_value()) {
				file = std::move(opened);
			}
			else if (collected == DELUGE_FILE_WRITE_FAILED) {
				// The channel cannot say NOT_FOUND apart from any other failure, so give the folder
				// retry its one chance rather than failing a recording whose only problem is a
				// missing parent.
				createOpenNotFound_ = !createTriedMakeFolder_;
			}
			break;
		}
		case CreatePhase::MakeFolder: {
			DelugeStatus outStatus = DELUGE_OK;
			const deluge::storage::bytes::ByteIo io =
			    deluge::storage::bytes::collectMkdirAll(createFolderPath_.c_str(), &outStatus);
			collected = (io == deluge::storage::bytes::ByteIo::Pending)   ? DELUGE_FILE_WRITE_PENDING
			            : (io == deluge::storage::bytes::ByteIo::Ready)   ? DELUGE_FILE_WRITE_READY
			            : (io == deluge::storage::bytes::ByteIo::Unknown) ? DELUGE_FILE_WRITE_UNKNOWN
			                                                              : DELUGE_FILE_WRITE_FAILED;
			break;
		}
		default:
			collected = DELUGE_FILE_WRITE_READY;
			break;
		}
		if (collected == DELUGE_FILE_WRITE_PENDING) {
			// Still in flight, even past the bound: an open may yet hand back a stream, so wait for the answer,
			// and fail on it if this went past the bound.
			if (deluge::storage::stalledPastBound(collected, createUnproductiveTicks_)) {
				createFailing_ = true;
			}
			createUnproductiveTicks_++;
			return CreateStep::Pending;
		}
		if (collected == DELUGE_FILE_WRITE_UNKNOWN) {
			// Never keyed, or its answer was displaced: ask again next tick, until the bound.
			createIssued_ = false;
			const CreateStep step = createTransition(collected);
			createUnproductiveTicks_++;
			return step;
		}
		if (createPhase_ == CreatePhase::PathSearch) {
			pathReady = true;
		}
	}

	return createTransition(collected, spaceExhausted, pathReady);
}

SampleRecorder::CreateStep SampleRecorder::createAccepted() {
	createIssued_ = true;
	createUnproductiveTicks_ = 0;
	return CreateStep::Pending;
}

SampleRecorder::CreateStep SampleRecorder::createRefused() {
	g_channelRefusals++;
	const CreateStep step = createTransition(DELUGE_FILE_WRITE_UNKNOWN);
	createUnproductiveTicks_++;
	return step;
}

SampleRecorder::CreateStep SampleRecorder::createTransition(uint8_t collected, bool spaceExhausted, bool pathReady) {
	using deluge::storage::CreatePhase;
	const CreatePhase next = deluge::storage::nextCreatePhase(deluge::storage::CreateInputs{
	    createPhase_,
	    collected,
	    spaceExhausted,
	    pathReady,
	    createOpenNotFound_,
	    createTriedMakeFolder_,
	    createUnproductiveTicks_,
	    createFailing_,
	});
	if (next != createPhase_) {
		createIssued_ = false;
		createUnproductiveTicks_ = 0;
		createFailing_ = false;
	}
	createPhase_ = next;

	if (createPhase_ == CreatePhase::Done) {
		return CreateStep::Done;
	}
	if (createPhase_ == CreatePhase::Failed) {
		return CreateStep::Failed;
	}
	return CreateStep::Pending;
}

SampleRecorder::FinalizeStep SampleRecorder::advanceFinalize() {
	using deluge::storage::FinalizePhase;

	// ACQUIRE: cardRoutine() decision read (debug assertion, same fiber path as the flip observation
	// above).
	if (ALPHA_OR_BETA_VERSION && finalizePhase_ == FinalizePhase::DrainClusters && !finalizeIssued_
	    && (status.load(std::memory_order_acquire) == RecorderStatus::ABORTED || hadCardError)) {
		FREEZE_WITH_ERROR("E273");
	}

	// The channel is usable only from a context that will YIELD before asking again. On the storage
	// owner the caller spins -- the offline stem-export drain and the harness's own pump both do --
	// and the Rust driver never gets to run, so an issued request would stay PENDING forever. There
	// blocking is legal anyway, so take the synchronous arm. `started()` keeps anything running
	// before the worker exists on the arm it has always used.
	const bool finalizeMayUseChannel = !deluge::storage::Owner::on_owner() && deluge::storage::Owner::started();

	if (!deluge::storage::Owner::on_owner()) {
		AudioEngine::noteOffOwnerFinalizeTick();
	}

	uint8_t collected = DELUGE_FILE_WRITE_UNKNOWN;

	// Issue this phase's operation, if it has one and has not issued it yet.
	if (!finalizeIssued_) {
		switch (finalizePhase_) {

		case FinalizePhase::DrainClusters: {
			finalizePrologueOnce();
			if (firstUnwrittenClusterIndex < currentRecordClusterIndex.load(std::memory_order_acquire)) {
				// One cluster per tick. `pumpInFlightWrite()` at the top of cardRoutine collects it,
				// so this phase never collects for itself -- it just re-enters until none remain.
				if (writeOneCompletedCluster(/*mayUseChannel=*/true) != Error::NONE) {
					return FinalizeStep::Failed;
				}
				return FinalizeStep::Pending;
			}
			break; // Drained: fall through to the transition below.
		}

		case FinalizePhase::WriteFinalPartial:
			if (finalizeWriteFinalPartial() != Error::NONE) {
				return FinalizeStep::Failed;
			}
			break;

		case FinalizePhase::Decide:
			finalizeDecide();
			break;

		case FinalizePhase::Truncate:
			if (!file) {
				return FinalizeStep::Failed;
			}
			if (finalizeMayUseChannel) {
				// Refused: nothing was queued, so ask again next tick, until the bound. Every phase below does the
				// same; doing it here instead would block this tier on the card.
				return file->request_truncate(finalizeTruncateTo_) ? finalizeAccepted() : finalizeRefused();
			}
			// On the owner, or before the owner has started: blocking is legal there, and it is the only arm that
			// makes progress.
			noteRecorderIo();
			if (!file->truncate(finalizeTruncateTo_)) {
				return FinalizeStep::Failed;
			}
			break;

		case FinalizePhase::HeaderRead: {
			if (!file) {
				return FinalizeStep::Failed;
			}
			const uint32_t headerWant = finalizeAltering_ ? sample->audioDataStartPosBytes : kHeaderSectorBytes;
			std::span<std::byte> dst(headerScratch_.data(), headerWant);
			if (finalizeMayUseChannel) {
				return file->request_read_at(0, dst) ? finalizeAccepted() : finalizeRefused();
			}
			noteRecorderIo();
			auto readResult = file->read_at_via(0, dst);
			if (!readResult) {
				return FinalizeStep::Failed;
			}
			if (*readResult == 0) {
				finalizeNeedHeaderPatch_ = false;
			}
			finalizeHeaderLen_ = *readResult;
			break;
		}

		case FinalizePhase::HeaderWrite: {
			if (!file) {
				return FinalizeStep::Failed;
			}
			std::span<std::byte> headerBytes(headerScratch_.data(), finalizeHeaderLen_);
			updateDataLengthInHeader(headerBytes);
			if (finalizeAltering_ && finalizeAction_ != MonitoringAction::NONE) {
				// A channel-combining action leaves a mono file: channel count, byte rate and block
				// align all follow. Same three patches the old alterFile made.
				uint16_t data16 = 1;
				memcpy(headerBytes.data() + 22, &data16, 2);
				uint32_t data32 = kSampleRate * 1 * 3;
				memcpy(headerBytes.data() + 28, &data32, 4);
				data16 = 1 * 3;
				memcpy(headerBytes.data() + 32, &data16, 2);
			}
			if (finalizeMayUseChannel) {
				const std::span<const std::byte> src(headerBytes);
				return file->request_write_at(0, src) ? finalizeAccepted() : finalizeRefused();
			}
			// The old path ignored a failed header write here ("that's a shame, but we don't need to
			// do anything"), and that judgement is preserved: the audio is on the card either way.
			noteRecorderIo();
			(void)file->write_at(0, std::span<const std::byte>(headerBytes));
			break;
		}

		case FinalizePhase::Close: {
			if (!file) {
				break; // Already closed by the alteration path; nothing to wait for.
			}
			if (finalizeMayUseChannel) {
				return file->request_close() ? finalizeAccepted() : finalizeRefused();
			}
			noteRecorderIo();
			auto closeResult = file->close();
			file.reset();
			if (!closeResult) {
				return FinalizeStep::Failed;
			}
			break;
		}

		case FinalizePhase::CloseBeforeAlter: {
			// The recording stream is closed and the file reopened for append, because the transform
			// reads and writes the same file through two cursors and wants a context that starts at
			// its true size.
			if (!file) {
				break;
			}
			if (finalizeMayUseChannel) {
				return file->request_close() ? finalizeAccepted() : finalizeRefused();
			}
			noteRecorderIo();
			auto closeResult = file->close();
			file.reset();
			if (!closeResult) {
				return FinalizeStep::Failed;
			}
			break;
		}

		case FinalizePhase::OpenForAlter: {
			if (finalizeMayUseChannel) {
				const bool accepted = deluge::io::Stream::request_open(sample->filePath, DELUGE_STREAM_WRITE_APPEND);
				return accepted ? finalizeAccepted() : finalizeRefused();
			}
			noteRecorderIo();
			auto openedStream = deluge::io::Stream::open(sample->filePath, DELUGE_STREAM_WRITE_APPEND);
			if (!openedStream) {
				return FinalizeStep::Failed;
			}
			file = std::move(openedStream.value());
			break;
		}

		case FinalizePhase::BatchRead: {
			const uint32_t bytes = alterBatchFrames() * alterInputFrameBytes_;
			std::span<std::byte> dst(alterBatchBuf_.data(), bytes);
			if (!file) {
				return FinalizeStep::Failed;
			}
			if (finalizeMayUseChannel) {
				return file->request_read_at(alterReadCursor_, dst) ? finalizeAccepted() : finalizeRefused();
			}
			noteRecorderIo();
			auto readResult = file->read_at_via(alterReadCursor_, dst);
			// A short read mid-transform means the file is not what finalize believes it is.
			if (!readResult || *readResult != bytes) {
				return FinalizeStep::Failed;
			}
			transformBatchInPlace(alterBatchFrames());
			break;
		}

		case FinalizePhase::BatchWrite: {
			const uint32_t batchFrames = alterBatchFrames();
			const uint32_t outBytes = batchFrames * 3;
			std::span<const std::byte> src(alterBatchBuf_.data(), outBytes);
			if (!file) {
				return FinalizeStep::Failed;
			}
			if (finalizeMayUseChannel) {
				return file->request_write_at(alterWriteCursor_, src) ? finalizeAccepted() : finalizeRefused();
			}
			noteRecorderIo();
			auto writeResult = file->write_at(alterWriteCursor_, src);
			if (!writeResult || *writeResult != outBytes) {
				return FinalizeStep::Failed;
			}
			alterReadCursor_ += batchFrames * alterInputFrameBytes_;
			alterWriteCursor_ += outBytes;
			alterFramesDone_ += batchFrames;
			break;
		}

		case FinalizePhase::Bookkeeping:
			if (finalizeBookkeeping() != Error::NONE) {
				return FinalizeStep::Failed;
			}
			break;

		case FinalizePhase::Done:
			return FinalizeStep::Done;
		case FinalizePhase::Failed:
			return FinalizeStep::Failed;
		}
		collected = DELUGE_FILE_WRITE_READY; // A phase with nothing to wait for advances immediately.
	}
	else {
		// Collect the answer this phase issued last tick.
		uint32_t got = 0;
		switch (finalizePhase_) {
		case FinalizePhase::Truncate:
			collected = file ? file->collect_truncate() : DELUGE_FILE_WRITE_FAILED;
			break;
		case FinalizePhase::HeaderRead:
			collected = file ? file->collect_read_at(&got) : DELUGE_FILE_WRITE_FAILED;
			if (collected == DELUGE_FILE_WRITE_READY) {
				if (got == 0) {
					// Nothing came back at all: there is no header to patch, which the old path
					// treated as "leave it alone" rather than an error.
					finalizeNeedHeaderPatch_ = false;
				}
				finalizeHeaderLen_ = got;
			}
			break;
		case FinalizePhase::HeaderWrite:
			collected = file ? file->collect_write_at(&got) : DELUGE_FILE_WRITE_FAILED;
			break;
		case FinalizePhase::CloseBeforeAlter:
			collected = file ? file->collect_close() : DELUGE_FILE_WRITE_FAILED;
			if (collected == DELUGE_FILE_WRITE_READY || collected == DELUGE_FILE_WRITE_FAILED) {
				file.reset();
			}
			break;
		case FinalizePhase::OpenForAlter: {
			std::optional<deluge::io::Stream> opened;
			collected = deluge::io::Stream::collect_open(&opened);
			if (collected == DELUGE_FILE_WRITE_READY && opened.has_value()) {
				file = std::move(opened);
			}
			break;
		}
		case FinalizePhase::BatchRead:
			collected = file ? file->collect_read_at(&got) : DELUGE_FILE_WRITE_FAILED;
			if (collected == DELUGE_FILE_WRITE_READY) {
				if (got != alterBatchFrames() * alterInputFrameBytes_) {
					return FinalizeStep::Failed; // Short read mid-transform.
				}
				transformBatchInPlace(alterBatchFrames());
			}
			break;
		case FinalizePhase::BatchWrite:
			collected = file ? file->collect_write_at(&got) : DELUGE_FILE_WRITE_FAILED;
			if (collected == DELUGE_FILE_WRITE_READY) {
				const uint32_t batchFrames = alterBatchFrames();
				alterReadCursor_ += batchFrames * alterInputFrameBytes_;
				alterWriteCursor_ += batchFrames * 3;
				alterFramesDone_ += batchFrames;
			}
			break;
		case FinalizePhase::Close:
			collected = file ? file->collect_close() : DELUGE_FILE_WRITE_FAILED;
			if (collected == DELUGE_FILE_WRITE_READY || collected == DELUGE_FILE_WRITE_FAILED) {
				file.reset();
			}
			break;
		default:
			collected = DELUGE_FILE_WRITE_READY;
			break;
		}
		if (collected == DELUGE_FILE_WRITE_PENDING) {
			// Still in flight, even past the bound: the driver may be using this recorder's header or batch
			// buffer, so wait for the answer, and fail on it if this went past the bound.
			if (deluge::storage::stalledPastBound(collected, finalizeUnproductiveTicks_)) {
				finalizeFailing_ = true;
			}
			finalizeUnproductiveTicks_++;
			return FinalizeStep::Pending;
		}
		if (collected == DELUGE_FILE_WRITE_UNKNOWN) {
			// Never keyed, or its answer was displaced: ask again next tick, until the bound.
			finalizeIssued_ = false;
			const FinalizeStep step = finalizeTransition(collected);
			finalizeUnproductiveTicks_++;
			return step;
		}
	}

	return finalizeTransition(collected);
}

SampleRecorder::FinalizeStep SampleRecorder::finalizeAccepted() {
	finalizeIssued_ = true;
	finalizeUnproductiveTicks_ = 0;
	return FinalizeStep::Pending;
}

SampleRecorder::FinalizeStep SampleRecorder::finalizeRefused() {
	g_channelRefusals++;
	const FinalizeStep step = finalizeTransition(DELUGE_FILE_WRITE_UNKNOWN);
	finalizeUnproductiveTicks_++;
	return step;
}

SampleRecorder::FinalizeStep SampleRecorder::finalizeTransition(uint8_t collected) {
	using deluge::storage::FinalizePhase;
	const FinalizePhase next = deluge::storage::nextFinalizePhase(deluge::storage::FinalizeInputs{
	    finalizePhase_,
	    collected,
	    firstUnwrittenClusterIndex < currentRecordClusterIndex.load(std::memory_order_acquire),
	    currentRecordBuffer != nullptr,
	    finalizeAltering_,
	    alterFramesDone_ < alterNumFrames_,
	    finalizeNeedTruncate_,
	    finalizeNeedHeaderPatch_,
	    finalizeUnproductiveTicks_,
	    finalizeFailing_,
	});
	if (next != finalizePhase_) {
		finalizeIssued_ = false;
		finalizeUnproductiveTicks_ = 0;
		finalizeFailing_ = false;
	}
	finalizePhase_ = next;

	if (finalizePhase_ == FinalizePhase::Done) {
		return FinalizeStep::Done;
	}
	if (finalizePhase_ == FinalizePhase::Failed) {
		return FinalizeStep::Failed;
	}
	return FinalizeStep::Pending;
}

void SampleRecorder::updateDataLengthInHeader(std::span<std::byte> headerBuf) {
	// Write top-level RIFF chunk size
	*reinterpret_cast<uint32_t*>(headerBuf.data() + 4) =
	    audioDataLengthBytesAsWrittenToFile + sample->audioDataStartPosBytes - 8;

	// Write data chunk size
	*reinterpret_cast<uint32_t*>(headerBuf.data() + (sample->audioDataStartPosBytes - 4)) =
	    audioDataLengthBytesAsWrittenToFile;

	if (recordingExtraMargins) {
		// Write loop end point
		*reinterpret_cast<uint32_t*>(headerBuf.data() + 92) = loopEndSampleAsWrittenToFile;
	}
}

extern int32_t pendingGlobalMIDICommandNumClustersWritten;

// Called by the fiber (writeOneCompletedCluster()/finalizeRecordedFile()) for a buffer whose
// index is already < currentRecordClusterIndex (or the final partial one), so bufferTable_[clusterIndex]
// is stable -- the audio thread never rewrites an already-assigned slot, only appends new ones (see
// bufferTable_'s doc). Flushes it to disk, advances the committed-length watermark, then recycles the
// buffer's memory for reuse.
namespace {
/// How many cluster writes went through the channel, process-wide. Counted at the ISSUE, because
/// that is the point at which the write left the storage owner -- which is the thing this rung
/// changes and the thing a gate has to be able to see.
int32_t g_writeChannelServed = 0;
} // namespace

int32_t SampleRecorder::writeChannelServedOps() {
	return g_writeChannelServed;
}

int32_t SampleRecorder::channelRefusals() {
	return g_channelRefusals;
}

bool SampleRecorder::settled() const {
	// ACQUIRE: the same decision read cardRoutine() makes.
	const RecorderStatus current = status.load(std::memory_order_acquire);
	if (current < RecorderStatus::COMPLETE || writePending() || file.has_value()) {
		return false;
	}
	return current != RecorderStatus::ABORTED || abortPhase_ == AbortPhase::Done;
}

void SampleRecorder::finishCluster(int32_t clusterIndex) {
	// writeCluster() recycles this buffer's memory back to the free-list once the write succeeds --
	// no separate "reason" to drop; we privately own it outright.
	recycleBuffer(bufferTable_[clusterIndex]);
	// Null the table slot now that its buffer has been recycled -- this index is drained
	// (< firstUnwrittenClusterIndex once the caller advances it) and must never be read again, but
	// leaving a dangling pointer here would let a stale/reused buffer linger at a drained index.
	// releaseCaptureBuffers() already tolerates a null entry here (its `if (buffer != nullptr)`
	// guard).
	bufferTable_[clusterIndex] = nullptr;
}

bool SampleRecorder::pumpInFlightWrite() {
	using deluge::storage::WriteStep;

	if (inFlightClusterIndex_ < 0) {
		return false;
	}

	const int32_t index = inFlightClusterIndex_;
	uint32_t written = 0;
	const uint8_t collected = inFlightIssued_ ? file->collect_write_at(&written) : DELUGE_FILE_WRITE_UNKNOWN;

	const WriteStep step =
	    deluge::storage::nextWriteStep(/*channelUsable=*/true, inFlightIssued_, collected, writeUnproductiveTicks_);
	switch (step) {
	case WriteStep::Wait:
		writeUnproductiveTicks_++;
		return true; // Still outstanding: nothing that could free this buffer may run.

	case WriteStep::StalledWait:
		// Fail now, release later. The take has failed and issues nothing more, but the driver was handed a
		// pointer into this buffer and may still write from it, so the buffer stays until the answer comes.
		hadCardError = true;
		writeUnproductiveTicks_++;
		return true;

	case WriteStep::Issue:
		// Owed: refused last time, or answered UNKNOWN. Ask again. Either way it is still outstanding, so
		// nothing below may run this tick.
		inFlightIssued_ = false;
		(void)issueOwedWrite();
		return true;

	case WriteStep::Complete:
		if (written != inFlightNumBytes_) {
			hadCardError = true; // A short write is a card error, exactly as the synchronous arm says.
		}
		break;

	case WriteStep::Failed:
		// FAILED, or given up at the bound with nothing outstanding.
		hadCardError = true;
		break;

	case WriteStep::Inline:
		// Unreachable: a write is owed to the channel only where the channel is usable. Fail rather than block.
		hadCardError = true;
		break;
	}

	// Settled: the driver holds nothing of this buffer any more, whatever became of the write.
	inFlightClusterIndex_ = -1;
	inFlightIssued_ = false;
	writeUnproductiveTicks_ = 0;
	finishCluster(index);
	return false;
}

bool SampleRecorder::issueOwedWrite() {
	const uint32_t byteOffset = static_cast<uint32_t>(inFlightClusterIndex_) << Cluster::size_magnitude;
	const std::span<const std::byte> bytes(bufferTable_[inFlightClusterIndex_], inFlightNumBytes_);
	if (file->request_write_at(byteOffset, bytes)) {
		inFlightIssued_ = true;
		writeUnproductiveTicks_ = 0;
		g_writeChannelServed++;
		return true;
	}
	// Refused: nothing was queued. The cluster stays owed and its buffer stays put.
	g_channelRefusals++;
	writeUnproductiveTicks_++;
	return false;
}

Error SampleRecorder::writeClusterSynchronously(int32_t clusterIndex, size_t numBytes) {
	noteRecorderIo();
	std::byte* buffer = bufferTable_[clusterIndex];
	const uint32_t byteOffset = static_cast<uint32_t>(clusterIndex) << Cluster::size_magnitude;
	auto writeResult = file->write_at(byteOffset, std::span<const std::byte>(buffer, numBytes));
	if (!writeResult || *writeResult != numBytes) {
		return Error::SD_CARD;
	}
	finishCluster(clusterIndex);
	return Error::NONE;
}

Error SampleRecorder::writeCluster(int32_t clusterIndex, size_t numBytes, bool mayUseChannel) {
	// The same selector as create, finalize and abort. Off the owner once it has started, the channel is the
	// only arm: a refusal leaves the write owed rather than falling back to blocking here. On the owner the
	// write is synchronous from the start: an owner that spins -- the offline stem-export drain, the
	// harness's own pump -- never lets the driver answer, and blocking there is legal.
	if (mayUseChannel && !deluge::storage::Owner::on_owner() && deluge::storage::Owner::started()) {
		inFlightClusterIndex_ = clusterIndex;
		inFlightNumBytes_ = static_cast<uint32_t>(numBytes);
		inFlightIssued_ = false;
		writeUnproductiveTicks_ = 0;
		// Asking now saves a tick. Either way `pumpInFlightWrite()` collects it on a later tick and recycles
		// the buffer then; recycling here would hand the card a buffer the audio thread has reused.
		(void)issueOwedWrite();
		return Error::NONE;
	}

	return writeClusterSynchronously(clusterIndex, numBytes);
}

Error SampleRecorder::createNextCluster() {

	std::byte* oldRecordBuffer = currentRecordBuffer; // Cos we're gonna set that to NULL just below here, but still
	                                                  // want to be able to access the old one a bit further down

	// Mark the record-buffer we were on as finished. RELEASE: publishes that the just-completed
	// buffer's payload writes are visible to a consumer that later acquire-loads this index.
	int32_t newIndex = currentRecordClusterIndex.load(std::memory_order_relaxed) + 1;
	currentRecordClusterIndex.store(newIndex, std::memory_order_release);

	currentRecordBuffer = nullptr; // Note that we haven't yet created our next record-buffer - we'll do that below
	                               // if no error first; and if there is an error and we don't create one, this has to
	                               // remain NULL to indicate that we never created one

	// If this new cluster would actually put us past the 4GB limit...
	if (newIndex >= (1 << (MAX_FILE_SIZE_MAGNITUDE - Cluster::size_magnitude))) {

		// See if we actually already had any bytes to write into that new cluster we can't have...
		int32_t bytesTilClusterEnd = clusterEndPos - writePos;
		if (bytesTilClusterEnd < 0) {
			numSamplesCaptured--;
			writePos -= (int32_t)recordingNumChannels * 3;
		}

		totalSampleLengthNowKnown(numSamplesCaptured, numSamplesCaptured);

		reachedMaxFileSize = true;
		return Error::MAX_FILE_SIZE_REACHED;
	}

	// We need our next capture buffer -- a recycled one if the free-list has one, otherwise grow by
	// allocating fresh (the "grow if the fiber's fallen behind" case; see bufferTable_'s doc).
	std::byte* newBuffer = allocateBuffer();

	// If couldn't allocate a buffer (would normally only happen if no SD card present so recording only to RAM)
	if (!newBuffer) {
		D_PRINTLN("SampleRecorder::createNextCluster() fail");
		return Error::INSUFFICIENT_RAM;
	}

	try {
		bufferTable_.resize(static_cast<size_t>(newIndex) + 1);
	} catch (deluge::exception&) {
		delugeDealloc(newBuffer);
		return Error::INSUFFICIENT_RAM;
	}
	bufferTable_[newIndex] = newBuffer;
	currentRecordBuffer = newBuffer;

	// Copy those extra bytes from the end of the old record buffer to the start of the new buffer
	memcpy(currentRecordBuffer, oldRecordBuffer + Cluster::size,
	       5); // 5 is the max number of bytes we could have overshot

	int32_t bytesOvershot = writePos - clusterEndPos;

	writePos = reinterpret_cast<char*>(currentRecordBuffer + bytesOvershot);
	clusterEndPos = reinterpret_cast<char*>(currentRecordBuffer + Cluster::size);

	return Error::NONE;
}

// Gets called when we've captured all the samples of audio that we wanted - either as a direct result of user
// action, or after being fed a few more samples to make up for latency.
void SampleRecorder::finishCapturing() {
	// RELEASE: this is the producer-role hand-off to the fiber. Pairs with the acquire load in
	// cardRoutine() so the fiber, on seeing this status, also observes our final currentRecordClusterIndex
	// and payload writes before it takes over as producer in finalizeRecordedFile(). See B3.
	status.store(RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING, std::memory_order_release);

	// A freshly recorded sample now has a final length and needs its waveform overview pre-scanned. Re-arm
	// the background scan, which may have gone idle after all previously-loaded samples were scanned (#4460).
	audioFileManager.overviewScanAllDone = false;
	if (getRootUI()) {
		getRootUI()->sampleNeedsReRendering(sample);
	}
	if (outputRecordingFrom) {
		outputRecordingFrom->removeRecorder();
	}
}

// Only call this after checking that status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING
// Watch out - this could be called during SD writing - including during cardRoutine() for this class!
void SampleRecorder::feedAudio(std::span<StereoSample> input, bool applyGain, uint8_t gainToApply) {
	int32_t numSamples = input.size();
	do {
		int32_t numSamplesThisCycle = input.size();

		// If haven't actually started recording yet cos we're compensating for lag...
		if (numSamplesBeenRunning < (uint32_t)numSamplesToRunBeforeBeginningCapturing) {
			int32_t numSamplesTilBeginRecording = numSamplesToRunBeforeBeginningCapturing - numSamplesBeenRunning;
			numSamplesThisCycle = std::min(numSamplesThisCycle, numSamplesTilBeginRecording);
		}

		// Or, if properly recording...
		else {
			int32_t samplesLeft;

			// RELAXED: audio-thread-owned (only feedAudio/endSyncedRecording/finishCapturing, all on the
			// audio side, write this value before the release hand-off).
			if (status.load(std::memory_order_relaxed) == RecorderStatus::CAPTURING_DATA_WAITING_TO_STOP) {

				samplesLeft = sample->lengthInSamples - numSamplesCaptured;
				if (samplesLeft <= 0) {
doFinishCapturing:
					finishCapturing();
					return;
				}

				numSamplesThisCycle = std::min(numSamplesThisCycle, samplesLeft);
			}
			if (ALPHA_OR_BETA_VERSION && numSamplesThisCycle <= 0) {
				FREEZE_WITH_ERROR("bbbb");
			}

			int32_t bytesPerSample = recordingNumChannels * 3;
			int32_t bytesWeWantToWrite = numSamplesThisCycle * bytesPerSample;

			int32_t bytesTilClusterEnd = clusterEndPos - writePos;

			// If we need a new cluster right now...
			if (bytesTilClusterEnd <= 0) {
				Error error = createNextCluster();
				if (error == Error::MAX_FILE_SIZE_REACHED) {
					goto doFinishCapturing;
				}
				else if (error != Error::NONE) { // RAM error
					D_PRINTLN("couldn't allocate RAM");
					abort();
					return;
				}

				bytesTilClusterEnd = clusterEndPos - writePos; // Recalculate it
			}

			if (bytesTilClusterEnd <= bytesWeWantToWrite - bytesPerSample) {
				int32_t samplesTilClusterEnd =
				    (static_cast<uint16_t>(bytesTilClusterEnd - 1) / static_cast<uint8_t>(bytesPerSample))
				    + 1; // Rounds up
				numSamplesThisCycle = std::min(numSamplesThisCycle, samplesTilClusterEnd);
			}

			if (ALPHA_OR_BETA_VERSION && numSamplesThisCycle <= 0) {
				FREEZE_WITH_ERROR("aaaa");
			}

			std::byte* __restrict__ writePosNow = reinterpret_cast<std::byte*>(writePos);
			// Balanced input. For this, we skip a bunch of stat-grabbing, cos we know this is just for AudioClips.
			// We also know that applyGain is false - that's just for the MIX option
			if (mode == AudioInputChannel::BALANCED) {
				for (StereoSample sample : input.first(numSamplesThisCycle)) {
					q31_t rxBalanced = (sample.l / 2) - (sample.r / 2);

					// Copy the last 24 bits (lower 3 bytes) to writePosNow
					writePosNow = std::copy_n(&reinterpret_cast<std::byte*>(&rxBalanced)[1], 3, writePosNow);
				}
			}

			// Or, all other, non-balanced input types
			else {
				for (auto [rxL, rxR] : input.first(numSamplesThisCycle)) {
					if (applyGain) {
						rxL = lshiftAndSaturateUnknown(rxL, gainToApply);
					}

					// Copy the last 24 bits (lower 3 bytes) to writePosNow
					writePosNow = std::copy_n(&reinterpret_cast<std::byte*>(&rxL)[1], 3, writePosNow);

					recordMax = std::max(rxL, recordMax);
					recordMin = std::min(rxL, recordMin);

					q31_t absL = (rxL >= 0) ? rxL : -1 - rxL;
					recordSumL += absL;

					if (rxL < recordPeakL) {
						recordPeakL = rxL;
					}
					else if (-rxL < recordPeakL) {
						recordPeakL = -rxL;
					}
					if (rxL == std::numeric_limits<q31_t>::max() || rxL == std::numeric_limits<q31_t>::min()) {
						recordingClippedRecently = true;
					}

					// recording stereo
					if (recordingNumChannels == 2) {
						if (applyGain) {
							rxR = lshiftAndSaturateUnknown(rxR, gainToApply);
						}

						// Copy the last 24 bits (lower 3 bytes) to writePosNow
						writePosNow = std::copy_n(&reinterpret_cast<std::byte*>(&rxR)[1], 3, writePosNow);

						recordMax = std::max(rxR, recordMax);
						recordMin = std::min(rxR, recordMin);

						recordSumR += (rxR >= 0) ? rxR : -1 - rxR;

						int32_t lPlusR = (rxL >> 1) + (rxR >> 1);
						recordSumLPlusR += (lPlusR >= 0) ? lPlusR : -1 - lPlusR;

						int32_t lMinusR = (rxL >> 1) - (rxR >> 1);
						recordSumLMinusR += (lMinusR >= 0) ? lMinusR : -1 - lMinusR;

						if (rxR < recordPeakR) {
							recordPeakR = rxR;
						}
						else if (-rxR < recordPeakR) {
							recordPeakR = -rxR;
						}
						if (rxR == std::numeric_limits<q31_t>::max() || rxR == std::numeric_limits<q31_t>::min()) {
							recordingClippedRecently = true;
						}

						if (lMinusR < recordPeakLMinusR) {
							recordPeakLMinusR = lMinusR;
						}
						else if (-lMinusR < recordPeakLMinusR) {
							recordPeakLMinusR = -lMinusR;
						}
					}
				}
			}
			writePos = reinterpret_cast<char*>(writePosNow);
			numSamplesCaptured += numSamplesThisCycle;

			// if we're threshold recording and didn't detect audio in previous cycles
			// check if there's any audio in this cycle
			if (thresholdRecording) [[unlikely]] {
				size_t samples_to_copy = minThresholdMargin;
				StereoFloatSample approxRMSLevel = envelopeFollower.calcApproxRMS(input);
				if (std::max(approxRMSLevel.l, approxRMSLevel.r) > startValueThreshold) {
					thresholdRecording = false;
					samples_to_copy += numSamplesThisCycle;
				}

				// is this overkill? yes, we could  let the cluster fill up before doing the copy
				// is there a downside? no - threshold recording only occurs when a single audio clip is being recorded
				// or the standalone sampler is being used so whatever this is simple
				if (numSamplesCaptured > minThresholdMargin) {
					auto* endpos = (char*)writePosNow;
					ptrdiff_t num_bytes = samples_to_copy * 3 * recordingNumChannels;
					char* audio_start_pos = endpos - num_bytes;
					char* cluster_start_pos =
					    reinterpret_cast<char*>(currentRecordBuffer + sample->audioDataStartPosBytes);
					if (audio_start_pos > cluster_start_pos) {
						memcpy(cluster_start_pos, audio_start_pos, num_bytes);
						writePos = cluster_start_pos + num_bytes;
						numSamplesCaptured = samples_to_copy;
					}
				}
			}

			// update our input view to exclude the chunk we just processed
			input = input.subspan(numSamplesThisCycle);
		}

		numSamplesBeenRunning += numSamplesThisCycle;

		numSamples -= numSamplesThisCycle;
	} while (numSamples > 0);
}

void SampleRecorder::endSyncedRecording(int32_t buttonLatencyForTempolessRecording) {
#if ALPHA_OR_BETA_VERSION
	// RELAXED: audio-thread-owned debug assertions (this function only runs on the audio side, before the
	// release hand-off in finishCapturing()/abort()).
	if (status.load(std::memory_order_relaxed) == RecorderStatus::CAPTURING_DATA_WAITING_TO_STOP) {
		FREEZE_WITH_ERROR("E272");
	}
	else if (status.load(std::memory_order_relaxed) == RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
		FREEZE_WITH_ERROR("E288");
	}
	else if (status.load(std::memory_order_relaxed) == RecorderStatus::COMPLETE) {
		FREEZE_WITH_ERROR("E289");
	}
	else if (status.load(std::memory_order_relaxed) == RecorderStatus::ABORTED) {
		FREEZE_WITH_ERROR("E290");
	}
	else if (status.load(std::memory_order_relaxed) == RecorderStatus::AWAITING_DELETION) {
		FREEZE_WITH_ERROR("E291");
	}
#endif

	if (numSamplesCaptured) {
		int32_t numMoreSamplesTilEndLoopPoint =
		    numSamplesExtraToCaptureAtEndSyncingWise - buttonLatencyForTempolessRecording;
		int32_t numMoreSamplesToCapture = numMoreSamplesTilEndLoopPoint;

		D_PRINTLN("buttonLatencyForTempolessRecording:  %d", buttonLatencyForTempolessRecording);

		if (recordingExtraMargins) {
			numMoreSamplesToCapture += kAudioClipMarginSizePostEnd; // Means we also have an audioClip
		}

		uint32_t loopEndPointSamples = numSamplesCaptured + numMoreSamplesTilEndLoopPoint;

		totalSampleLengthNowKnown(numSamplesCaptured + numMoreSamplesToCapture, loopEndPointSamples);

		if (numMoreSamplesToCapture <= 0) {
			if (numMoreSamplesToCapture < 0) {
				capturedTooMuch = true;
				D_PRINTLN("captured too much.");
			}
			finishCapturing();
		}
		else {
			// RELAXED: audio-thread-owned write (pre-hand-off).
			status.store(RecorderStatus::CAPTURING_DATA_WAITING_TO_STOP, std::memory_order_relaxed);
		}
	}
	else {
		// if we haven't captured any samples (which can happen with threshold recording), abort
		abort();
	}
}

void SampleRecorder::totalSampleLengthNowKnown(uint32_t totalLengthSamples, uint32_t loopEndPointSamples) {

	sample->lengthInSamples = totalLengthSamples;
	sample->audioDataLengthBytes = totalLengthSamples * sample->byteDepth * sample->numChannels;
	// when stem recording is done, we want to update the sample loop end position in the sample holder
	// so that that loop end marker is available right away if you want to load that sample into a kit
	if (stemExport.writeLoopEndPos()) {
		sample->fileLoopEndSamples = stemExport.loopEndPointInSamplesForAudioFile;
	}
	else {
		sample->fileLoopEndSamples = loopEndPointSamples;
	}

	// If we haven't written the first buffer yet, quick - update it with the actual length. bufferTable_[0]
	// is still ours (not yet flushed/recycled), so patch it in place -- the (now-correct) header goes to
	// disk when this buffer is flushed normally, with nothing left to repatch at finalize.
	if (firstUnwrittenClusterIndex == 0) {
		if (ALPHA_OR_BETA_VERSION && (bufferTable_.size() == 0 || bufferTable_[0] == nullptr)) {
			FREEZE_WITH_ERROR("E274");
		}

		audioDataLengthBytesAsWrittenToFile = sample->audioDataLengthBytes;
		loopEndSampleAsWrittenToFile = sample->fileLoopEndSamples; // Even if we're not actually writing loop points
		                                                           // to the file, this is harmless
		updateDataLengthInHeader(std::span<std::byte>(bufferTable_[0], Cluster::size));
	}
}

bool SampleRecorder::inputLooksDifferential() {
	return (recordSumLPlusR < (recordSumL >> 4));
}

bool SampleRecorder::inputHasNoRightChannel() {
	return (recordSumR < (recordSumL >> 6));
}

namespace {
/// @brief Reconstruct the int32 value the finalize batch transform operates on from a
///        stored 24-bit little-endian sample: bytes [0,1,2] of @p src become bits [8,31] of the
///        result, with bits [0,7] always 0.
int32_t read24AsShiftedInt32(const std::byte* src) {
	uint32_t b0 = static_cast<uint32_t>(src[0]);
	uint32_t b1 = static_cast<uint32_t>(src[1]);
	uint32_t b2 = static_cast<uint32_t>(src[2]);
	return static_cast<int32_t>((b0 | (b1 << 8) | (b2 << 16)) << 8);
}

/// @brief Inverse of the above: write bytes [1,2,3] of @p processed (its top 3 bytes, little-endian)
///        to @p dst as a 3-byte frame -- the exact bytes the batch transform's normalize/downmix gain
///        produces per output frame.
void writeShiftedInt32As24(int32_t processed, std::byte* dst) {
	uint32_t u = static_cast<uint32_t>(processed);
	dst[0] = static_cast<std::byte>((u >> 8) & 0xFF);
	dst[1] = static_cast<std::byte>((u >> 16) & 0xFF);
	dst[2] = static_cast<std::byte>((u >> 24) & 0xFF);
}
} // namespace

constexpr uint32_t kAlterBatchFrames = 256;

uint32_t SampleRecorder::alterBatchFrames() const {
	const uint32_t remaining = alterNumFrames_ - alterFramesDone_;
	return remaining < kAlterBatchFrames ? remaining : kAlterBatchFrames;
}

void SampleRecorder::transformBatchInPlace(uint32_t batchFrames) {
	for (uint32_t i = 0; i < batchFrames; i++) {
		const std::byte* in = alterBatchBuf_.data() + static_cast<size_t>(i) * alterInputFrameBytes_;

		int32_t value = read24AsShiftedInt32(in);
		if (finalizeAction_ == MonitoringAction::SUBTRACT_RIGHT_CHANNEL) {
			int32_t right = read24AsShiftedInt32(in + 3);
			value = (value >> 1) - (right >> 1);
		}
		// REMOVE_RIGHT_CHANNEL reads only the left channel above and discards the right (in + 3)
		// entirely; NONE never has a right channel to begin with (mono input).

		int32_t processed = value << finalizeLshift_;
		// Writing at i*3 while reading at i*alterInputFrameBytes_: see alterBatchBuf_'s comment for
		// why this cannot clobber a frame a later iteration has yet to read.
		writeShiftedInt32As24(processed, alterBatchBuf_.data() + static_cast<size_t>(i) * 3);
	}
}
