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
#include "definitions_cxx.hpp"
#include "drivers/pic/pic.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/root_ui.h"
#include "gui/ui_timer_manager.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/sample/sample.h"
#include "processing/engines/audio_engine.h"
#include "processing/stem_export/stem_export.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include <new>

extern "C" {
#include "drivers/ssi/ssi.h"
#include "fatfs/diskio.h"

LBA_t clst2sect(           /* !=0:Sector number, 0:Failed (invalid cluster#) */
                FATFS* fs, /* Filesystem object */
                DWORD clst /* Cluster# to be converted */
);
}

extern "C" void routineForSD(void);

extern uint8_t currentlyAccessingCard;

#define MAX_FILE_SIZE_MAGNITUDE 32

SampleRecorder::~SampleRecorder() {
	D_PRINTLN("~SampleRecorder()");
	if (sample != nullptr) {
		detachSample();
	}
}

// This can be called when this SampleRecorder is destructed routinely - or earlier if we've aborted and the sample file
// is being deleted IMPORTANT!!!! You have to set sample to NULL after calling this, if not destructing
void SampleRecorder::detachSample() {

	// If we were holding onto the reasons for the first couple of Clusters, release them now
	if (keepingReasonsForFirstClusters) {
		int32_t numClustersToRemoveFor = std::min(kNumClustersLoadedAhead, sample->clusters.getNumElements());
		numClustersToRemoveFor = std::min(numClustersToRemoveFor, firstUnwrittenClusterIndex);

		for (int32_t l = 0; l < numClustersToRemoveFor; l++) {
			Cluster* cluster = sample->clusters.getElement(l)->cluster;

			// Some bug-hunting
			if (!cluster->numReasonsHeldBySampleRecorder) {
				FREEZE_WITH_ERROR("E345");
			}
			cluster->numReasonsHeldBySampleRecorder--;

			audioFileManager.removeReasonFromCluster(cluster, "E257");
		}
	}

	int32_t removeForClustersUntilIndex = currentRecordClusterIndex;
	if (currentRecordCluster) {
		removeForClustersUntilIndex++; // If there's a currentRecordCluster (usually will be if aborting), need to
		                               // remove its "reason" too
	}

	while (firstUnwrittenClusterIndex < removeForClustersUntilIndex) {
		Cluster* cluster = sample->clusters.getElement(firstUnwrittenClusterIndex)->cluster;

		if (!cluster) {
			FREEZE_WITH_ERROR("E363");
		}

		// Some bug-hunting
		if (!cluster->numReasonsHeldBySampleRecorder) {
			FREEZE_WITH_ERROR("E346");
		}
		cluster->numReasonsHeldBySampleRecorder--;

		audioFileManager.removeReasonFromCluster(cluster, "E249");
		firstUnwrittenClusterIndex++;
	}

	sample->removeReason("E400");
}

Error SampleRecorder::setup(int32_t newNumChannels, AudioInputChannel newMode, bool newKeepingReasons,
                            bool shouldRecordExtraMargins, AudioRecordingFolder newFolderID,
                            int32_t buttonPressLatency) {

	if (!audioFileManager.ensureEnoughMemoryForOneMoreAudioFile()) {
		return Error::INSUFFICIENT_RAM;
	}

	keepingReasonsForFirstClusters = newKeepingReasons;
	recordingExtraMargins = shouldRecordExtraMargins;
	folderID = newFolderID;

	// Didn't seem to make a difference forcing this into local RAM
	void* sampleMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(Sample));
	if (!sampleMemory) {
		return Error::INSUFFICIENT_RAM;
	}

	sample = new (sampleMemory) Sample;
	sample->addReason(); // Must call this so it's protected from stealing, before we call initialize().
	Error error = sample->initialize(1);
	if (error != Error::NONE) {
gotError:
		sample->~Sample();
		delugeDealloc(sampleMemory);
		return error;
	}

	currentRecordCluster =
	    sample->clusters.getElement(0)->getCluster(sample, 0, CLUSTER_DONT_LOAD); // Adds a "reason" to it, too
	if (!currentRecordCluster) {
		error = Error::INSUFFICIENT_RAM;
		goto gotError;
	}

	// Bug hunting - newly gotten Cluster
	if (currentRecordCluster->numReasonsHeldBySampleRecorder) {
		FREEZE_WITH_ERROR("E360");
	}
	currentRecordCluster->numReasonsHeldBySampleRecorder++;

	// Give the sample some stuff
	sample->audioDataStartPosBytes = recordingExtraMargins ? 112 : 44;
	sample->byteDepth = 3;
	sample->numChannels = newNumChannels;
	sample->lengthInSamples = 0x8FFFFFFFFFFFFFFF;
	sample->audioDataLengthBytes =
	    0x8FFFFFFFFFFFFFFF; // If you ever change this value, update the check for it in SampleManager::loadCluster()
	sample->sampleRate = kSampleRate;
	sample->workOutBitMask();

	currentRecordCluster->loaded =
	    true; // I think this is ok - mark it as loaded even though we're yet to record into it

	pointerHeldElsewhere = true;
	mode = newMode;
	currentRecordClusterIndex = 0;

	numSamplesToRunBeforeBeginningCapturing = numSamplesExtraToCaptureAtEndSyncingWise =
	    (mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) ? kAudioRecordLagCompensation : 0;

	// Apart from the MIX option, all other audio sources are fed to us during the "outputting" routine. Occasionally,
	// there'll be some more of that going to happen for the previous render, so we have to compensate for that
	if (mode != AudioInputChannel::MIX) {
		numSamplesToRunBeforeBeginningCapturing += AudioEngine::getNumSamplesLeftToOutputFromPreviousRender();
	}

	// External sources
	if (mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {

		sourcePos = (int32_t*)AudioEngine::i2sRXBufferPos;

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
			if (sourcePos >= getRxBufferEnd()) {
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
				if (sourcePos < getRxBufferStart()) {
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

	writePos = currentRecordCluster->data;
	clusterEndPos = &currentRecordCluster->data[audioFileManager.clusterSize];

	numSamplesBeenRunning = 0;
	numSamplesCaptured = 0;

	capturedTooMuch = false;

	recordingNumChannels = newNumChannels;
	int32_t byteDepth = 3;
	int32_t lengthSec =
	    5; // Mark it as 5 seconds long initially. We'll update that later when we know how long it actually is
	int32_t lengthSamples = lengthSec * sample->sampleRate;
	audioDataLengthBytesAsWrittenToFile = lengthSamples * 3 * recordingNumChannels;

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

// Beware! This could get called during card routine - e.g. if user stopped playback. So we'll just store a changed
// status, then do the descrutcion and file deletion when we know we're out of the card routine. Also, this gets called
// in audio routine! So don't do anything drastic.
void SampleRecorder::abort() {
	status = RecorderStatus::ABORTED; // Note: it may already equal this!
}

// Returns error if one occurred just now - not if one was already noted before
Error SampleRecorder::cardRoutine() {

	// If aborted, delete the file.
	if (status == RecorderStatus::ABORTED) {

aborted:
		if (sample) { // This might get called multiple times, so check we haven't already detached it.

			// Note: if this abort() is due to a song-swap (loading a different song),
			// then samples is about to be searched for temp ones to delete, and we'll need to have deleted it here
			// before that trips over us. Previously caused an E281. So, for that to happen,
			// SampleManager::deleteAnyTempRecordedSamplesFromMemory() (indirectly) calls us here first.

			detachSample(); // Does not set sample to NULL - we do that below

#if ALPHA_OR_BETA_VERSION
			// It should be impossible that anyone else still holds a "reason" to this Sample, as we can only be
			// "aborted" before AudioClip::finishLinearRecording() is called, and it's only then at the AudioClip
			// becomes a "reason".
			if (sample->numReasonsToBeLoaded) {
				FREEZE_WITH_ERROR("E282");
			}
#endif

			if (haveAddedSampleToArray) { // We only add it to the array when the file is created.
				audioFileManager.deleteUnusedAudioFileFromMemoryIndexUnknown(sample);
			}

			sample = NULL; // So we don't try to detach it again when we're destructed
		}

		// Delete the file if one was created
		if (!filePathCreated.isEmpty()) {

			FRESULT result = f_unlink(filePathCreated.get());

			// If this was the most recent recording in this category, tick the counter backwards - so long as
			// either the delete was successful or it was for an AudioClip, which means the file is in the TEMP folder
			// and can be overwritten anyway
			if (result == FR_OK || folderID == AudioRecordingFolder::CLIPS) {
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
			status = RecorderStatus::AWAITING_DELETION;
		}
		return Error::NONE;
	}

	if (status >= RecorderStatus::COMPLETE) {
		return Error::NONE;
	}

	Error error = Error::NONE;

	if (!hadCardError) {

		// If file not created yet, do that
		if (filePathCreated.isEmpty()) {

			error = storageManager.initSD();
			if (error != Error::NONE) {
				goto gotError;
			}

			// Check there's space on the card
			error = storageManager.checkSpaceOnCard();
			if (error != Error::NONE) {
				goto gotError;
			}

			String filePath;
			String tempFilePathForRecording;

			// Note: we couldn't pass the actual Sample pointer into this function, cos the Sample might get destructed
			// during the card access! (Though probably not anymore right?)
			// Recording could finish or abort during this!
			if (stemExport.processStarted) {
				error = stemExport.getUnusedStemRecordingFilePath(&filePath, folderID);
			}
			else {
				error = audioFileManager.getUnusedAudioRecordingFilePath(&filePath, &tempFilePathForRecording, folderID,
				                                                         &audioFileNumber);
			}
			if (status == RecorderStatus::ABORTED) {
				goto aborted; // In case aborted during
			}
			if (error != Error::NONE) {
				goto gotError;
			}

			bool mayOverwrite = true;

			// Now store our own copy of the actually (possibly temp) filename
			if (!tempFilePathForRecording.isEmpty()) {
				filePathCreated.set(&tempFilePathForRecording); // Can't fail!
			}
			else {
				filePathCreated.set(&filePath); // Can't fail!
				mayOverwrite = false;
			}

			// Recording could finish or abort during this!
			auto created = storageManager.createFile(filePathCreated.get(), mayOverwrite);
			if (!created) {
				filePathCreated.clear();
				goto gotError;
			}
			else {
				this->file = created.value();
			}

			if (status == RecorderStatus::ABORTED) {
				goto aborted; // In case aborted during
			}

			// Ok, the Sample still exists.
			sample->filePath.set(&filePath);                                 // Can't fail!
			sample->tempFilePathForRecording.set(&tempFilePathForRecording); // Can't fail!

			error = audioFileManager.audioFiles.insertElement(sample);
			if (error != Error::NONE) {
				goto gotError;
			}

			haveAddedSampleToArray = true;
		}

		// Might want to write just one cluster
		if (firstUnwrittenClusterIndex < currentRecordClusterIndex) {
			error = writeOneCompletedCluster();

			if (error != Error::NONE) {
gotError:
				hadCardError = true;
			}

			else {
				// If more clusters still to write, come back later to do them
				if (true || firstUnwrittenClusterIndex < currentRecordClusterIndex) {
					goto allDoneForNow;
				}
			}
		}
	}

	// If we've actually finished recording...
	if (status == RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
		if (!hadCardError) {
			error = finalizeRecordedFile();
			if (error != Error::NONE) {
				hadCardError = true;
				error = Error::SD_CARD;
			}
		}

		if (reachedMaxFileSize) {
			if (autoDeleteWhenDone) {
				abort();
			}
			else {
				status = RecorderStatus::COMPLETE;
			}
			error = Error::MAX_FILE_SIZE_REACHED;
		}
		else {
			status = autoDeleteWhenDone ? RecorderStatus::AWAITING_DELETION : RecorderStatus::COMPLETE;
		}
	}

allDoneForNow:
	return error;
}

Error SampleRecorder::writeAnyCompletedClusters() {
	while (firstUnwrittenClusterIndex < currentRecordClusterIndex) {

		Error error = writeOneCompletedCluster();

		// If there was an error, we can only return now after removing that reason, because we'd already incremented
		// firstUnwrittenClusterIndex, and we can't leave that incremented without removing the reason
		if (error != Error::NONE) {
			return error;
		}
	}

	return Error::NONE;
}

Error SampleRecorder::writeOneCompletedCluster() {
	int32_t writingClusterIndex = firstUnwrittenClusterIndex;

#if ALPHA_OR_BETA_VERSION
	// Trying to pin down E347 which Leo got, below
	Cluster* cluster = sample->clusters.getElement(writingClusterIndex)->cluster;
	if (!cluster->numReasonsHeldBySampleRecorder) {
		FREEZE_WITH_ERROR("E374");
	}
#endif

	firstUnwrittenClusterIndex++; // Have to increment this before writing, cos while writing, the audio routine will be
	                              // called, and we need to be counting this cluster as "written", as in too late for it
	                              // to be modified (by writing a final length to it)

	Error error = writeCluster(writingClusterIndex, audioFileManager.clusterSize);

	// We no longer have a reason to require this Cluster to be kept in memory
	if (!keepingReasonsForFirstClusters || writingClusterIndex >= kNumClustersLoadedAhead) {
		Cluster* cluster = sample->clusters.getElement(writingClusterIndex)->cluster;

		// Some bug-hunting
		if (!cluster->numReasonsHeldBySampleRecorder) {
			// Leo got!!! And Vinz, and keyman. May be solved now that fixed so detachSample() doesn't get called during
			// card routine.
			FREEZE_WITH_ERROR("E347");
		}
		cluster->numReasonsHeldBySampleRecorder--;

		audioFileManager.removeReasonFromCluster(cluster, "E015");
	}

	// If there was an error, we can only return now after removing that reason, because we'd already incremented
	// firstUnwrittenClusterIndex, and we can't leave that incremented without removing the reason
	return error;
}

Error SampleRecorder::finalizeRecordedFile() {

	if (ALPHA_OR_BETA_VERSION && (status == RecorderStatus::ABORTED || hadCardError)) {
		FREEZE_WITH_ERROR("E273");
	}

	D_PRINTLN("finalizing");

	// In the very rare case where we've already got between 1 and 5 bytes overhanging the end of our current cluster,
	// we need to allocate a new one right now
	int32_t bytesTilClusterEnd = (uint32_t)clusterEndPos - (uint32_t)writePos;
	if (bytesTilClusterEnd < 0) {
		Error error = createNextCluster();

		if (error == Error::MAX_FILE_SIZE_REACHED) {
		} // So incredibly unlikely. But no real problem - we maybe just lose a byte or two

		else if (error != Error::NONE) {
			return error;
		}
		else { // No error
			// Having just created a new cluster, there'll be one more completed one to write
			error = writeAnyCompletedClusters();
			if (error != Error::NONE) {
				return error;
			}
		}
	}

	// And we probably need to write some of the final cluster(s) to file. (If it's NULL, it means that it couldn't be
	// created, cos or RAM or file size limit.)
	if (currentRecordCluster) {

		int32_t bytesToWrite = (uint32_t)writePos - (uint32_t)currentRecordCluster->data;
		if (bytesToWrite > 0) { // Will always be true
			Error error = writeCluster(currentRecordClusterIndex, bytesToWrite);
			if (error != Error::NONE) {
				return error;
			}
		}

		firstUnwrittenClusterIndex++;

		// Having incremented firstUnwrittenClusterIndex, we need to remove the "reason" for that final cluster.
		// Normally that happens in writeAnyCompletedClusters(), but well this cluster wasn't "complete" so we're doing
		// the whole thing here instead
		if (!keepingReasonsForFirstClusters || currentRecordClusterIndex >= kNumClustersLoadedAhead) {

			// Some bug-hunting
			if (!currentRecordCluster->numReasonsHeldBySampleRecorder) {
				FREEZE_WITH_ERROR("E348");
			}
			currentRecordCluster->numReasonsHeldBySampleRecorder--;

			audioFileManager.removeReasonFromCluster(currentRecordCluster, "E047");
		}
		currentRecordClusterIndex++; // We've finished with that cluster
		currentRecordCluster = NULL; // But currentRecordClusterIndex now refers to a cluster that'll never exist
	}

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

		for (lshiftAmount = 0; ((uint32_t)2147483648 >> (lshiftAmount + 1)) > maxPeak; lshiftAmount++) {}
	}
	uint32_t dataLengthAfterAction =
	    action != MonitoringAction::NONE ? (dataLengthBeforeAction >> 1) : dataLengthBeforeAction;

	// TODO: in a perfect world, where we're not deleting a channel, we'd go backwards from the last Cluster, because
	// that's the most likely to still be in memory

	// If some processing of the recorded audio data needs to happen...
	if (lshiftAmount || action != MonitoringAction::NONE) {

		auto closed = this->file->close();
		if (!closed) {
			return Error::SD_CARD;
		}

		Error error = alterFile(action, lshiftAmount, idealFileSizeBeforeAction, dataLengthAfterAction);
		if (error != Error::NONE) {
			return error;
		}
	}

	// Or if no action or shifting was required...
	else {

		// If we made the file too long, because we then compensated for button latency and are throwing away the last
		// little bit, then truncate it
		if (capturedTooMuch) {
			D_PRINTLN("truncating");
			uint32_t correctLength =
			    sample->audioDataStartPosBytes
			    + sample->audioDataLengthBytes; // These were written to in totalSampleLengthNowKnown().
			Error error = truncateFileDownToSize(correctLength);
		}

		auto closed = this->file->close();
		if (!closed) {
			return Error::SD_CARD;
		}

		// If the actual audio data length we ended up with is not the same as was written in the headers in the first
		// cluster (very likely; various reasons)
		if (sample->audioDataLengthBytes != audioDataLengthBytesAsWrittenToFile
		    || (recordingExtraMargins && sample->fileLoopEndSamples != loopEndSampleAsWrittenToFile)) {

			// Update data length as written in first cluster
			SampleCluster* firstSampleCluster = sample->clusters.getElement(0);
			Cluster* cluster =
			    firstSampleCluster->getCluster(sample, 0, CLUSTER_LOAD_IMMEDIATELY); // Remember, this adds a "reason"
			if (cluster) {

				// Bug hunting - newly gotten Cluster
				cluster->numReasonsHeldBySampleRecorder++;

				// Do a last-ditch check that the SD address doesn't look invalid
				if (firstSampleCluster->sdAddress == 0) {
					FREEZE_WITH_ERROR("E268");
				}
				if ((firstSampleCluster->sdAddress - fileSystem.database) & (fileSystem.csize - 1)) {
					FREEZE_WITH_ERROR("E269");
				}

				audioDataLengthBytesAsWrittenToFile = sample->audioDataLengthBytes;
				loopEndSampleAsWrittenToFile = sample->fileLoopEndSamples;
				updateDataLengthInFirstCluster(cluster);

				// Write just that one first sector back to the card
				disk_write(0, (BYTE*)cluster->data, firstSampleCluster->sdAddress, 1);

				// If that failed, well, that's a shame, but we don't need to do anything

				// Some bug-hunting
				if (!cluster->numReasonsHeldBySampleRecorder) {
					FREEZE_WITH_ERROR("E349");
				}
				cluster->numReasonsHeldBySampleRecorder--;

				audioFileManager.removeReasonFromCluster(cluster, "E026");
			}
		}
	}

	sample->numChannels = (action != MonitoringAction::NONE || recordingNumChannels == 1) ? 1 : 2;
	sample->lengthInSamples = dataLengthAfterAction / (sample->byteDepth * sample->numChannels);
	sample->audioDataLengthBytes =
	    sample->lengthInSamples
	    * (sample->byteDepth
	       * sample->numChannels); // Ensure whole number of samples (surely it already would be though?)

	if (sample->tempFilePathForRecording.isEmpty()) {
		sampleBrowser.lastFilePathLoaded.set(&sample->filePath);
	}

	return Error::NONE;
}

void SampleRecorder::updateDataLengthInFirstCluster(Cluster* cluster) {
	uint32_t data32;

	// Write top-level RIFF chunk size
	*(uint32_t*)&cluster->data[4] = audioDataLengthBytesAsWrittenToFile + sample->audioDataStartPosBytes - 8;

	// Write data chunk size
	*(uint32_t*)&cluster->data[sample->audioDataStartPosBytes - 4] = audioDataLengthBytesAsWrittenToFile;

	if (recordingExtraMargins) {
		// Write loop end point
		*(uint32_t*)&cluster->data[92] = loopEndSampleAsWrittenToFile;
	}
}

extern int32_t pendingGlobalMIDICommandNumClustersWritten;

// You'll want to remove the "reason" after calling this
Error SampleRecorder::writeCluster(int32_t clusterIndex, size_t numBytes) {
	// D_PRINTLN("writeCluster");

	SampleCluster* sampleCluster = sample->clusters.getElement(clusterIndex);

	auto written = file->write({(std::byte*)sampleCluster->cluster->data, numBytes});
	if (!written || numBytes != written.value()) {
		return Error::SD_CARD;
	}

	// MUST re-get this - while writing above, the audio routine is being called, and that could
	// allocate new SampleClusters and move them around!
	sampleCluster = sample->clusters.getElement(clusterIndex);

	// Grab the SD address, for later
	sampleCluster->sdAddress = clst2sect(&fileSystem, file->inner().clust);
	return Error::NONE;
}

Error SampleRecorder::createNextCluster() {

	Cluster* oldRecordCluster = currentRecordCluster; // Cos we're gonna set that to NULL just below here, but still
	                                                  // want to be able to access the old one a bit further down

	currentRecordClusterIndex++; // Mark record-cluster we were on as finished

	currentRecordCluster = NULL; // Note that we haven't yet created our next record-cluster - we'll do that below if no
	                             // error first; and if there is an error and we don't create one, this has to remain
	                             // NULL to indicate that we never created one

	// If this new cluster would actually put us past the 4GB limit...
	if (currentRecordClusterIndex >= (1 << (MAX_FILE_SIZE_MAGNITUDE - audioFileManager.clusterSizeMagnitude))) {

		// See if we actually already had any bytes to write into that new cluster we can't have...
		int32_t bytesTilClusterEnd = (uint32_t)clusterEndPos - (uint32_t)writePos;
		if (bytesTilClusterEnd < 0) {
			numSamplesCaptured--;
			writePos -= (int32_t)recordingNumChannels * 3;
		}

		totalSampleLengthNowKnown(numSamplesCaptured, numSamplesCaptured);

		reachedMaxFileSize = true;
		return Error::MAX_FILE_SIZE_REACHED;
	}

	// We need to allocate our next Cluster
	Error error = sample->clusters.insertSampleClustersAtEnd(1);
	if (error != Error::NONE) {
		return error;
	}

	currentRecordCluster = sample->clusters.getElement(currentRecordClusterIndex)
	                           ->getCluster(sample, currentRecordClusterIndex, CLUSTER_DONT_LOAD);

	// If couldn't allocate cluster (would normally only happen if no SD card present so recording only to RAM)
	if (!currentRecordCluster) {
		D_PRINTLN("SampleRecorder::createNextCluster() fail");
		return Error::INSUFFICIENT_RAM;
	}

	// Bug hunting - newly gotten Cluster
	if (currentRecordCluster->numReasonsHeldBySampleRecorder) {
		FREEZE_WITH_ERROR("E362");
	}
	currentRecordCluster->numReasonsHeldBySampleRecorder++;

	// Copy those extra bytes from the end of the old record cluster to the start of the new cluster
	memcpy(currentRecordCluster->data, &oldRecordCluster->data[audioFileManager.clusterSize],
	       5); // 5 is the max number of bytes we could have overshot

	int32_t bytesOvershot = (uint32_t)writePos - (uint32_t)clusterEndPos;

	currentRecordCluster->loaded =
	    true; // I think this is ok - mark it as loaded even though we're yet to record into it

	writePos = (char*)&currentRecordCluster->data[bytesOvershot];
	clusterEndPos = (char*)&currentRecordCluster->data[audioFileManager.clusterSize];

	return Error::NONE;
}

// Gets called when we've captured all the samples of audio that we wanted - either as a direct result of user action,
// or after being fed a few more samples to make up for latency.
void SampleRecorder::finishCapturing() {
	status = RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING;
	if (getRootUI()) {
		getRootUI()->sampleNeedsReRendering(sample);
	}
}

// Only call this after checking that status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING
// Watch out - this could be called during SD writing - including during cardRoutine() for this class!
void SampleRecorder::feedAudio(int32_t* __restrict__ inputAddress, int32_t numSamples, bool applyGain) {

	do {
		int32_t numSamplesThisCycle = numSamples;
		if (ALPHA_OR_BETA_VERSION && numSamplesThisCycle <= 0) {
			FREEZE_WITH_ERROR("cccc");
		}

		// If haven't actually started recording yet cos we're compensating for lag...
		if (numSamplesBeenRunning < (uint32_t)numSamplesToRunBeforeBeginningCapturing) {
			int32_t numSamplesTilBeginRecording = numSamplesToRunBeforeBeginningCapturing - numSamplesBeenRunning;
			numSamplesThisCycle = std::min(numSamplesThisCycle, numSamplesTilBeginRecording);
		}

		// Or, if properly recording...
		else {
			int32_t samplesLeft;

			if (status == RecorderStatus::CAPTURING_DATA_WAITING_TO_STOP) {

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

			int32_t bytesTilClusterEnd = (uint32_t)clusterEndPos - (uint32_t)writePos;

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

				bytesTilClusterEnd = (uint32_t)clusterEndPos - (uint32_t)writePos; // Recalculate it
			}

			if (bytesTilClusterEnd <= bytesWeWantToWrite - bytesPerSample) {
				int32_t samplesTilClusterEnd =
				    (uint16_t)(bytesTilClusterEnd - 1) / (uint8_t)bytesPerSample + 1; // Rounds up
				numSamplesThisCycle = std::min(numSamplesThisCycle, samplesTilClusterEnd);
			}

			if (ALPHA_OR_BETA_VERSION && numSamplesThisCycle <= 0) {
				FREEZE_WITH_ERROR("aaaa");
			}

			int32_t* endInputNow = inputAddress + (numSamplesThisCycle << NUM_MONO_INPUT_CHANNELS_MAGNITUDE);

			char* __restrict__ writePosNow = writePos;

			// Balanced input. For this, we skip a bunch of stat-grabbing, cos we knob this is just for AudioClips.
			// We also know that applyGain is false - that's just for the MIX option
			if (mode == AudioInputChannel::BALANCED) {

				do {
					int32_t rxL = *inputAddress;
					int32_t rxR = *(inputAddress + 1);
					int32_t rxBalanced = (rxL >> 1) - (rxR >> 1);

					char* __restrict__ readPos = (char*)&rxBalanced + 1;
					*(writePosNow++) = *(readPos++);
					*(writePosNow++) = *(readPos++);
					*(writePosNow++) = *(readPos++);

					inputAddress += NUM_MONO_INPUT_CHANNELS;
				} while (inputAddress < endInputNow);
			}

			// Or, all other, non-balanced input types
			else {
				do {
					int32_t rxL = *inputAddress;
					if (applyGain) {
						rxL = lshiftAndSaturate<5>(rxL);
					}

					char* __restrict__ readPos = (char*)&rxL + 1;
					*(writePosNow++) = *(readPos++);
					*(writePosNow++) = *(readPos++);
					*(writePosNow++) = *(readPos++);

					if (rxL > recordMax) {
						recordMax = rxL;
					}
					if (rxL < recordMin) {
						recordMin = rxL;
					}

					int32_t absL;
					if (rxL >= 0) {
						absL = rxL;
					}
					else {
						absL = -1 - rxL;
					}
					recordSumL += absL;

					if (rxL < recordPeakL) {
						recordPeakL = rxL;
					}
					else if (-rxL < recordPeakL) {
						recordPeakL = -rxL;
					}
					if (rxL == 2147483647 || rxL == -2147483648) {
						recordingClippedRecently = true;
					}

					if (recordingNumChannels == 2) {
						int32_t rxR = *(inputAddress + 1);
						if (applyGain) {
							rxR = lshiftAndSaturate<5>(rxR);
						}

						readPos = (char*)&rxR + 1;
						*(writePosNow++) = *(readPos++);
						*(writePosNow++) = *(readPos++);
						*(writePosNow++) = *(readPos++);

						if (rxR > recordMax) {
							recordMax = rxR;
						}
						if (rxR < recordMin) {
							recordMin = rxR;
						}

						if (rxR >= 0) {
							recordSumR += rxR;
						}
						else {
							recordSumR += -1 - rxR;
						}

						int32_t lPlusR = (rxL >> 1) + (rxR >> 1);
						if (lPlusR >= 0) {
							recordSumLPlusR += lPlusR;
						}
						else {
							recordSumLPlusR += -1 - lPlusR;
						}

						int32_t lMinusR = (rxL >> 1) - (rxR >> 1);
						if (lMinusR >= 0) {
							recordSumLMinusR += lMinusR;
						}
						else {
							recordSumLMinusR += -1 - lMinusR;
						}

						if (rxR < recordPeakR) {
							recordPeakR = rxR;
						}
						else if (-rxR < recordPeakR) {
							recordPeakR = -rxR;
						}
						if (rxR == 2147483647 || rxR == -2147483648) {
							recordingClippedRecently = true;
						}

						if (lMinusR < recordPeakLMinusR) {
							recordPeakLMinusR = lMinusR;
						}
						else if (-lMinusR < recordPeakLMinusR) {
							recordPeakLMinusR = -lMinusR;
						}
					}

					inputAddress += NUM_MONO_INPUT_CHANNELS;
				} while (inputAddress < endInputNow);
			}

			writePos = writePosNow;

			numSamplesCaptured += numSamplesThisCycle;
		}

		numSamplesBeenRunning += numSamplesThisCycle;

		numSamples -= numSamplesThisCycle;
	} while (numSamples);
}

void SampleRecorder::endSyncedRecording(int32_t buttonLatencyForTempolessRecording) {
#if ALPHA_OR_BETA_VERSION
	if (status == RecorderStatus::CAPTURING_DATA_WAITING_TO_STOP) {
		FREEZE_WITH_ERROR("E272");
	}
	else if (status == RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
		FREEZE_WITH_ERROR("E288");
	}
	else if (status == RecorderStatus::COMPLETE) {
		FREEZE_WITH_ERROR("E289");
	}
	else if (status == RecorderStatus::ABORTED) {
		FREEZE_WITH_ERROR("E290");
	}
	else if (status == RecorderStatus::AWAITING_DELETION) {
		FREEZE_WITH_ERROR("E291");
	}
#endif

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
		status = RecorderStatus::CAPTURING_DATA_WAITING_TO_STOP;
	}
}

void SampleRecorder::totalSampleLengthNowKnown(uint32_t totalLengthSamples, uint32_t loopEndPointSamples) {

	sample->lengthInSamples = totalLengthSamples;
	sample->audioDataLengthBytes = totalLengthSamples * sample->byteDepth * sample->numChannels;

	sample->fileLoopEndSamples = loopEndPointSamples;

	// If we haven't written the first cluster yet, quick - update it with the actual length
	if (firstUnwrittenClusterIndex == 0) {
		SampleCluster* firstSampleCluster = sample->clusters.getElement(0);
		Cluster* cluster =
		    firstSampleCluster->cluster; // It should still be there, cos it hasn't been written to card yet
		if (ALPHA_OR_BETA_VERSION && !cluster) {
			FREEZE_WITH_ERROR("E274");
		}

		audioDataLengthBytesAsWrittenToFile = sample->audioDataLengthBytes;
		loopEndSampleAsWrittenToFile =
		    sample->fileLoopEndSamples; // Even if we're not actually writing loop points to the file, this is harmless
		updateDataLengthInFirstCluster(cluster);
	}
}

bool SampleRecorder::inputLooksDifferential() {
	return (recordSumLPlusR < (recordSumL >> 4));
}

bool SampleRecorder::inputHasNoRightChannel() {
	return (recordSumR < (recordSumL >> 6));
}

// Only call this if currentRecordCluster points to a real cluster
void SampleRecorder::setExtraBytesOnPreviousCluster(Cluster* currentCluster, int32_t currentClusterIndex) {
	if (currentClusterIndex <= 0) {
		return;
	}

	Cluster* prevCluster = sample->clusters.getElement(currentClusterIndex - 1)->cluster;

	// It might have since been deallocated, which is just fine. But if not...
	if (prevCluster) {
		memcpy(&prevCluster->data[audioFileManager.clusterSize], currentCluster->data, 5);
	}
}

Error SampleRecorder::alterFile(MonitoringAction action, int32_t lshiftAmount, uint32_t idealFileSizeBeforeAction,
                                uint64_t dataLengthAfterAction) {

	D_PRINTLN("altering file");
	int32_t currentReadClusterIndex = 0;
	int32_t currentWriteClusterIndex = 0;

	Cluster* currentReadCluster = sample->clusters.getElement(0)->getCluster(
	    sample, 0, CLUSTER_LOAD_IMMEDIATELY); // Remember, this adds a "reason"
	if (!currentReadCluster) {
		return Error::SD_CARD;
	}

	// Bug hunting - newly gotten Cluster
	currentReadCluster->numReasonsHeldBySampleRecorder++;

	int32_t numClustersBeforeAction =
	    ((idealFileSizeBeforeAction - 1) >> audioFileManager.clusterSizeMagnitude) + 1; // Rounds up
	if (ALPHA_OR_BETA_VERSION && numClustersBeforeAction > sample->clusters.getNumElements()) {
		FREEZE_WITH_ERROR("E286");
	}

	Cluster* nextReadCluster = NULL;

	if (numClustersBeforeAction >= 2) {
		nextReadCluster = sample->clusters.getElement(1)->getCluster(
		    sample, 1, CLUSTER_LOAD_IMMEDIATELY); // Remember, this adds a "reason"
		if (!nextReadCluster) {

			// Some bug-hunting
			if (!currentReadCluster->numReasonsHeldBySampleRecorder) {
				FREEZE_WITH_ERROR("E350");
			}
			currentReadCluster->numReasonsHeldBySampleRecorder--;

			audioFileManager.removeReasonFromCluster(currentReadCluster, "E017");
			return Error::SD_CARD;
		}

		// Bug hunting - newly gotten Cluster
		nextReadCluster->numReasonsHeldBySampleRecorder++;
	}

	Cluster* currentWriteCluster =
	    sample->clusters.getElement(0)->getCluster(sample, 0, CLUSTER_DONT_LOAD); // Remember, this adds a "reason"
	// That one can't fail, fortunately, cos we already grabbed Cluster 0 above, so it exists

	// Bug hunting - newly gotten Cluster
	currentWriteCluster->numReasonsHeldBySampleRecorder++;

	uint32_t data32;
	uint16_t data16;

	audioDataLengthBytesAsWrittenToFile = dataLengthAfterAction;
	loopEndSampleAsWrittenToFile = sample->fileLoopEndSamples;
	updateDataLengthInFirstCluster(currentWriteCluster);

	if (action != MonitoringAction::NONE) {
		// Write num channels
		data16 = 1;
		memcpy(&currentWriteCluster->data[22], &data16, 2);

		// Data rate
		data32 = kSampleRate * 1 * 3;
		memcpy(&currentWriteCluster->data[28], &data32, 4);

		// Data block size
		data16 = 1 * 3;
		memcpy(&currentWriteCluster->data[32], &data16, 2);
	}

	char* readPos = &currentReadCluster->data[sample->audioDataStartPosBytes];
	char* writePos = &currentWriteCluster->data[sample->audioDataStartPosBytes];

	uint32_t bytesFinalCluster = idealFileSizeBeforeAction & (audioFileManager.clusterSize - 1);
	if (bytesFinalCluster == 0) {
		bytesFinalCluster = audioFileManager.clusterSize;
	}

	uint32_t count = 0;

	// TODO: this is really inefficient - checks a bunch of stuff for every single audio sample. Should check in advance
	// how many samples we can process at a time

	while (true) {

		if (!(count & 0b11111111)) { // 10x 1's seems to work ok. So we go down to 8 to be sure
			AudioEngine::routineWithClusterLoading();

			uiTimerManager.routine();

			PIC::flush();
		}

		count++;

		int32_t* input = (int32_t*)(readPos - 1);
		readPos += 3;
		int32_t value = *input & 0xFFFFFF00;

		if (action == MonitoringAction::SUBTRACT_RIGHT_CHANNEL) {
			input = (int32_t*)(readPos - 1);
			readPos += 3;
			value = (value >> 1) - ((int32_t)(*input & 0xFFFFFF00) >> 1);
		}

		else if (action == MonitoringAction::REMOVE_RIGHT_CHANNEL) {
			readPos += 3;
		}
		int32_t processed = value << lshiftAmount;

		char* processedPos = (char*)&processed + 1;
		*(writePos++) = *(processedPos++);
		*(writePos++) = *(processedPos++);
		*(writePos++) = *(processedPos++);

		// If need to advance write-head past the end of a cluster, then we'll write that current cluster to disk and
		// carry on
		int32_t writeOvershot = (uint32_t)writePos - (uint32_t)&currentWriteCluster->data[audioFileManager.clusterSize];
		if (writeOvershot >= 0) {

			// If reached very end of file, break
			if (currentWriteClusterIndex == numClustersBeforeAction - 1) {
				break;
			}

			D_PRINTLN("write advance");

			currentWriteCluster->loaded = true; // I don't think this is necessary anymore

			uint32_t sdAddress = sample->clusters.getElement(currentWriteClusterIndex)->sdAddress;

			// Do a last-ditch check that the SD address doesn't look invalid
			if (sdAddress == 0) {
				FREEZE_WITH_ERROR("E268");
			}
			if ((sdAddress - fileSystem.database) & (fileSystem.csize - 1)) {
				FREEZE_WITH_ERROR("E275");
			}

			// Write the Cluster we just finished processing to card
			DRESULT result =
			    disk_write(0, (BYTE*)currentWriteCluster->data, sdAddress, audioFileManager.clusterSize >> 9);

			// Grab any overshot / extra bytes from the end of the Cluster we just finished...
			uint8_t extraBytes[5]; // 5 is the max number of bytes we could have overshot
			if (writeOvershot) {
				memcpy(extraBytes, &currentWriteCluster->data[audioFileManager.clusterSize], writeOvershot);
			}

			// And from the Cluster we just finished, give the Cluster *before that* the extra bytes from its start
			setExtraBytesOnPreviousCluster(currentWriteCluster, currentWriteClusterIndex);

			// We don't need that old Cluster anymore

			// Some bug-hunting
			if (!currentWriteCluster->numReasonsHeldBySampleRecorder) {
				FREEZE_WITH_ERROR("E351");
			}
			currentWriteCluster->numReasonsHeldBySampleRecorder--;

			audioFileManager.removeReasonFromCluster(currentWriteCluster, "E023");
			currentWriteCluster = NULL;

			// If write operation failed, now's the time to get out
			if (result) {
writeFailed:
				// Before we get out, remove "reasons" from the clusters we've been reading from

				// Some bug-hunting
				if (!currentReadCluster->numReasonsHeldBySampleRecorder) {
					FREEZE_WITH_ERROR("E352");
				}
				currentReadCluster->numReasonsHeldBySampleRecorder--;

				audioFileManager.removeReasonFromCluster(currentReadCluster, "E024");

				if (nextReadCluster) {
					// Some bug-hunting
					if (!nextReadCluster->numReasonsHeldBySampleRecorder) {
						FREEZE_WITH_ERROR("E353");
					}
					nextReadCluster->numReasonsHeldBySampleRecorder--;

					audioFileManager.removeReasonFromCluster(nextReadCluster, "E025");
				}
				return Error::SD_CARD;
			}

			// Ok, move on and start thinking about the next Cluster now
			currentWriteClusterIndex++;

			// Get the new / next Cluster, but don't insist on actually reading from the card, cos we're gonna overwrite
			// it with new data anyway
			currentWriteCluster =
			    sample->clusters.getElement(currentWriteClusterIndex)
			        ->getCluster(sample, currentWriteClusterIndex, CLUSTER_DONT_LOAD); // Remember, this adds a "reason"

			// That could only fail if no RAM, but juuuust in case...
			if (!currentWriteCluster) {
				goto writeFailed;
			}

			// Bug hunting - newly gotten Cluster
			currentWriteCluster->numReasonsHeldBySampleRecorder++;

			// Ok, and those extra bytes that we grabbed from the end of the previous Cluster - paste them into the
			// beginning of the new current Cluster
			if (writeOvershot) {
				memcpy(currentWriteCluster->data, extraBytes, writeOvershot);
			}

			// And get ready to write to the new current Cluster - from the next sample, which might not be perfectly
			// aligned to the Cluster start
			writePos = &currentWriteCluster->data[writeOvershot];
		}

		// If we're in the final read-Cluster and reached the end, then all that's left to do is flush out what we have
		// left to write (max 1 cluster), and get out.
		if (currentReadClusterIndex == numClustersBeforeAction - 1
		    && readPos >= &currentReadCluster->data[bytesFinalCluster]) {
			break;
		}

		// Advance read-head. We read one Cluster ahead, so we can access its "extra bytes"
		if (readPos >= &currentReadCluster->data[audioFileManager.clusterSize]) {

			D_PRINTLN("read advance");

			int32_t overshot = (uint32_t)readPos - (uint32_t)&currentReadCluster->data[audioFileManager.clusterSize];

			// Some bug-hunting
			if (!currentReadCluster->numReasonsHeldBySampleRecorder) {
				FREEZE_WITH_ERROR("E354");
			}
			currentReadCluster->numReasonsHeldBySampleRecorder--;

			audioFileManager.removeReasonFromCluster(currentReadCluster, "E020");
			currentReadClusterIndex++;
			currentReadCluster = nextReadCluster;

			// If there are further read Clusters...
			if (currentReadClusterIndex < numClustersBeforeAction - 1) {
				nextReadCluster = sample->clusters.getElement(currentReadClusterIndex + 1)
				                      ->getCluster(sample, currentReadClusterIndex + 1,
				                                   CLUSTER_LOAD_IMMEDIATELY); // Remember, this adds a "reason"

				// If that failed, remove other reasons and get out
				if (!nextReadCluster) {

					// Some bug-hunting
					if (!currentReadCluster->numReasonsHeldBySampleRecorder) {
						FREEZE_WITH_ERROR("E355");
					}
					currentReadCluster->numReasonsHeldBySampleRecorder--;

					audioFileManager.removeReasonFromCluster(currentReadCluster, "E021");

					// Some bug-hunting
					if (!currentWriteCluster->numReasonsHeldBySampleRecorder) {
						FREEZE_WITH_ERROR("E356");
					}
					currentWriteCluster->numReasonsHeldBySampleRecorder--;

					audioFileManager.removeReasonFromCluster(currentWriteCluster, "E022");
					currentWriteCluster = NULL;
					return Error::SD_CARD;
				}

				// Bug hunting - newly gotten Cluster
				nextReadCluster->numReasonsHeldBySampleRecorder++;
			}
			else { // Not sure these are strictly necessary...
				nextReadCluster = NULL;
			}

			readPos = &currentReadCluster->data[overshot];
		}
	}

	// We got to the end, so wrap everything up

	// Some bug-hunting
	if (!currentReadCluster->numReasonsHeldBySampleRecorder) {
		FREEZE_WITH_ERROR("E357");
	}
	currentReadCluster->numReasonsHeldBySampleRecorder--;

	audioFileManager.removeReasonFromCluster(currentReadCluster, "E018");
	// We know that finishedAlteringFile must be NULL

	currentWriteCluster->loaded = true;

	uint32_t bytesToWriteFinalCluster = (uint32_t)writePos - (uint32_t)currentWriteCluster->data;

	if (bytesToWriteFinalCluster) { // If there is in fact anything to flush out to the file / card...

		// And from this final Cluster, give the Cluster *before that* the extra bytes from its start
		setExtraBytesOnPreviousCluster(currentWriteCluster, currentWriteClusterIndex);

		uint32_t numSectorsToWrite = ((bytesToWriteFinalCluster - 1) >> 9) + 1;
		if (numSectorsToWrite > (audioFileManager.clusterSize >> 9)) {
			FREEZE_WITH_ERROR("E239");
		}

		uint32_t sdAddress = sample->clusters.getElement(currentWriteClusterIndex)->sdAddress;

		// Do a last-ditch check that the SD address doesn't look invalid
		if (sdAddress == 0) {
			FREEZE_WITH_ERROR("E268");
		}
		if ((sdAddress - fileSystem.database) & (fileSystem.csize - 1)) {
			FREEZE_WITH_ERROR("E276");
		}

		DRESULT result = disk_write(0, (BYTE*)currentWriteCluster->data, sdAddress, numSectorsToWrite);

		// Some bug-hunting
		if (!currentWriteCluster->numReasonsHeldBySampleRecorder) {
			FREEZE_WITH_ERROR("E358");
		}
		currentWriteCluster->numReasonsHeldBySampleRecorder--;

		audioFileManager.removeReasonFromCluster(currentWriteCluster, "E019");
		currentWriteCluster = NULL;

		// If writing disk failed, above, we've now removed that "reason", so we can get out
		if (result) {
			return Error::SD_CARD;
		}

		if (action != MonitoringAction::NONE || capturedTooMuch) {

			auto opened = this->file->open(sample->filePath.get(), FA_WRITE);

			if (!opened) {
				return Error::SD_CARD;
			}
			this->file = opened.value();

			Error error = truncateFileDownToSize(dataLengthAfterAction + sample->audioDataStartPosBytes);
			if (error != Error::NONE) {
				return error;
			}

			auto closed = this->file->close();
			if (!closed) {
				return Error::SD_CARD;
			}
		}
	}
	else { // Or if there was nothing further to write (very rare)...

		// Some bug-hunting
		if (!currentWriteCluster->numReasonsHeldBySampleRecorder) {
			FREEZE_WITH_ERROR("E359");
		}
		currentWriteCluster->numReasonsHeldBySampleRecorder--;

		audioFileManager.removeReasonFromCluster(currentWriteCluster, "E238");
		currentWriteCluster = NULL;
	}

	return Error::NONE;
}

// You must still have the file open when you call this
Error SampleRecorder::truncateFileDownToSize(uint32_t newFileSize) {

	// Update the Sample object to indicate the correct size. Do this before we risk errors below

	uint64_t numClustersAfterAction = ((newFileSize - 1) >> audioFileManager.clusterSizeMagnitude) + 1;

	int32_t numToDelete = sample->clusters.getNumElements() - numClustersAfterAction;
	if (numToDelete > 0) {
		for (int32_t i = numClustersAfterAction; i < sample->clusters.getNumElements(); i++) {
			sample->clusters.getElement(i)->~SampleCluster();
		}
		sample->clusters.deleteAtIndex(numClustersAfterAction, numToDelete);
	}

	// Truncate file size
	auto seeked = file->lseek(newFileSize);
	if (!seeked) {
		return Error::SD_CARD;
	}
	auto truncated = file->truncate();
	if (!truncated) {
		return Error::SD_CARD;
	}

	return Error::NONE;
}
