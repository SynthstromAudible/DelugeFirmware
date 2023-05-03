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

#include <AudioEngine.h>
#include <Cluster.h>
#include <SampleRecorder.h>
#include "Sample.h"
#include "SampleManager.h"
#include "GeneralMemoryAllocator.h"
#include "storagemanager.h"
#include "functions.h"
#include <new>
#include "RootUI.h"
#include "AudioClip.h"
#include "samplebrowser.h"
#include "numericdriver.h"
#include "uart.h"
#include "uitimermanager.h"

extern "C" {
#include "fatfs/diskio.h"
#include "ssi_all_cpus.h"
#include "uart_all_cpus.h"

LBA_t clst2sect (	/* !=0:Sector number, 0:Failed (invalid cluster#) */
	FATFS* fs,		/* Filesystem object */
	DWORD clst		/* Cluster# to be converted */
);
}

extern "C" void routineForSD(void);

extern uint8_t currentlyAccessingCard;

#define MAX_FILE_SIZE_MAGNITUDE 32



SampleRecorder::SampleRecorder() {
	allowFileAlterationAfter = false;
	autoDeleteWhenDone = false;
	currentRecordCluster = NULL;
	status = RECORDER_STATUS_CAPTURING_DATA;
	hadCardError = false;
	reachedMaxFileSize = false;
	haveAddedSampleToArray = false;

	currentRecordClusterIndex = -1; // Put things in valid state so if we get destructed before any recording, it's all ok
	firstUnwrittenClusterIndex = 0;
}

SampleRecorder::~SampleRecorder() {
	Uart::println("~SampleRecorder()");
	if (sample) {
		detachSample();
	}
}


// This can be called when this SampleRecorder is destructed routinely - or earlier if we've aborted and the sample file is being deleted
// IMPORTANT!!!! You have to set sample to NULL after calling this, if not destructing
void SampleRecorder::detachSample() {

	// If we were holding onto the reasons for the first couple of Clusters, release them now
	if (keepingReasonsForFirstClusters) {
		int numClustersToRemoveFor = getMin(NUM_SAMPLE_CHUNKS_LOADED_AHEAD, sample->clusters.getNumElements());
		numClustersToRemoveFor = getMin(numClustersToRemoveFor, firstUnwrittenClusterIndex);

		for (int l = 0; l < numClustersToRemoveFor; l++) {
			Cluster* cluster = sample->clusters.getElement(l)->loadedSampleChunk;

			// Some bug-hunting
			if (!cluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E345");
			cluster->numReasonsHeldBySampleRecorder--;

			sampleManager.removeReasonFromLoadedSampleChunk(cluster, "E257");
		}
	}

	int removeForClustersUntilIndex = currentRecordClusterIndex;
	if (currentRecordCluster) removeForClustersUntilIndex++; // If there's a currentRecordCluster (usually will be if aborting), need to remove its "reason" too

	while (firstUnwrittenClusterIndex < removeForClustersUntilIndex) {
		Cluster* cluster = sample->clusters.getElement(firstUnwrittenClusterIndex)->loadedSampleChunk;

		if (!cluster) numericDriver.freezeWithError("E363");

		// Some bug-hunting
		if (!cluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E346");
		cluster->numReasonsHeldBySampleRecorder--;

		sampleManager.removeReasonFromLoadedSampleChunk(cluster, "E249");
		firstUnwrittenClusterIndex++;
	}

	sample->removeReason("E400");
}


int SampleRecorder::setup(int newNumChannels, int newMode, bool newKeepingReasons, bool shouldRecordExtraMargins, int newFolderID, int buttonPressLatency) {

	if (!sampleManager.ensureEnoughMemoryForOneMoreAudioFile()) return ERROR_INSUFFICIENT_RAM;

	keepingReasonsForFirstClusters = newKeepingReasons;
	recordingExtraMargins = shouldRecordExtraMargins;
	folderID = newFolderID;

    void* sampleMemory = generalMemoryAllocator.alloc(sizeof(Sample)); // Didn't seem to make a difference forcing this into local RAM
    if (!sampleMemory) return ERROR_INSUFFICIENT_RAM;

    sample = new (sampleMemory) Sample;
	sample->addReason(); // Must call this so it's protected from stealing, before we call initialize().
    int error = sample->initialize(1);
    if (error) {
gotError:
    	sample->~Sample();
    	generalMemoryAllocator.dealloc(sampleMemory);
    	return error;
    }

    currentRecordCluster = sample->clusters.getElement(0)->getLoadedSampleChunk(sample, 0, CHUNK_DONT_LOAD); // Adds a "reason" to it, too
	if (!currentRecordCluster) {
		error = ERROR_INSUFFICIENT_RAM;
		goto gotError;
	}

	// Bug hunting - newly gotten Cluster
	if (currentRecordCluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E360");
	currentRecordCluster->numReasonsHeldBySampleRecorder++;

	// Give the sample some stuff
	sample->audioDataStartPosBytes = recordingExtraMargins ? 112 : 44;
	sample->byteDepth = 3;
	sample->numChannels = newNumChannels;
	sample->lengthInSamples = 0x8FFFFFFFFFFFFFFF;
	sample->audioDataLengthBytes = 0x8FFFFFFFFFFFFFFF; // If you ever change this value, update the check for it in SampleManager::loadSampleChunk()
	sample->sampleRate = 44100;
	sample->workOutBitMask();

	currentRecordCluster->loaded = true; // I think this is ok - mark it as loaded even though we're yet to record into it

	pointerHeldElsewhere = true;
	mode = newMode;
	currentRecordClusterIndex = 0;

	numSamplesToRunBeforeBeginningCapturing = numSamplesExtraToCaptureAtEndSyncingWise =
			(mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) ? AUDIO_RECORD_LAG_COMPENTATION : 0;

	// Apart from the MIX option, all other audio sources are fed to us during the "outputting" routine. Occasionally, there'll be some more of that
	// going to happen for the previous render, so we have to compensate for that
	if (mode != AUDIO_INPUT_CHANNEL_MIX)
		numSamplesToRunBeforeBeginningCapturing += AudioEngine::getNumSamplesLeftToOutputFromPreviousRender();

	// External sources
	if (mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {

		sourcePos = (int32_t*)AudioEngine::i2sRXBufferPos;

		numSamplesToRunBeforeBeginningCapturing -= buttonPressLatency; // Compensate for button press latency. We only do this for external sources

		// If doing extra margins...
		if (recordingExtraMargins) {

			// Everything will be fine, so long as the button press latency we compensated for isn't, like, as big as the RX buffer
			sample->fileLoopStartSamples = SSI_RX_BUFFER_NUM_SAMPLES - (SSI_TX_BUFFER_NUM_SAMPLES << 1) + numSamplesToRunBeforeBeginningCapturing;
			numSamplesToRunBeforeBeginningCapturing = 0;

			sourcePos += (SSI_TX_BUFFER_NUM_SAMPLES << (NUM_MONO_INPUT_CHANNELS_MAGNITUDE + 1)); // I think the +1 was just because it needs to move two tx buffers' length for some reason...
			if (sourcePos >= getRxBufferEnd()) sourcePos -= SSI_RX_BUFFER_NUM_SAMPLES << NUM_MONO_INPUT_CHANNELS_MAGNITUDE;
		}

		// Or if not doing extra margins...
		else {

			// If the button press latency we're compensating for is more than the audio latency, we have to adjust stuff, to grab some audio from just back in time
			if (numSamplesToRunBeforeBeginningCapturing < 0) {

				sourcePos += numSamplesToRunBeforeBeginningCapturing * NUM_MONO_INPUT_CHANNELS; // This might be negative!
				if (sourcePos < getRxBufferStart()) sourcePos += SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;

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
	clusterEndPos = &currentRecordCluster->data[sampleManager.clusterSize];

	numSamplesBeenRunning = 0;
	numSamplesCaptured = 0;

	capturedTooMuch = false;

	recordingNumChannels = newNumChannels;
	int byteDepth = 3;
	int lengthSec = 5; // Mark it as 5 seconds long initially. We'll update that later when we know how long it actually is
	int lengthSamples = lengthSec * sample->sampleRate;
	audioDataLengthBytesAsWrittenToFile = lengthSamples * 3 * recordingNumChannels;

	// Riff chunk -------------------------------------------------------
	writeInt32(&writePos, 0x46464952); // "RIFF"
	writeInt32(&writePos, audioDataLengthBytesAsWrittenToFile + sample->audioDataStartPosBytes - 8); // Chunk size
	writeInt32(&writePos, 0x45564157); // "WAVE"

	// Format chunk --------------------------------------------------------
	writeInt32(&writePos, 0x20746d66); // "fmt "
	writeInt32(&writePos, 16); // Chunk size
	writeInt16(&writePos, 0x0001); // Format - PCM
	writeInt16(&writePos, recordingNumChannels); // Num channels
	writeInt32(&writePos, sample->sampleRate); // Sample rate
	writeInt32(&writePos, sample->sampleRate * recordingNumChannels * byteDepth); // Data rate
	writeInt16(&writePos, recordingNumChannels * byteDepth); // Data block size
	writeInt16(&writePos, byteDepth * 8); // Bits per sample

	if (recordingExtraMargins) {

		loopEndSampleAsWrittenToFile = lengthSamples;

		// Sampler chunk ------------------------------------------------------
		writeInt32(&writePos, 0x6c706d73); // "smpl"
		writeInt32(&writePos, 60); // Chunk size
		writeInt32(&writePos, 0); // Manufacturer - 0 means none
		writeInt32(&writePos, 0); // Product - 0 means none
		writeInt32(&writePos, (1000000000 + (sample->sampleRate >> 1)) / sample->sampleRate); // Nanoseconds per sample
		writeInt32(&writePos, 0); // MIDI note - 0 conventionally seems to mean none
		writeInt32(&writePos, 0); // MIDI pitch fraction
		writeInt32(&writePos, 0); // SMPTE format - 0 means none / no offset
		writeInt32(&writePos, 0); // SMPTE offset
		writeInt32(&writePos, 1); // Number of loops
		writeInt32(&writePos, 0); // Number of additional sampler data bytes

		// Loop definition ----------------------------------------------------
		writeInt32(&writePos, 0); // Cue point ID
		writeInt32(&writePos, 0); // Type - 0 means loop forward
		writeInt32(&writePos, sample->fileLoopStartSamples); // Start point
		writeInt32(&writePos, loopEndSampleAsWrittenToFile); // End point
		writeInt32(&writePos, 0); // Loop point sample fraction
		writeInt32(&writePos, 0); // Play count - 0 means continuous
	}

	// Data chunk ------------------------------------------------------
	writeInt32(&writePos, 0x61746164); // "data"
	writeInt32(&writePos, audioDataLengthBytesAsWrittenToFile); // Chunk size

	return NO_ERROR;
}

// Beware! This could get called during card routine - e.g. if user stopped playback. So we'll just store a changed status,
// then do the descrutcion and file deletion when we know we're out of the card routine.
// Also, this gets called in audio routine! So don't do anything drastic.
void SampleRecorder::abort() {
	status = RECORDER_STATUS_ABORTED; // Note: it may already equal this!
}


// Returns error if one occurred just now - not if one was already noted before
int SampleRecorder::cardRoutine() {

	// If aborted, delete the file.
	if (status == RECORDER_STATUS_ABORTED) {

aborted:
		if (sample) { // This might get called multiple times, so check we haven't already detached it.

			// Note: if this abort() is due to a song-swap (loading a different song),
			// then samples is about to be searched for temp ones to delete, and we'll need to have deleted it here before that trips over us. Previously caused an E281.
			// So, for that to happen, SampleManager::deleteAnyTempRecordedSamplesFromMemory() (indirectly) calls us here first.

			detachSample(); // Does not set sample to NULL - we do that below

#if ALPHA_OR_BETA_VERSION
			// It should be impossible that anyone else still holds a "reason" to this Sample, as we can only be "aborted"
			// before AudioClip::finishLinearRecording() is called, and it's only then at the AudioClip becomes a "reason".
			if (sample->numReasons) numericDriver.freezeWithError("E282");
#endif

			if (haveAddedSampleToArray) { // We only add it to the array when the file is created.
				sampleManager.deleteUnusedAudioFileFromMemoryIndexUnknown(sample);
			}

			sample = NULL; // So we don't try to detach it again when we're destructed
		}

		// Delete the file if one was created
		if (!filePathCreated.isEmpty()) {

			FRESULT result = f_unlink(filePathCreated.get());

			// If this was the most recent recording in this category, tick the counter backwards - so long as
			// either the delete was successful or it was for an AudioClip, which means the file is in the TEMP folder and can be overwritten anyway
			if (result == FR_OK || folderID == AUDIO_RECORDING_FOLDER_CLIPS) {
				if (sampleManager.highestUsedAudioRecordingNumber[folderID] == audioFileNumber) {
					sampleManager.highestUsedAudioRecordingNumber[folderID]--;
					Uart::println("ticked file counter backwards");
				}
			}
			filePathCreated.clear();
		}

		// Normally we now just await deletion - except if a pointer is still being held elsewhere - which I think can only happen from AudioRecorder.
		// Or if the abort comes from a failure within this class and the AudioClip hasn't realised yet?
		if (!pointerHeldElsewhere) status = RECORDER_STATUS_AWAITING_DELETION;
		return NO_ERROR;
	}

	if (status >= RECORDER_STATUS_COMPLETE) return NO_ERROR;

	int errorToReturn = NO_ERROR;

	if (!hadCardError) {

		// If file not created yet, do that
		if (filePathCreated.isEmpty()) {

			errorToReturn = storageManager.initSD();
			if (errorToReturn) goto gotError;

			// Check there's space on the card
			errorToReturn = storageManager.checkSpaceOnCard();
			if (errorToReturn) goto gotError;

			String filePath;
			String tempFilePathForRecording;

			// Note: we couldn't pass the actual Sample pointer into this function, cos the Sample might get destructed during the card access! (Though probably not anymore right?)
			errorToReturn = sampleManager.getUnusedAudioRecordingFilePath(&filePath, &tempFilePathForRecording, folderID, &audioFileNumber); // Recording could finish or abort during this!
			if (status == RECORDER_STATUS_ABORTED) goto aborted; // In case aborted during
			if (errorToReturn) goto gotError;

			bool mayOverwrite = true;

			// Now store our own copy of the actually (possibly temp) filename
			if (!tempFilePathForRecording.isEmpty()) {
				filePathCreated.set(&tempFilePathForRecording); // Can't fail!
			}
			else {
				filePathCreated.set(&filePath); // Can't fail!
				mayOverwrite = false;
			}

			errorToReturn = storageManager.createFile(&file, filePathCreated.get(), mayOverwrite); // Recording could finish or abort during this!
			if (errorToReturn) {
				filePathCreated.clear();
				goto gotError;
			}
			if (status == RECORDER_STATUS_ABORTED) goto aborted; // In case aborted during

			// Ok, the Sample still exists.
			sample->filePath.set(&filePath); // Can't fail!
			sample->tempFilePathForRecording.set(&tempFilePathForRecording); // Can't fail!

			errorToReturn = sampleManager.audioFiles.insertElement(sample);
			if (errorToReturn) goto gotError;

			haveAddedSampleToArray = true;
		}

		// Might want to write just one cluster
		if (firstUnwrittenClusterIndex < currentRecordClusterIndex) {
			errorToReturn = writeOneCompletedCluster();

			if (errorToReturn) {
gotError:
				hadCardError = true;
			}

			else {
				// If more clusters still to write, come back later to do them
				if (true || firstUnwrittenClusterIndex < currentRecordClusterIndex) goto allDoneForNow;
			}
		}
	}

	// If we've actually finished recording...
	if (status == RECORDER_STATUS_FINISHED_CAPTURING_BUT_STILL_WRITING) {
		if (!hadCardError) {
			errorToReturn = finalizeRecordedFile();
			if (errorToReturn) {
				hadCardError = true;
				errorToReturn = ERROR_SD_CARD;
			}
		}

		if (reachedMaxFileSize) {
			if (autoDeleteWhenDone) abort();
			else status = RECORDER_STATUS_COMPLETE;
			errorToReturn = ERROR_MAX_FILE_SIZE_REACHED;
		}
		else {
			status = autoDeleteWhenDone ? RECORDER_STATUS_AWAITING_DELETION : RECORDER_STATUS_COMPLETE;
		}
	}

allDoneForNow:
	return errorToReturn;
}

int SampleRecorder::writeAnyCompletedClusters() {
	while (firstUnwrittenClusterIndex < currentRecordClusterIndex) {

		int error = writeOneCompletedCluster();

		// If there was an error, we can only return now after removing that reason, because we'd already incremented firstUnwrittenClusterIndex, and we can't leave that incremented without removing the reason
		if (error) return error;
	}

	return NO_ERROR;
}

int SampleRecorder::writeOneCompletedCluster() {
	int32_t writingClusterIndex = firstUnwrittenClusterIndex;

#if ALPHA_OR_BETA_VERSION
	// Trying to pin down E347 which Leo got, below
	Cluster* cluster = sample->clusters.getElement(writingClusterIndex)->loadedSampleChunk;
	if (!cluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E374");
#endif

	firstUnwrittenClusterIndex++; // Have to increment this before writing, cos while writing, the audio routine will be called, and we need to be counting this cluster as "written", as in too late for it to be modified (by writing a final length to it)

	int error = writeCluster(writingClusterIndex, sampleManager.clusterSize);

	// We no longer have a reason to require this Cluster to be kept in memory
	if (!keepingReasonsForFirstClusters || writingClusterIndex >= NUM_SAMPLE_CHUNKS_LOADED_AHEAD) {
		Cluster* cluster = sample->clusters.getElement(writingClusterIndex)->loadedSampleChunk;

		// Some bug-hunting
		if (!cluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E347"); // Leo got!!! And Vinz, and keyman. May be solved now that fixed so detachSample() doesn't get called during card routine.
		cluster->numReasonsHeldBySampleRecorder--;

		sampleManager.removeReasonFromLoadedSampleChunk(cluster, "E015");
	}

	// If there was an error, we can only return now after removing that reason, because we'd already incremented firstUnwrittenClusterIndex, and we can't leave that incremented without removing the reason
	return error;
}


int SampleRecorder::finalizeRecordedFile() {

	if (ALPHA_OR_BETA_VERSION && (status == RECORDER_STATUS_ABORTED || hadCardError)) numericDriver.freezeWithError("E273");

	Uart::println("finalizing");

	// In the very rare case where we've already got between 1 and 5 bytes overhanging the end of our current cluster, we need to allocate a new one right now
	int bytesTilClusterEnd = (uint32_t)clusterEndPos - (uint32_t)writePos;
	if (bytesTilClusterEnd < 0) {
		int error = createNextCluster();

		if (error == ERROR_MAX_FILE_SIZE_REACHED) {} // So incredibly unlikely. But no real problem - we maybe just lose a byte or two

		else if (error) return error;

		else { // No error
			// Having just created a new cluster, there'll be one more completed one to write
			error = writeAnyCompletedClusters();
			if (error) return error;
		}
	}

	// And we probably need to write some of the final cluster(s) to file. (If it's NULL, it means that it couldn't be created, cos or RAM or file size limit.)
	if (currentRecordCluster) {

		int bytesToWrite = (uint32_t)writePos - (uint32_t)currentRecordCluster->data;
		if (bytesToWrite > 0) { // Will always be true
			int error = writeCluster(currentRecordClusterIndex, bytesToWrite);
			if (error) return error;
		}

		firstUnwrittenClusterIndex++;

		// Having incremented firstUnwrittenClusterIndex, we need to remove the "reason" for that final cluster.
		// Normally that happens in writeAnyCompletedClusters(), but well this cluster wasn't "complete" so we're doing the whole thing here instead
		if (!keepingReasonsForFirstClusters || currentRecordClusterIndex >= NUM_SAMPLE_CHUNKS_LOADED_AHEAD) {

			// Some bug-hunting
			if (!currentRecordCluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E348");
			currentRecordCluster->numReasonsHeldBySampleRecorder--;

			sampleManager.removeReasonFromLoadedSampleChunk(currentRecordCluster, "E047");
		}
		currentRecordClusterIndex++; // We've finished with that cluster
		currentRecordCluster = NULL; // But currentRecordClusterIndex now refers to a cluster that'll never exist
	}

	uint32_t idealFileSizeBeforeAction = sample->audioDataStartPosBytes + sample->audioDataLengthBytes;
	uint32_t dataLengthBeforeAction = sample->audioDataLengthBytes;

	// Figure out what processing needs to happen on the recorded audio
	int action = 0;
	int lshiftAmount = 0;

	if (allowFileAlterationAfter && idealFileSizeBeforeAction <= 67108864) { // Arbitrarily, don't alter files bigger than 64MB
		if (recordingNumChannels == 1) action = 0;
		else {
			// If R is really quiet or is nearly identical to L, delete R
			if (inputHasNoRightChannel() || recordSumLMinusR < (recordSumL >> 6)) {
				Uart::println("removing right channel");
				action = ACTION_REMOVE_RIGHT_CHANNEL;
			}

			// Or, if R is the differential signal of L, do that
			else if (mode < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION && AudioEngine::lineInPluggedIn && inputLooksDifferential()) {
				Uart::println("subtracting right channel");
				action = ACTION_SUBTRACT_RIGHT_CHANNEL;
			}

			else {
				Uart::println("keeping right channel");
				action = 0;
			}
		}

		uint32_t maxPeak;
		if (action == ACTION_SUBTRACT_RIGHT_CHANNEL) maxPeak = -1 - recordPeakLMinusR;
		else maxPeak = -1 - getMin(recordPeakL, recordPeakR);

		for (lshiftAmount = 0; ((uint32_t)2147483648 >> (lshiftAmount + 1)) > maxPeak; lshiftAmount++) {}
	}
	uint32_t dataLengthAfterAction = action ? (dataLengthBeforeAction >> 1) : dataLengthBeforeAction;

	// TODO: in a perfect world, where we're not deleting a channel, we'd go backwards from the last chunk, because that's the most likely to still be in memory

	// If some processing of the recorded audio data needs to happen...
	if (lshiftAmount || action) {

		FRESULT result = f_close(&file);
		if (result) return ERROR_SD_CARD;

		int error = alterFile(action, lshiftAmount, idealFileSizeBeforeAction, dataLengthAfterAction);
		if (error) return error;
	}

	// Or if no action or shifting was required...
	else {

		// If we made the file too long, because we then compensated for button latency and are throwing away the last little bit, then truncate it
		if (capturedTooMuch) {
			Uart::println("truncating");
			uint32_t correctLength = sample->audioDataStartPosBytes + sample->audioDataLengthBytes; // These were written to in totalSampleLengthNowKnown().
			int error = truncateFileDownToSize(correctLength);
		}

		FRESULT result = f_close(&file);
		if (result) return ERROR_SD_CARD;

		// If the actual audio data length we ended up with is not the same as was written in the headers in the first cluster (very likely; various reasons)
		if (sample->audioDataLengthBytes != audioDataLengthBytesAsWrittenToFile
				|| (recordingExtraMargins && sample->fileLoopEndSamples != loopEndSampleAsWrittenToFile)) {

			// Update data length as written in first cluster
			SampleCluster* firstSampleCluster = sample->clusters.getElement(0);
			Cluster* loadedSampleChunk = firstSampleCluster->getLoadedSampleChunk(sample, 0, CHUNK_LOAD_IMMEDIATELY); // Remember, this adds a "reason"
			if (loadedSampleChunk) {

				// Bug hunting - newly gotten Cluster
				loadedSampleChunk->numReasonsHeldBySampleRecorder++;

				// Do a last-ditch check that the SD address doesn't look invalid
				if (firstSampleCluster->sdAddress == 0) numericDriver.freezeWithError("E268");
				if ((firstSampleCluster->sdAddress - fileSystemStuff.fileSystem.database) & (fileSystemStuff.fileSystem.csize - 1)) numericDriver.freezeWithError("E269");

				audioDataLengthBytesAsWrittenToFile = sample->audioDataLengthBytes;
				loopEndSampleAsWrittenToFile = sample->fileLoopEndSamples;
				updateDataLengthInFirstCluster(loadedSampleChunk);

				// Write just that one first sector back to the card
				disk_write(0, (BYTE*)loadedSampleChunk->data, firstSampleCluster->sdAddress, 1);

				// If that failed, well, that's a shame, but we don't need to do anything

				// Some bug-hunting
				if (!loadedSampleChunk->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E349");
				loadedSampleChunk->numReasonsHeldBySampleRecorder--;

				sampleManager.removeReasonFromLoadedSampleChunk(loadedSampleChunk, "E026");
			}
		}
	}

	sample->numChannels = (action != 0 || recordingNumChannels == 1) ? 1 : 2;
	sample->lengthInSamples = dataLengthAfterAction / (sample->byteDepth * sample->numChannels);
	sample->audioDataLengthBytes = sample->lengthInSamples * (sample->byteDepth * sample->numChannels); // Ensure whole number of samples (surely it already would be though?)

	if (sample->tempFilePathForRecording.isEmpty()) {
		sampleBrowser.lastFilePathLoaded.set(&sample->filePath);
	}

	return NO_ERROR;
}


void SampleRecorder::updateDataLengthInFirstCluster(Cluster* loadedSampleChunk) {
	uint32_t data32;

	// Write top-level RIFF chunk size
	*(uint32_t*)&loadedSampleChunk->data[4] = audioDataLengthBytesAsWrittenToFile + sample->audioDataStartPosBytes - 8;

	// Write data chunk size
	*(uint32_t*)&loadedSampleChunk->data[sample->audioDataStartPosBytes - 4] = audioDataLengthBytesAsWrittenToFile;

	if (recordingExtraMargins) {
		// Write loop end point
		*(uint32_t*)&loadedSampleChunk->data[92] = loopEndSampleAsWrittenToFile;
	}
}

extern int pendingGlobalMIDICommandNumClustersWritten;

// You'll want to remove the "reason" after calling this
int SampleRecorder::writeCluster(int32_t clusterIndex, int numBytes) {
	//Uart::println("writeCluster");

	SampleCluster* sampleCluster = sample->clusters.getElement(clusterIndex);

	UINT numBytesWritten;
	FRESULT result = f_write(&file, sampleCluster->loadedSampleChunk->data, numBytes, &numBytesWritten);

	if (result || numBytes != numBytesWritten) {
		return ERROR_SD_CARD;
	}

	sampleCluster = sample->clusters.getElement(clusterIndex); // MUST re-get this - while writing above, the audio routine is being called, and that could allocate new SampleClusters and move them around!

	// Grab the SD address, for later
	sampleCluster->sdAddress = clst2sect(&fileSystemStuff.fileSystem, file.clust);
	return NO_ERROR;
}


int SampleRecorder::createNextCluster() {

	Cluster* oldRecordCluster = currentRecordCluster; // Cos we're gonna set that to NULL just below here, but still want to be able to access the old one a bit further down

	currentRecordClusterIndex++;	// Mark record-cluster we were on as finished

	currentRecordCluster = NULL;	// Note that we haven't yet created our next record-cluster - we'll do that below if no error first; and if there is an error and we
									// don't create one, this has to remain NULL to indicate that we never created one

	// If this new cluster would actually put us past the 4GB limit...
	if (currentRecordClusterIndex >= (1 << (MAX_FILE_SIZE_MAGNITUDE - sampleManager.clusterSizeMagnitude))) {

		// See if we actually already had any bytes to write into that new cluster we can't have...
		int bytesTilClusterEnd = (uint32_t)clusterEndPos - (uint32_t)writePos;
		if (bytesTilClusterEnd < 0) {
			numSamplesCaptured--;
			writePos -= (int)recordingNumChannels * 3;
		}

		totalSampleLengthNowKnown(numSamplesCaptured, numSamplesCaptured);

		reachedMaxFileSize = true;
		return ERROR_MAX_FILE_SIZE_REACHED;
	}


	// We need to allocate our next Cluster
	int error = sample->clusters.insertSampleClustersAtEnd(1);
	if (error) return error;

	currentRecordCluster = sample->clusters.getElement(currentRecordClusterIndex)->getLoadedSampleChunk(sample, currentRecordClusterIndex, CHUNK_DONT_LOAD);

	// If couldn't allocate loadedSampleChunk (would normally only happen if no SD card present so recording only to RAM)
	if (!currentRecordCluster) {
		Uart::println("SampleRecorder::createNextCluster() fail");
		return ERROR_INSUFFICIENT_RAM;
	}

	// Bug hunting - newly gotten Cluster
	if (currentRecordCluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E362");
	currentRecordCluster->numReasonsHeldBySampleRecorder++;

	// Copy those extra bytes from the end of the old record cluster to the start of the new cluster
	memcpy(currentRecordCluster->data, &oldRecordCluster->data[sampleManager.clusterSize], 5); // 5 is the max number of bytes we could have overshot

	int bytesOvershot = (uint32_t)writePos - (uint32_t)clusterEndPos;

	currentRecordCluster->loaded = true; // I think this is ok - mark it as loaded even though we're yet to record into it

	writePos = (char*)&currentRecordCluster->data[bytesOvershot];
	clusterEndPos = (char*)&currentRecordCluster->data[sampleManager.clusterSize];

	return NO_ERROR;
}


// Gets called when we've captured all the samples of audio that we wanted - either as a direct result of user action, or after being fed a few more samples
// to make up for latency.
void SampleRecorder::finishCapturing() {
	status = RECORDER_STATUS_FINISHED_CAPTURING_BUT_STILL_WRITING;
	if (getRootUI()) getRootUI()->sampleNeedsReRendering(sample);
}


// Only call this after checking that status < RECORDER_STATUS_FINISHED_CAPTURING_BUT_STILL_WRITING
// Watch out - this could be called during SD writing - including during cardRoutine() for this class!
void SampleRecorder::feedAudio(int32_t* __restrict__ inputAddress, int numSamples, bool applyGain) {

	do {
		int numSamplesThisCycle = numSamples;
		if (ALPHA_OR_BETA_VERSION && numSamplesThisCycle <= 0) numericDriver.freezeWithError("cccc");

		// If haven't actually started recording yet cos we're compensating for lag...
		if (numSamplesBeenRunning < (uint32_t)numSamplesToRunBeforeBeginningCapturing) {
			int numSamplesTilBeginRecording = numSamplesToRunBeforeBeginningCapturing - numSamplesBeenRunning;
			numSamplesThisCycle = getMin(numSamplesThisCycle, numSamplesTilBeginRecording);
		}

		// Or, if properly recording...
		else {
			int samplesLeft;

			if (status == RECORDER_STATUS_CAPTURING_DATA_WAITING_TO_STOP) {

				samplesLeft = sample->lengthInSamples - numSamplesCaptured;
				if (samplesLeft <= 0) {
doFinishCapturing:
					finishCapturing();
					return;
				}

				numSamplesThisCycle = getMin(numSamplesThisCycle, samplesLeft);
			}
			if (ALPHA_OR_BETA_VERSION && numSamplesThisCycle <= 0) numericDriver.freezeWithError("bbbb");


			int bytesPerSample = recordingNumChannels * 3;
			int bytesWeWantToWrite = numSamplesThisCycle * bytesPerSample;

			int bytesTilClusterEnd = (uint32_t)clusterEndPos - (uint32_t)writePos;

			// If we need a new cluster right now...
			if (bytesTilClusterEnd <= 0) {
				int error = createNextCluster();
				if (error == ERROR_MAX_FILE_SIZE_REACHED) {
					goto doFinishCapturing;
				}
				else if (error) { // RAM error
					Uart::println("couldn't allocate RAM");
					abort();
					return;
				}

				bytesTilClusterEnd = (uint32_t)clusterEndPos - (uint32_t)writePos; // Recalculate it
			}

			if (bytesTilClusterEnd <= bytesWeWantToWrite - bytesPerSample) {
				int samplesTilClusterEnd = (uint16_t)(bytesTilClusterEnd - 1) / (uint8_t)bytesPerSample + 1; // Rounds up
				numSamplesThisCycle = getMin(numSamplesThisCycle, samplesTilClusterEnd);
			}

			if (ALPHA_OR_BETA_VERSION && numSamplesThisCycle <= 0) numericDriver.freezeWithError("aaaa");

			int32_t* endInputNow = inputAddress + (numSamplesThisCycle << NUM_MONO_INPUT_CHANNELS_MAGNITUDE);


			char* __restrict__ writePosNow = writePos;

			// Balanced input. For this, we skip a bunch of stat-grabbing, cos we knob this is just for AudioClips.
			// We also know that applyGain is false - that's just for the MIX option
			if (mode == AUDIO_INPUT_CHANNEL_BALANCED) {

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
					if (applyGain) rxL = lshiftAndSaturate(rxL, 5);

					char* __restrict__ readPos = (char*)&rxL + 1;
					*(writePosNow++) = *(readPos++);
					*(writePosNow++) = *(readPos++);
					*(writePosNow++) = *(readPos++);

					if (rxL > recordMax) recordMax = rxL;
					if (rxL < recordMin) recordMin = rxL;

					int32_t absL;
					if (rxL >= 0) absL = rxL;
					else absL = -1 - rxL;
					recordSumL += absL;

					if (rxL < recordPeakL) recordPeakL = rxL;
					else if (-rxL < recordPeakL) recordPeakL = -rxL;
					if (rxL == 2147483647 || rxL == -2147483648) recordingClippedRecently = true;

					if (recordingNumChannels == 2) {
						int32_t rxR = *(inputAddress + 1);
						if (applyGain) rxR = lshiftAndSaturate(rxR, 5);

						readPos = (char*)&rxR + 1;
						*(writePosNow++) = *(readPos++);
						*(writePosNow++) = *(readPos++);
						*(writePosNow++) = *(readPos++);

						if (rxR > recordMax) recordMax = rxR;
						if (rxR < recordMin) recordMin = rxR;

						if (rxR >= 0) recordSumR += rxR;
						else recordSumR += -1 - rxR;

						int32_t lPlusR = (rxL >> 1) + (rxR >> 1);
						if (lPlusR >= 0) recordSumLPlusR += lPlusR;
						else recordSumLPlusR += -1 - lPlusR;

						int32_t lMinusR = (rxL >> 1) - (rxR >> 1);
						if (lMinusR >= 0) recordSumLMinusR += lMinusR;
						else recordSumLMinusR += -1 - lMinusR;

						if (rxR < recordPeakR) recordPeakR = rxR;
						else if (-rxR < recordPeakR) recordPeakR = -rxR;
						if (rxR == 2147483647 || rxR == -2147483648) recordingClippedRecently = true;

						if (lMinusR < recordPeakLMinusR) recordPeakLMinusR = lMinusR;
						else if (-lMinusR < recordPeakLMinusR) recordPeakLMinusR = -lMinusR;
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



void SampleRecorder::endSyncedRecording(int buttonLatencyForTempolessRecording) {
#if ALPHA_OR_BETA_VERSION
	if (status == RECORDER_STATUS_CAPTURING_DATA_WAITING_TO_STOP) numericDriver.freezeWithError("E272");
	else if (status == RECORDER_STATUS_FINISHED_CAPTURING_BUT_STILL_WRITING) numericDriver.freezeWithError("E288");
	else if (status == RECORDER_STATUS_COMPLETE) numericDriver.freezeWithError("E289");
	else if (status == RECORDER_STATUS_ABORTED) numericDriver.freezeWithError("E290");
	else if (status == RECORDER_STATUS_AWAITING_DELETION) numericDriver.freezeWithError("E291");
#endif

	int numMoreSamplesTilEndLoopPoint = numSamplesExtraToCaptureAtEndSyncingWise - buttonLatencyForTempolessRecording;
	int numMoreSamplesToCapture = numMoreSamplesTilEndLoopPoint;

	Uart::print("buttonLatencyForTempolessRecording: ");
	Uart::println(buttonLatencyForTempolessRecording);

	if (recordingExtraMargins) numMoreSamplesToCapture += AUDIO_CLIP_MARGIN_SIZE_POST_END; // Means we also have an audioClip

	uint32_t loopEndPointSamples = numSamplesCaptured + numMoreSamplesTilEndLoopPoint;

	totalSampleLengthNowKnown(numSamplesCaptured + numMoreSamplesToCapture, loopEndPointSamples);

	if (numMoreSamplesToCapture <= 0) {
		if (numMoreSamplesToCapture < 0) {
			capturedTooMuch = true;
			Uart::println("captured too much.");
		}
		finishCapturing();
	}
	else {
		status = RECORDER_STATUS_CAPTURING_DATA_WAITING_TO_STOP;
	}
}


void SampleRecorder::totalSampleLengthNowKnown(uint32_t totalLengthSamples, uint32_t loopEndPointSamples) {

	sample->lengthInSamples = totalLengthSamples;
	sample->audioDataLengthBytes = totalLengthSamples * sample->byteDepth * sample->numChannels;

	sample->fileLoopEndSamples = loopEndPointSamples;

	// If we haven't written the first cluster yet, quick - update it with the actual length
	if (firstUnwrittenClusterIndex == 0) {
		SampleCluster* firstSampleCluster = sample->clusters.getElement(0);
		Cluster* loadedSampleChunk = firstSampleCluster->loadedSampleChunk; // It should still be there, cos it hasn't been written to card yet
		if (ALPHA_OR_BETA_VERSION && !loadedSampleChunk) numericDriver.freezeWithError("E274");

		audioDataLengthBytesAsWrittenToFile = sample->audioDataLengthBytes;
		loopEndSampleAsWrittenToFile = sample->fileLoopEndSamples; // Even if we're not actually writing loop points to the file, this is harmless
		updateDataLengthInFirstCluster(loadedSampleChunk);
	}
}



bool SampleRecorder::inputLooksDifferential() {
	return (recordSumLPlusR < (recordSumL >> 4));
}

bool SampleRecorder::inputHasNoRightChannel() {
	return (recordSumR < (recordSumL >> 6));
}

// Only call this if currentRecordCluster points to a real cluster
void SampleRecorder::setExtraBytesOnPreviousCluster(Cluster* currentCluster, int currentClusterIndex) {
	if (currentClusterIndex <= 0) return;

	Cluster* prevCluster = sample->clusters.getElement(currentClusterIndex - 1)->loadedSampleChunk;

	// It might have since been deallocated, which is just fine. But if not...
	if (prevCluster) {
		memcpy(&prevCluster->data[sampleManager.clusterSize], currentCluster->data, 5);
	}
}

int SampleRecorder::alterFile(int action, int lshiftAmount, uint32_t idealFileSizeBeforeAction, uint64_t dataLengthAfterAction) {

	Uart::println("altering file");
	int currentReadClusterIndex = 0;
	int currentWriteClusterIndex = 0;

	Cluster* currentReadCluster = sample->clusters.getElement(0)->getLoadedSampleChunk(sample, 0, CHUNK_LOAD_IMMEDIATELY); // Remember, this adds a "reason"
	if (!currentReadCluster) {
		return ERROR_SD_CARD;
	}

	// Bug hunting - newly gotten Cluster
	currentReadCluster->numReasonsHeldBySampleRecorder++;

	int numClustersBeforeAction = ((idealFileSizeBeforeAction - 1) >> sampleManager.clusterSizeMagnitude) + 1; // Rounds up
	if (ALPHA_OR_BETA_VERSION && numClustersBeforeAction > sample->clusters.getNumElements()) numericDriver.freezeWithError("E286");

	Cluster* nextReadCluster = NULL;

	if (numClustersBeforeAction >= 2) {
		nextReadCluster = sample->clusters.getElement(1)->getLoadedSampleChunk(sample, 1, CHUNK_LOAD_IMMEDIATELY); // Remember, this adds a "reason"
		if (!nextReadCluster) {

			// Some bug-hunting
			if (!currentReadCluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E350");
			currentReadCluster->numReasonsHeldBySampleRecorder--;

			sampleManager.removeReasonFromLoadedSampleChunk(currentReadCluster, "E017");
			return ERROR_SD_CARD;
		}

		// Bug hunting - newly gotten Cluster
		nextReadCluster->numReasonsHeldBySampleRecorder++;
	}

	Cluster* currentWriteCluster = sample->clusters.getElement(0)->getLoadedSampleChunk(sample, 0, CHUNK_DONT_LOAD); // Remember, this adds a "reason"
	// That one can't fail, fortunately, cos we already grabbed chunk 0 above, so it exists

	// Bug hunting - newly gotten Cluster
	currentWriteCluster->numReasonsHeldBySampleRecorder++;

	uint32_t data32;
	uint16_t data16;

	audioDataLengthBytesAsWrittenToFile = dataLengthAfterAction;
	loopEndSampleAsWrittenToFile = sample->fileLoopEndSamples;
	updateDataLengthInFirstCluster(currentWriteCluster);

	if (action) {
		// Write num channels
		data16 = 1;
		memcpy(&currentWriteCluster->data[22], &data16, 2);

		// Data rate
		data32 = 44100 * 1 * 3;
		memcpy(&currentWriteCluster->data[28], &data32, 4);

		// Data block size
		data16 = 1 * 3;
		memcpy(&currentWriteCluster->data[32], &data16, 2);
	}

	char* readPos = &currentReadCluster->data[sample->audioDataStartPosBytes];
	char* writePos = &currentWriteCluster->data[sample->audioDataStartPosBytes];

	unsigned int bytesFinalChunk = idealFileSizeBeforeAction & (sampleManager.clusterSize - 1);
	if (bytesFinalChunk == 0) bytesFinalChunk = sampleManager.clusterSize;

	uint32_t count = 0;

	// TODO: this is really inefficient - checks a bunch of stuff for every single audio sample. Should check in advance how many samples we can process at a time

	while (true) {

		if (!(count & 0b11111111)) { // 10x 1's seems to work ok. So we go down to 8 to be sure
		    AudioEngine::routineWithChunkLoading();

			uiTimerManager.routine();

			uartFlushIfNotSending(UART_ITEM_PIC);
		}

		count++;

		int32_t* input = (int32_t*)(readPos - 1);
		readPos += 3;
		int32_t value = *input & 0xFFFFFF00;

		if (action == ACTION_SUBTRACT_RIGHT_CHANNEL) {
			input = (int32_t*)(readPos - 1);
			readPos += 3;
			value = (value >> 1) - ((int32_t)(*input & 0xFFFFFF00) >> 1);
		}

		else if (action == ACTION_REMOVE_RIGHT_CHANNEL) {
			readPos += 3;
		}
		int32_t processed = value << lshiftAmount;

		char* processedPos = (char*)&processed + 1;
		*(writePos++) = *(processedPos++);
		*(writePos++) = *(processedPos++);
		*(writePos++) = *(processedPos++);


		// If need to advance write-head past the end of a cluster, then we'll write that current cluster to disk and carry on
		int writeOvershot = (uint32_t)writePos - (uint32_t)&currentWriteCluster->data[sampleManager.clusterSize];
		if (writeOvershot >= 0) {

			// If reached very end of file, break
			if (currentWriteClusterIndex == numClustersBeforeAction - 1) break;

			Uart::println("write advance");

			currentWriteCluster->loaded = true; // I don't think this is necessary anymore

			uint32_t sdAddress = sample->clusters.getElement(currentWriteClusterIndex)->sdAddress;

			// Do a last-ditch check that the SD address doesn't look invalid
			if (sdAddress == 0) numericDriver.freezeWithError("E268");
			if ((sdAddress - fileSystemStuff.fileSystem.database) & (fileSystemStuff.fileSystem.csize - 1)) numericDriver.freezeWithError("E275");

			// Write the chunk we just finished processing to card
			DRESULT result = disk_write(
					0,
					(BYTE*)currentWriteCluster->data,
					sdAddress,
					sampleManager.clusterSize >> 9);

			// Grab any overshot / extra bytes from the end of the chunk we just finished...
			uint8_t extraBytes[5]; // 5 is the max number of bytes we could have overshot
			if (writeOvershot) memcpy(extraBytes, &currentWriteCluster->data[sampleManager.clusterSize], writeOvershot);

			// And from the chunk we just finished, give the chunk *before that* the extra bytes from its start
			setExtraBytesOnPreviousCluster(currentWriteCluster, currentWriteClusterIndex);

			// We don't need that old chunk anymore

			// Some bug-hunting
			if (!currentWriteCluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E351");
			currentWriteCluster->numReasonsHeldBySampleRecorder--;

			sampleManager.removeReasonFromLoadedSampleChunk(currentWriteCluster, "E023");
			currentWriteCluster = NULL;

			// If write operation failed, now's the time to get out
			if (result) {
writeFailed:
				// Before we get out, remove "reasons" from the loadedSampleChunks we've been reading from

				// Some bug-hunting
				if (!currentReadCluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E352");
				currentReadCluster->numReasonsHeldBySampleRecorder--;

				sampleManager.removeReasonFromLoadedSampleChunk(currentReadCluster, "E024");

				if (nextReadCluster) {
					// Some bug-hunting
					if (!nextReadCluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E353");
					nextReadCluster->numReasonsHeldBySampleRecorder--;

					sampleManager.removeReasonFromLoadedSampleChunk(nextReadCluster, "E025");
				}
				return ERROR_SD_CARD;
			}

			// Ok, move on and start thinking about the next chunk now
			currentWriteClusterIndex++;

			// Get the new / next chunk's LoadedSampleChunk, but don't insist on actually reading from the card, cos we're gonna overwrite it with new data anyway
			currentWriteCluster = sample->clusters.getElement(currentWriteClusterIndex)->getLoadedSampleChunk(sample, currentWriteClusterIndex, CHUNK_DONT_LOAD); // Remember, this adds a "reason"

			// That could only fail if no RAM, but juuuust in case...
			if (!currentWriteCluster) {
				goto writeFailed;
			}

			// Bug hunting - newly gotten Cluster
			currentWriteCluster->numReasonsHeldBySampleRecorder++;

			// Ok, and those extra bytes that we grabbed from the end of the previous chunk - paste them into the beginning of the new current chunk
			if (writeOvershot) memcpy(currentWriteCluster->data, extraBytes, writeOvershot);

			// And get ready to write to the new current chunk - from the next sample, which might not be perfectly aligned to the chunk start
			writePos = &currentWriteCluster->data[writeOvershot];
		}


		// If we're in the final read-chunk and reached the end, then all that's left to do is flush out what we have left to write (max 1 cluster),
		// and get out.
		if (currentReadClusterIndex == numClustersBeforeAction - 1 && readPos >= &currentReadCluster->data[bytesFinalChunk]) {
			break;
		}

		// Advance read-head. We read one chunk ahead, so we can access its "extra bytes"
		if (readPos >= &currentReadCluster->data[sampleManager.clusterSize]) {

			Uart::println("read advance");

			int overshot = (uint32_t)readPos - (uint32_t)&currentReadCluster->data[sampleManager.clusterSize];

			// Some bug-hunting
			if (!currentReadCluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E354");
			currentReadCluster->numReasonsHeldBySampleRecorder--;

			sampleManager.removeReasonFromLoadedSampleChunk(currentReadCluster, "E020");
			currentReadClusterIndex++;
			currentReadCluster = nextReadCluster;

			// If there are further read chunks...
			if (currentReadClusterIndex < numClustersBeforeAction - 1) {
				nextReadCluster = sample->clusters.getElement(currentReadClusterIndex + 1)->getLoadedSampleChunk(sample, currentReadClusterIndex + 1, CHUNK_LOAD_IMMEDIATELY); // Remember, this adds a "reason"

				// If that failed, remove other reasons and get out
				if (!nextReadCluster) {

					// Some bug-hunting
					if (!currentReadCluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E355");
					currentReadCluster->numReasonsHeldBySampleRecorder--;

					sampleManager.removeReasonFromLoadedSampleChunk(currentReadCluster, "E021");

					// Some bug-hunting
					if (!currentWriteCluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E356");
					currentWriteCluster->numReasonsHeldBySampleRecorder--;

					sampleManager.removeReasonFromLoadedSampleChunk(currentWriteCluster, "E022");
					currentWriteCluster = NULL;
					return ERROR_SD_CARD;
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
	if (!currentReadCluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E357");
	currentReadCluster->numReasonsHeldBySampleRecorder--;

	sampleManager.removeReasonFromLoadedSampleChunk(currentReadCluster, "E018");
	// We know that finishedAlteringFile must be NULL

	currentWriteCluster->loaded = true;

	unsigned int bytesToWriteFinalChunk = (uint32_t)writePos - (uint32_t)currentWriteCluster->data;

	if (bytesToWriteFinalChunk) { // If there is in fact anything to flush out to the file / card...

		// And from this final chunk, give the chunk *before that* the extra bytes from its start
		setExtraBytesOnPreviousCluster(currentWriteCluster, currentWriteClusterIndex);

		unsigned int numSectorsToWrite = ((bytesToWriteFinalChunk - 1) >> 9) + 1;
		if (numSectorsToWrite > (sampleManager.clusterSize >> 9)) numericDriver.freezeWithError("E239");

		uint32_t sdAddress = sample->clusters.getElement(currentWriteClusterIndex)->sdAddress;

		// Do a last-ditch check that the SD address doesn't look invalid
		if (sdAddress == 0) numericDriver.freezeWithError("E268");
		if ((sdAddress - fileSystemStuff.fileSystem.database) & (fileSystemStuff.fileSystem.csize - 1)) numericDriver.freezeWithError("E276");

		DRESULT result = disk_write(0,
				(BYTE*)currentWriteCluster->data,
				sdAddress,
				numSectorsToWrite);

		// Some bug-hunting
		if (!currentWriteCluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E358");
		currentWriteCluster->numReasonsHeldBySampleRecorder--;

		sampleManager.removeReasonFromLoadedSampleChunk(currentWriteCluster, "E019");
		currentWriteCluster = NULL;

		// If writing disk failed, above, we've now removed that "reason", so we can get out
		if (result) return ERROR_SD_CARD;

		if (action || capturedTooMuch) {

			FRESULT fres = f_open(&file, sample->filePath.get(), FA_WRITE);
			if (fres) return ERROR_SD_CARD;

			int error = truncateFileDownToSize(dataLengthAfterAction + sample->audioDataStartPosBytes);
			if (error) return error;

			fres = f_close(&file);
			if (fres) return ERROR_SD_CARD;
		}
	}
	else { // Or if there was nothing further to write (very rare)...

		// Some bug-hunting
		if (!currentWriteCluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E359");
		currentWriteCluster->numReasonsHeldBySampleRecorder--;

		sampleManager.removeReasonFromLoadedSampleChunk(currentWriteCluster, "E238");
		currentWriteCluster = NULL;
	}

	return NO_ERROR;
}

// You must still have the file open when you call this
int SampleRecorder::truncateFileDownToSize(uint32_t newFileSize) {

	// Update the Sample object to indicate the correct size. Do this before we risk errors below

	uint64_t numClustersAfterAction = ((newFileSize - 1) >> sampleManager.clusterSizeMagnitude) + 1;


	int numToDelete = sample->clusters.getNumElements() - numClustersAfterAction;
	if (numToDelete > 0) {
		for (int i = numClustersAfterAction; i < sample->clusters.getNumElements(); i++) {
			sample->clusters.getElement(i)->~SampleCluster();
		}
		sample->clusters.deleteAtIndex(numClustersAfterAction, numToDelete);
	}

	// Truncate file size
	FRESULT fres = f_lseek(&file, newFileSize);
	if (fres) return ERROR_SD_CARD;
	fres = f_truncate(&file);
	if (fres) return ERROR_SD_CARD;

	return NO_ERROR;
}
