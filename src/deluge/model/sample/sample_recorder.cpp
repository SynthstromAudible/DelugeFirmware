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

SampleRecorder::~SampleRecorder() {
	D_PRINTLN("~SampleRecorder()");
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

	// Off the owner, only the steady-state cluster write may run here -- everything else blocks, so
	// send that tick to the owner and come back. One dispatch per recording for the create, one for
	// the finalize; the writes in between, which are the whole length of the recording, stay here.
	if (!deluge::storage::Owner::on_owner() && tickNeedsOwner() && AudioEngine::requestRecorderTickOnOwner(this)) {
		return Error::NONE;
	}

	// If aborted, delete the file.
	// ACQUIRE: synchronizes-with abort()'s release store.
	if (status.load(std::memory_order_acquire) == RecorderStatus::ABORTED) {

aborted:
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

		// Delete the file if one was created
		if (!filePathCreated.empty()) {

			// Flush and close the persistent write context BEFORE deleting the file. `file` is
			// write-through for cluster data but defers the directory-entry
			// (size/first_cluster/mtime) update to close()/flush_context -- if we deleted the file
			// first, the unlink below would see a stale (near-empty) directory entry with
			// first_cluster still ~0 and free no clusters (an orphaned-cluster leak), and a same-name
			// file that later recycles this freed short-name slot (see the counter tick-back below)
			// could have this now-dangling handle's deferred flush land on ITS entry instead. reset()
			// here first makes the flush observe the file still on disk, so it writes a CONSISTENT
			// entry; the unlink then frees the real chain and no dirty handle survives into the next
			// recording. reset() on an already-disengaged optional (an abort before the file was ever
			// opened) is a safe no-op.
			this->file.reset();

			deluge_file_invalidate_cache();
			bool unlinked = deluge::io::unlink(filePathCreated).has_value();

			// If this was the most recent recording in this category, tick the counter backwards - so long as
			// either the delete was successful or it was for an AudioClip, which means the file is in the TEMP folder
			// and can be overwritten anyway
			if (unlinked || folderID == AudioRecordingFolder::CLIPS) {
				if (audioFileManager.highestUsedAudioRecordingNumber[util::to_underlying(folderID)]
				    == audioFileNumber) {
					audioFileManager.highestUsedAudioRecordingNumber[util::to_underlying(folderID)]--;
					D_PRINTLN("ticked file counter backwards");
				}
			}
			filePathCreated.clear();
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

		// If file not created yet, do that
		if (filePathCreated.empty()) {

			error = StorageManager::initSD();
			if (error != Error::NONE) {
				goto gotError;
			}

			// Check there's space on the card
			error = StorageManager::checkSpaceOnCard();
			if (error != Error::NONE) {
				goto gotError;
			}

			std::string filePath;
			std::string tempFilePathForRecording;

			// Note: we couldn't pass the actual Sample pointer into this function, cos the Sample might get destructed
			// during the card access! (Though probably not anymore right?)
			// Recording could finish or abort during this!
			if (stemExport.processStarted) {
				error = stemExport.getUnusedStemRecordingFilePath(&filePath, folderID);
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
				error = audioFileManager.getUnusedAudioRecordingFilePath(filePath, &tempFilePathForRecording, folderID,
				                                                         &audioFileNumber, name, &currentSong->name);
			}
			// ACQUIRE: cardRoutine() decision read.
			if (status.load(std::memory_order_acquire) == RecorderStatus::ABORTED) {
				goto aborted; // In case aborted during
			}
			if (error != Error::NONE) {
				goto gotError;
			}

			bool mayOverwrite = true;

			// Now store our own copy of the actually (possibly temp) filename
			if (!tempFilePathForRecording.empty()) {
				filePathCreated = tempFilePathForRecording.c_str(); // Can't fail!
			}
			else {
				filePathCreated = filePath.c_str(); // Can't fail!
				mayOverwrite = false;
			}

			// Recording could finish or abort during this!
			DelugeStreamMode streamOpenMode =
			    mayOverwrite ? DELUGE_STREAM_WRITE_CREATE : DELUGE_STREAM_WRITE_CREATE_NEW;
			bool triedCreatingRecordingFolder = false;

tryOpenRecordingStream:
			// Bypasses the Tier 2 locator cache (same as the raw FatFS open createFileRaw used to do)
			// -- must invalidate it, or a stale cached locator for this exact path (from an earlier
			// directory listing) could survive a mayOverwrite create-always reopen and feed a later
			// cache-hit read the pre-overwrite file's stale sclust/objsize.
			deluge_file_invalidate_cache();
			auto openedStream = deluge::io::Stream::open(filePathCreated, streamOpenMode);
			if (!openedStream && openedStream.error() == deluge::io::Status::NOT_FOUND) {
				// deluge::io::Stream::open (unlike the old createFileRaw, and unlike the portable
				// deluge::io layer's createFile()) does no folder-creation retry of its own -- but the
				// containing folder legitimately might not exist yet (AudioClip's CLIPS/TEMP
				// subfolder, or a per-song recording subfolder under RECORD/RESAMPLE -- see
				// AudioFileManager::getUnusedAudioRecordingFilePath). Replicate createFileRaw's
				// create-parent-and-retry behaviour here, one folder level at a time.
				if (triedCreatingRecordingFolder) {
					filePathCreated.clear();
					goto gotError;
				}
				triedCreatingRecordingFolder = true;

				std::string folderPath = filePathCreated;

cutRecordingFolderPathAndTryCreating:
				size_t slashPos = folderPath.find_last_of('/');
				if (slashPos == std::string::npos) {
					filePathCreated.clear();
					goto gotError;
				}
				folderPath.resize(slashPos);

				auto madeDir = deluge::io::mkdir(folderPath.c_str());
				if (madeDir) {
					goto tryOpenRecordingStream;
				}
				else if (madeDir.error() == deluge::io::Status::NOT_FOUND) {
					triedCreatingRecordingFolder = false; // Let it try multiple levels again
					goto cutRecordingFolderPathAndTryCreating;
				}
				else {
					filePathCreated.clear();
					goto gotError;
				}
			}
			if (!openedStream) {
				filePathCreated.clear();
				goto gotError;
			}
			else {
				this->file = std::move(openedStream.value());
			}

			// ACQUIRE: cardRoutine() decision read.
			if (status.load(std::memory_order_acquire) == RecorderStatus::ABORTED) {
				goto aborted; // In case aborted during
			}

			// Ok, the Sample still exists.
			sample->filePath = filePath;                                 // Can't fail!
			sample->tempFilePathForRecording = tempFilePathForRecording; // Can't fail!

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

Error SampleRecorder::writeAnyCompletedClusters() {
	while (firstUnwrittenClusterIndex < currentRecordClusterIndex.load(std::memory_order_acquire)) {

		Error error = writeOneCompletedCluster(/*mayUseChannel=*/false);

		// On error, just return -- the buffer writeOneCompletedCluster() couldn't flush stays put in
		// bufferTable_ (never recycled) rather than being freed here.
		if (error != Error::NONE) {
			return error;
		}

		// Every caller of this function reaches it from finalizeRecordedFile(), which only ever runs
		// on the storage owner -- so writeCluster() took its synchronous arm and the cluster is
		// genuinely on the card. If that stopped being true this loop would spin without progress,
		// writing nothing, so say so rather than trust it.
		if (writePending()) {
			return Error::SD_CARD;
		}
	}

	return Error::NONE;
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
			// Finalize: this cluster must be on the card before the header patch-back below reads the file.
			Error error = writeCluster(currentRecordClusterIndex.load(std::memory_order_relaxed), bytesToWrite,
			                           /*mayUseChannel=*/false);
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

/// The alteration, still synchronous. The next change turns this into phases of its own.
Error SampleRecorder::finalizeRunAlterationSynchronously() {
	if (file) {
		auto closeResult = this->file->close();
		this->file.reset();
		if (!closeResult) {
			return Error::SD_CARD;
		}
	}
	return alterFile(finalizeAction_, finalizeLshift_, finalizeIdealSizeBefore_, finalizeDataLenAfter_);
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

SampleRecorder::FinalizeStep SampleRecorder::advanceFinalize() {
	using deluge::storage::FinalizeInputs;
	using deluge::storage::FinalizePhase;
	using deluge::storage::nextFinalizePhase;

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
	const bool finalizeMayUseChannel =
	    !deluge::storage::Owner::on_owner() && deluge::storage::Owner::started() && deluge_file_write_async_active();

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
			if (finalizeMayUseChannel && file->request_truncate(finalizeTruncateTo_)) {
				finalizeIssued_ = true;
				return FinalizeStep::Pending;
			}
			// Refused -- the channel is off, or busy with another operation. Do it here. Every phase
			// below keeps this arm for the same reason: a refusal must never fail a recording, and
			// this is the path that shipped before the channel existed.
			if (!file->truncate(finalizeTruncateTo_)) {
				return FinalizeStep::Failed;
			}
			break;

		case FinalizePhase::HeaderRead: {
			if (!file) {
				return FinalizeStep::Failed;
			}
			std::span<std::byte> dst(headerScratch_.data(), kHeaderSectorBytes);
			if (finalizeMayUseChannel && file->request_read_at(0, dst)) {
				finalizeIssued_ = true;
				return FinalizeStep::Pending;
			}
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
			if (finalizeMayUseChannel && file->request_write_at(0, std::span<const std::byte>(headerBytes))) {
				finalizeIssued_ = true;
				return FinalizeStep::Pending;
			}
			// The old path ignored a failed header write here ("that's a shame, but we don't need to
			// do anything"), and that judgement is preserved: the audio is on the card either way.
			(void)file->write_at(0, std::span<const std::byte>(headerBytes));
			break;
		}

		case FinalizePhase::Close: {
			if (!file) {
				break; // Already closed by the alteration path; nothing to wait for.
			}
			if (finalizeMayUseChannel && file->request_close()) {
				finalizeIssued_ = true;
				return FinalizeStep::Pending;
			}
			auto closeResult = file->close();
			file.reset();
			if (!closeResult) {
				return FinalizeStep::Failed;
			}
			break;
		}

		case FinalizePhase::CloseBeforeAlter:
		case FinalizePhase::OpenForAlter:
		case FinalizePhase::BatchRead:
		case FinalizePhase::BatchWrite:
			// The alteration path is still the old synchronous one; the next change converts it.
			// Running it here keeps this step's diff to the branch every audio-clip recording takes.
			if (finalizeRunAlterationSynchronously() != Error::NONE) {
				return FinalizeStep::Failed;
			}
			finalizePhase_ = FinalizePhase::Bookkeeping;
			return FinalizeStep::Pending;

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
			return FinalizeStep::Pending;
		}
	}

	const FinalizePhase next = nextFinalizePhase(FinalizeInputs{
	    finalizePhase_,
	    collected,
	    firstUnwrittenClusterIndex < currentRecordClusterIndex.load(std::memory_order_acquire),
	    currentRecordBuffer != nullptr,
	    finalizeAltering_,
	    false, // framesRemain: the alteration path does not use the machine yet
	    finalizeNeedTruncate_,
	    finalizeNeedHeaderPatch_,
	});
	if (next != finalizePhase_) {
		finalizeIssued_ = false;
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

bool SampleRecorder::tickNeedsOwner() const {
	const RecorderStatus st = status.load(std::memory_order_acquire);
	// The abort cleanup closes the stream and unlinks the file; the error path is about to do the
	// same; and with no file yet this tick runs initSD, checkSpaceOnCard and Stream::open.
	if (st == RecorderStatus::ABORTED || hadCardError || filePathCreated.empty()) {
		return true;
	}
	// Finalize -- but only once the clusters are drained. While there are still completed clusters
	// to write, this tick is a steady-state write like any other and belongs on the channel.
	if (st == RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING
	    && firstUnwrittenClusterIndex >= currentRecordClusterIndex.load(std::memory_order_acquire)) {
		return true;
	}
	return false;
}

bool SampleRecorder::pumpInFlightWrite() {
	if (inFlightClusterIndex_ < 0) {
		return false;
	}

	const int32_t index = inFlightClusterIndex_;
	const uint32_t expected = inFlightNumBytes_;
	uint32_t written = 0;
	const uint8_t collected = file->collect_write_at(&written);

	switch (
	    deluge::storage::nextWriteStep(/*channelActive=*/true, /*haveInFlight=*/true, collected, writePendingTicks_)) {
	case deluge::storage::WriteStep::Wait:
		writePendingTicks_++;
		return true; // Still outstanding: nothing that could free this buffer may run.

	case deluge::storage::WriteStep::Complete:
		inFlightClusterIndex_ = -1;
		writePendingTicks_ = 0;
		if (written != expected) {
			hadCardError = true; // A short write is a card error, exactly as the synchronous arm says.
		}
		else {
			finishCluster(index);
		}
		return false;

	case deluge::storage::WriteStep::Failed:
		inFlightClusterIndex_ = -1;
		writePendingTicks_ = 0;
		hadCardError = true;
		return false;

	case deluge::storage::WriteStep::Issue:
	case deluge::storage::WriteStep::Inline:
		// Either the channel never keyed the request (UNKNOWN) or it accepted one and stopped
		// answering (the stall bound). Both mean these bytes are NOT on the card, so write them here
		// and stop trusting the channel for the rest of this recording -- a driver that stalled once
		// would otherwise cost a full stall bound per cluster.
		inFlightClusterIndex_ = -1;
		writePendingTicks_ = 0;
		channelAbandoned_ = true;
		if (writeClusterSynchronously(index, expected) != Error::NONE) {
			hadCardError = true;
		}
		return false;
	}
	return false;
}

Error SampleRecorder::writeClusterSynchronously(int32_t clusterIndex, size_t numBytes) {
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
	// Deliberately NOT gated on `!on_owner()`, unlike the byte channel's selector. That term exists
	// to keep the INTERACTION tier off the card, and this cluster has no tier problem to solve --
	// every one of its sites was measured on the owner before this rung. Gating on it here would
	// mean the write keeps blocking the owner wherever it already runs, which is exactly the thing
	// that stops the owner ever being removable. `started()` stays: it keeps anything running before
	// the worker exists on the arm it has always used.
	const bool channelActive =
	    mayUseChannel && !channelAbandoned_ && deluge::storage::Owner::started() && deluge_file_write_async_active();

	if (channelActive && inFlightClusterIndex_ < 0) {
		const uint32_t byteOffset = static_cast<uint32_t>(clusterIndex) << Cluster::size_magnitude;
		if (file->request_write_at(byteOffset, std::span<const std::byte>(bufferTable_[clusterIndex], numBytes))) {
			inFlightClusterIndex_ = clusterIndex;
			inFlightNumBytes_ = static_cast<uint32_t>(numBytes);
			writePendingTicks_ = 0;
			g_writeChannelServed++;
			// Issued, not written. `pumpInFlightWrite()` collects it on a later tick and recycles the
			// buffer then; recycling here would hand the card a buffer the audio thread has reused.
			return Error::NONE;
		}
		// Refused. Fall through rather than dropping the cluster: a refused write that is not
		// retried is lost audio.
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
/// @brief Reconstruct the int32 value `alterFile()`'s per-frame transform operates on from a
///        stored 24-bit little-endian sample: bytes [0,1,2] of @p src become bits [8,31] of the
///        result, with bits [0,7] always 0.
int32_t read24AsShiftedInt32(const std::byte* src) {
	uint32_t b0 = static_cast<uint32_t>(src[0]);
	uint32_t b1 = static_cast<uint32_t>(src[1]);
	uint32_t b2 = static_cast<uint32_t>(src[2]);
	return static_cast<int32_t>((b0 | (b1 << 8) | (b2 << 16)) << 8);
}

/// @brief Inverse of the above: write bytes [1,2,3] of @p processed (its top 3 bytes, little-endian)
///        to @p dst as a 3-byte frame -- the exact bytes `alterFile()`'s normalize/downmix gain
///        produces per output frame.
void writeShiftedInt32As24(int32_t processed, std::byte* dst) {
	uint32_t u = static_cast<uint32_t>(processed);
	dst[0] = static_cast<std::byte>((u >> 8) & 0xFF);
	dst[1] = static_cast<std::byte>((u >> 16) & 0xFF);
	dst[2] = static_cast<std::byte>((u >> 24) & 0xFF);
}
} // namespace

Error SampleRecorder::alterFile(MonitoringAction action, int32_t lshiftAmount, uint32_t idealFileSizeBeforeAction,
                                uint64_t dataLengthAfterAction) {

	D_PRINTLN("altering file");

	// This function's SD writes go through a persistent write context -- opened here, at
	// the top, and held open for the WHOLE alteration. Every write below reuses it via
	// Stream::write_at, and it's closed exactly once: either by the end-of-alteration truncate
	// block, or, if that branch doesn't run (no truncation was needed), right before this function
	// returns. The recorder is efatfs-only.
	//
	// This whole function is a positional file->file transform over two independent
	// byte cursors on the SAME open stream -- an input read cursor and an output write cursor, both
	// starting at `audioDataStartPosBytes`. Output is never longer than input (mono output frames are
	// <= the mono/stereo input frames they're derived from), so in-place positional writes never
	// clobber input the read cursor hasn't reached yet -- no cluster residency, no get_cluster/
	// num_reasons bookkeeping, and no cluster-straddle "extra bytes" juggling: positional
	// `read_at_via`/`write_at` read and write any byte range directly, so there's no 32768-byte
	// window to straddle in the first place.
	auto openedStream = deluge::io::Stream::open(sample->filePath, DELUGE_STREAM_WRITE_APPEND);
	if (!openedStream) {
		return Error::SD_CARD;
	}
	this->file = std::move(openedStream.value());

	// Header fixups: read the header back through this SAME write context, patch it in a small
	// scratch buffer, and write it straight back. `audioDataStartPosBytes` is always 44 or 112 (see
	// setup()), well within this buffer.
	std::array<std::byte, 128> headerScratch{};
	if (ALPHA_OR_BETA_VERSION && sample->audioDataStartPosBytes > headerScratch.size()) {
		FREEZE_WITH_ERROR("E286");
	}
	std::span<std::byte> headerBuf(headerScratch.data(), sample->audioDataStartPosBytes);
	auto headerReadResult = file->read_at_via(0, headerBuf);
	if (!headerReadResult || *headerReadResult != headerBuf.size()) {
		return Error::SD_CARD;
	}

	audioDataLengthBytesAsWrittenToFile = dataLengthAfterAction;
	loopEndSampleAsWrittenToFile = sample->fileLoopEndSamples;
	updateDataLengthInHeader(headerBuf);

	if (action != MonitoringAction::NONE) {
		// Write num channels
		uint16_t data16 = 1;
		memcpy(headerBuf.data() + 22, &data16, 2);

		// Data rate
		uint32_t data32 = kSampleRate * 1 * 3;
		memcpy(headerBuf.data() + 28, &data32, 4);

		// Data block size
		data16 = 1 * 3;
		memcpy(headerBuf.data() + 32, &data16, 2);
	}

	auto headerWriteResult = file->write_at(0, headerBuf);
	if (!headerWriteResult || *headerWriteResult != headerBuf.size()) {
		return Error::SD_CARD;
	}

	// Input region: `idealFileSizeBeforeAction` total bytes from the start of the file, i.e.
	// `idealFileSizeBeforeAction - audioDataStartPosBytes` audio bytes, in frames of
	// `inputFrameBytes` each (mono in for NONE, stereo in for the two channel-combining actions).
	const uint32_t inputFrameBytes = (action == MonitoringAction::NONE) ? 3 : 6;
	const uint32_t audioInputBytes = idealFileSizeBeforeAction - sample->audioDataStartPosBytes;
	const uint32_t numFrames = audioInputBytes / inputFrameBytes;

	// Process in frame-aligned batches -- big enough to make the SD traffic efficient, small enough
	// to keep the periodic cooperative yield (every 256 frames, as before) meaningful.
	constexpr uint32_t kBatchFrames = 256;
	std::array<std::byte, kBatchFrames * 6> inputScratch{};
	std::array<std::byte, kBatchFrames * 3> outputScratch{};

	uint32_t readCursor = sample->audioDataStartPosBytes;
	uint32_t writeCursor = sample->audioDataStartPosBytes;
	uint32_t framesDone = 0;

	while (framesDone < numFrames) {
		AudioEngine::routineWithClusterLoading();
		uiTimerManager.routine();
		deluge_control_flush();

		uint32_t batchFrames = std::min(kBatchFrames, numFrames - framesDone);
		uint32_t inputBytesThisBatch = batchFrames * inputFrameBytes;
		uint32_t outputBytesThisBatch = batchFrames * 3;

		auto readResult = file->read_at_via(readCursor, std::span<std::byte>(inputScratch.data(), inputBytesThisBatch));
		if (!readResult || *readResult != inputBytesThisBatch) {
			return Error::SD_CARD;
		}

		for (uint32_t i = 0; i < batchFrames; i++) {
			const std::byte* in = inputScratch.data() + static_cast<size_t>(i) * inputFrameBytes;

			int32_t value = read24AsShiftedInt32(in);
			if (action == MonitoringAction::SUBTRACT_RIGHT_CHANNEL) {
				int32_t right = read24AsShiftedInt32(in + 3);
				value = (value >> 1) - (right >> 1);
			}
			// REMOVE_RIGHT_CHANNEL reads only the left channel above and discards the right (in + 3)
			// entirely; NONE never has a right channel to begin with (mono input).

			int32_t processed = value << lshiftAmount;
			writeShiftedInt32As24(processed, outputScratch.data() + static_cast<size_t>(i) * 3);
		}

		auto writeResult =
		    file->write_at(writeCursor, std::span<const std::byte>(outputScratch.data(), outputBytesThisBatch));
		if (!writeResult || *writeResult != outputBytesThisBatch) {
			return Error::SD_CARD;
		}

		readCursor += inputBytesThisBatch;
		writeCursor += outputBytesThisBatch;
		framesDone += batchFrames;
	}

	if (action != MonitoringAction::NONE || capturedTooMuch) {

		// `this->file` has been open -- receiving every write_at call above -- for the whole
		// alteration, opened at the top of this function, so there's nothing to reopen here.

		Error error = truncateFileDownToSize(dataLengthAfterAction + sample->audioDataStartPosBytes);
		if (error != Error::NONE) {
			return error;
		}

		auto closeResult = this->file->close();
		this->file.reset();
		if (!closeResult) {
			return Error::SD_CARD;
		}
	}

	// The write context (opened at the top of this function) is closed exactly once. The
	// truncate branch above already closed it (and reset `this->file`) whenever it ran; if it didn't
	// run -- no truncation was needed -- close it here.
	if (this->file) {
		auto closeResult = this->file->close();
		this->file.reset();
		if (!closeResult) {
			return Error::SD_CARD;
		}
	}

	return Error::NONE;
}

// You must still have the file open when you call this
Error SampleRecorder::truncateFileDownToSize(uint32_t newFileSize) {

	// Update the Sample object to indicate the correct size. Do this before we risk errors below

	uint64_t numClustersAfterAction = ((newFileSize - 1) >> Cluster::size_magnitude) + 1;

	// Guard on the PHYSICAL cache size, not num_clusters(): resizeOverviewCache() shrinks the actual
	// overviewCache_ storage, so the "am I really shrinking?" test must observe overviewCacheSize().
	// num_clusters() is derived and doesn't track the cache's physical size, so comparing against it
	// would mis-gate this shrink (matching the finalize-grow guard in the same file).
	if (numClustersAfterAction < sample->overviewCacheSize()) {
		sample->resizeOverviewCache(numClustersAfterAction);
	}

	auto truncateResult = file->truncate(newFileSize);
	if (!truncateResult) {
		return Error::SD_CARD;
	}

	return Error::NONE;
}
