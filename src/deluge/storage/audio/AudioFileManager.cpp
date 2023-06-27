/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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
#include <AudioFileManager.h>
#include <Cluster.h>
#include "definitions.h"
#include "Sample.h"
#include <string.h>
#include "storagemanager.h"
#include "uart.h"
#include <new>
#include "functions.h"
#include "numericdriver.h"
#include "ActionLogger.h"
#include "GeneralMemoryAllocator.h"
#include "SampleCache.h"
#include "SampleRecorder.h"
#include "playbackhandler.h"
#include "WaveTable.h"
#include "SampleReader.h"
#include "WaveTableReader.h"
#include "MIDIDeviceManager.h"
#include "extern.h"

extern "C" {
#include "ff.h"
#include "diskio.h"

DWORD get_fat_from_fs(                      /* 0xFFFFFFFF:Disk error, 1:Internal error, 2..0x7FFFFFFF:Cluster status */
                      FATFS* fs, DWORD clst /* Cluster number to get the value */
);

LBA_t clst2sect(           /* !=0:Sector number, 0:Failed (invalid cluster#) */
                FATFS* fs, /* Filesystem object */
                DWORD clst /* Cluster# to be converted */
);

DRESULT disk_read_without_streaming_first(BYTE pdrv, BYTE* buff, DWORD sector, UINT count);

extern uint8_t currentlyAccessingCard;
}

char const* const audioRecordingFolderNames[] = {"SAMPLES/CLIPS", "SAMPLES/RECORD", "SAMPLES/RESAMPLE"};

AudioFileManager audioFileManager{};

AudioFileManager::AudioFileManager() {
	cardDisabled = false;
	alternateLoadDirStatus = ALTERNATE_LOAD_DIR_NONE_SET;
	thingTypeBeingLoaded = THING_TYPE_NONE;

	for (int i = 0; i < NUM_AUDIO_RECORDING_FOLDERS; i++) {
		highestUsedAudioRecordingNumber[i] = -1;
		highestUsedAudioRecordingNumberNeedsReChecking[i] = true;
	}
}

void AudioFileManager::init() {

	clusterBeingLoaded = NULL;

	int error = storageManager.initSD();
	if (!error) {
		setClusterSize(fileSystemStuff.fileSystem.csize * 512);

		Uart::print("clusterSize ");
		Uart::println(clusterSize);
		Uart::print("clusterSizeMagnitude ");
		Uart::println(clusterSizeMagnitude);
		cardEjected = false;
	}

	else {
		clusterSize = 32768;
		clusterSizeMagnitude = 15;
		cardEjected = true;
	}

	clusterSizeAtBoot = clusterSize;

	void* temp = generalMemoryAllocator.alloc(clusterSizeAtBoot + CACHE_LINE_SIZE * 2, NULL, false, false);
	storageManager.fileClusterBuffer = (char*)temp + CACHE_LINE_SIZE;

	clusterObjectSize = sizeof(Cluster) + clusterSize;
}

void AudioFileManager::setClusterSize(uint32_t newSize) {
	clusterSize = newSize;
	clusterSizeMagnitude = 9;
	while ((clusterSize >> clusterSizeMagnitude) > 1)
		clusterSizeMagnitude++;
}

void AudioFileManager::cardReinserted() {

	cardDisabled = false;
	for (int i = 0; i < NUM_AUDIO_RECORDING_FOLDERS; i++) {
		highestUsedAudioRecordingNumberNeedsReChecking[i] = true;
	}

	// If cluster size has increased, we're in trouble
	if (fileSystemStuff.fileSystem.csize * 512 > clusterSize) {

		// But, if it's still not as big as it was when we booted up, that's still manageable
		if (fileSystemStuff.fileSystem.csize * 512 <= clusterSizeAtBoot) goto clusterSizeChangedButItsOk;

		Uart::println("cluster size increased and we're in trouble");
		cardDisabled = true;
		numericDriver.displayPopup(HAVE_OLED ? "Reboot to use this SD card" : "DIFF");
	}

	// If cluster size decreased, we have to stop all current samples from ever sounding again. Pretty big trouble really...
	else if (fileSystemStuff.fileSystem.csize * 512 < clusterSize) {

clusterSizeChangedButItsOk:
		Uart::println("cluster size changed, and smaller than original so it's ok");
		AudioEngine::unassignAllVoices(); // Will also stop synth voices - too bad.

		for (int e = 0; e < audioFiles.getNumElements(); e++) {
			AudioFile* thisAudioFile = (AudioFile*)audioFiles.getElement(e);

			// If AudioFile isn't used currently, take this opportunity to remove it from memory
			if (!thisAudioFile->numReasonsToBeLoaded) {
				deleteUnusedAudioFileFromMemory(thisAudioFile, e);
				e--;
			}

			// Otherwise, mark the sample as unplayable
			else {
				if (thisAudioFile->type == AUDIO_FILE_TYPE_SAMPLE) {
					((Sample*)thisAudioFile)->unplayable = true;
				}
			}
		}

		// That was all a pain, but now we can update the cluster size
		setClusterSize(fileSystemStuff.fileSystem.csize * 512);
	}

	// Or if cluster size stayed the same...
	else {
		// Go through every Sample in memory
		for (int e = 0; e < audioFiles.getNumElements(); e++) {

			AudioFile* thisAudioFile = (AudioFile*)audioFiles.getElement(e);

			// If Sample isn't used currently, take this opportunity to remove it from memory
			if (!thisAudioFile->numReasonsToBeLoaded) {
				deleteUnusedAudioFileFromMemory(thisAudioFile, e);
				e--;
			}

			// Or if it is still used by someone...
			else {
				if (thisAudioFile->type == AUDIO_FILE_TYPE_SAMPLE) {
					// Check the Sample's file still exists

					char const* filePath = ((Sample*)thisAudioFile)->tempFilePathForRecording.get();
					if (!*filePath) filePath = thisAudioFile->filePath.get();

					FRESULT result = f_open(&fileSystemStuff.currentFile, filePath, FA_READ);
					if (result != FR_OK) {
						Uart::println("couldn't open file");
						((Sample*)thisAudioFile)->markAsUnloadable();
						continue;
					}

					uint32_t firstSector =
					    clst2sect(&fileSystemStuff.fileSystem, fileSystemStuff.currentFile.obj.sclust);

					f_close(&fileSystemStuff.currentFile);

					// If address of first sector remained unchanged, we can be sure enough that the file hasn't been changed
					if (firstSector == ((Sample*)thisAudioFile)->clusters.getElement(0)->sdAddress) {}

					// Otherwise
					else {
						((Sample*)thisAudioFile)->markAsUnloadable();
						continue;
					}

					// Or if we're still here, the file's fine - who knows, maybe it's even fine again after it wasn't for a while (e.g. if the user temporarily had a different card inserted)
					((Sample*)thisAudioFile)->unloadable = false;
				}
			}
		}
	}

	MIDIDeviceManager::
	    readDevicesFromFile(); // Hopefully we can do this now. It'll only happen if it wasn't able to do it before.
}

// Call this after deleting the current (or in other words previous) Song from memory - meaning there won't be any further reason we'd ever move these temp samples into the permanent sample
// folder, meaning we don't want them in memory listed with their would-be real permanent filenames. Also, we won't be needing to play them back again.
// You must not call this during the card or audio routines.
void AudioFileManager::deleteAnyTempRecordedSamplesFromMemory() {

	// Also though, in case any of these Samples were still being recorded before the Song-delete, we need to make sure SampleRecorder::cardRoutine() gets called first
	// to "detach" the Sample from the recorder. So, do this:
	AudioEngine::doRecorderCardRoutines();

	for (int e = 0; e < audioFiles.getNumElements(); e++) {
		AudioFile* audioFile = (AudioFile*)audioFiles.getElement(e);

		if (audioFile->type == AUDIO_FILE_TYPE_SAMPLE) {
			// If it's a temp-recorded one
			if (!((Sample*)audioFile)->tempFilePathForRecording.isEmpty()) {

				//if (ALPHA_OR_BETA_VERSION && audioFile->numReasons) numericDriver.freezeWithError("E281"); // It definitely shouldn't still have any reasons
				// No - it could still have a reason - the reason of its SampleRecorder. Scenario where this happened was: recording AudioClip (instance)
				// into Arranger when loading a new song, first causes Arranger playback to switch to Session playback, which causes
				// finishLinearRecording() on AudioClip, so when song-swap does happen, the AudioClip no longer has a recorder, so the recorder doesn't clear stuff,
				// and it's still not quite yet finalized the file, so still holds the "reason" to the Sample.
				// TODO: although the Sample doesn't store a pointer to the SampleRecorder, we could easily search for it - and delete it and its "reason"?

				highestUsedAudioRecordingNumberNeedsReChecking[AUDIO_RECORDING_FOLDER_CLIPS] =
				    true; // We know Sample belonged to an AudioClip originally because only those ones can be TEMP
				highestUsedAudioRecordingNumber[AUDIO_RECORDING_FOLDER_CLIPS] =
				    -1; // We may have deleted several, so do make sure we go and re-check from 0

				deleteUnusedAudioFileFromMemory(audioFile, e);
				e--;
			}
		}
	}
}

// Oi, don't even think about modifying this to take a Sample* pointer - cos the whole Sample could get deleted during the card access.
int AudioFileManager::getUnusedAudioRecordingFilePath(String* filePath, String* tempFilePathForRecording, int folderID,
                                                      uint32_t* getNumber) {

	int error = storageManager.initSD();
	if (error) return error;

	if (highestUsedAudioRecordingNumberNeedsReChecking[folderID]) {

		FRESULT result = f_opendir(&staticDIR, audioRecordingFolderNames[folderID]);
		if (result == FR_OK) {

			while (true) {
				loadAnyEnqueuedClusters();
				FRESULT result = f_readdir(&staticDIR, &staticFNO); /* Read a directory item */
				if (__builtin_expect(result != FR_OK, 0)) return ERROR_SD_CARD;

				if (__builtin_expect((*(uint32_t*)staticFNO.altname & 0x00FFFFFF) == 0x00434552, 1)) { // "REC"
					if (*(uint32_t*)&staticFNO.altname[8] == 0x5641572E) {                             // ".WAV"

						int thisSlot = memToUIntOrError(&staticFNO.altname[3], &staticFNO.altname[8]);
						if (thisSlot == -1) continue;

						if (thisSlot > highestUsedAudioRecordingNumber[folderID])
							highestUsedAudioRecordingNumber[folderID] = thisSlot;
					}
				}
				else if (!staticFNO.altname[0]) break; /* Break on end of dir */
			}
			//f_closedir(&staticDIR);
		}

		highestUsedAudioRecordingNumberNeedsReChecking[folderID] = false;
	}

	highestUsedAudioRecordingNumber[folderID]++;

	Uart::print("new file: -------------- ");
	Uart::println(highestUsedAudioRecordingNumber[folderID]);

	error = filePath->set(audioRecordingFolderNames[folderID]);
	if (error) return error;

	bool doingTempFolder = (folderID == AUDIO_RECORDING_FOLDER_CLIPS);
	if (doingTempFolder) {
		error = tempFilePathForRecording->set(audioRecordingFolderNames[folderID]);
		if (error) return error;
		error = tempFilePathForRecording->concatenate("/TEMP");
		if (error) return error;
	}

	error = filePath->concatenate("/REC");
	if (error) return error;
	error = filePath->concatenateInt(highestUsedAudioRecordingNumber[folderID], 5);
	if (error) return error;
	error = filePath->concatenate(".WAV");
	if (error) return error;

	if (doingTempFolder) {
		error = tempFilePathForRecording->concatenate(&filePath->get()[strlen(audioRecordingFolderNames[folderID])]);
		if (error) return error;
	}

	*getNumber = highestUsedAudioRecordingNumber[folderID];

	return NO_ERROR;
}

// Returns false if exists but can't be deleted
bool AudioFileManager::tryToDeleteAudioFileFromMemoryIfItExists(char const* filePath) {
	bool foundExact;

	for (int t = 0; t < 2; t++) { // Got to do this twice, just in case there's a Sample and a WaveTable.

		int i = audioFiles.search(filePath, GREATER_OR_EQUAL, &foundExact);
		if (!foundExact) return true; // We're fine, it didn't exist

		// Ok, it's in memory. Can we delete it - is it unused?
		AudioFile* audioFile = (AudioFile*)audioFiles.getElement(i);
		if (audioFile->numReasonsToBeLoaded)
			return false; // Alert - not only is it in memory, but it also can't be deleted

		// Ok, it's unused. Delete it.
		deleteUnusedAudioFileFromMemory(audioFile, i);
	}
	return true; // We're fine - it got deleted
}

void AudioFileManager::deleteUnusedAudioFileFromMemoryIndexUnknown(AudioFile* audioFile) {
	int i = audioFiles.searchForExactObject(audioFile);
	if (i < 0) {
#if ALPHA_OR_BETA_VERSION
		numericDriver.freezeWithError("E401"); // Leo got. And me! But now I've solved.
#endif
	}
	else {
		deleteUnusedAudioFileFromMemory(audioFile, i);
	}
}

void AudioFileManager::deleteUnusedAudioFileFromMemory(AudioFile* audioFile, int i) {

	// Remove AudioFile from memory
	audioFiles.removeElement(i);
	//audioFile->remove(); // Remove from the unused AudioFiles list, where this already must have been. Actually no, the destructor does this anyway.
	audioFile->~AudioFile();
	generalMemoryAllocator.dealloc(audioFile);
}

bool AudioFileManager::ensureEnoughMemoryForOneMoreAudioFile() {

	return audioFiles.ensureEnoughSpaceAllocated(1);
}

int AudioFileManager::setupAlternateAudioFileDir(String* newPath, char const* rootDir,
                                                 String* songFilenameWithoutExtension) {

	int error = newPath->set(rootDir);
	if (error) return error;

	error = newPath->concatenate("/");
	if (error) return error;

	error = newPath->concatenate(songFilenameWithoutExtension);
	if (error) return error;

	return NO_ERROR;
}

int AudioFileManager::setupAlternateAudioFilePath(String* newPath, int dirPathLength, String* oldPath) {
	int error = newPath->concatenateAtPos(&oldPath->get()[8], dirPathLength); // The [8] skips us past "SAMPLES/"
	if (error) return error;

	int pos = dirPathLength;

	while (true) {
		char const* newPathChars = newPath->get();
		char const* slashAddress = strchr(&newPathChars[pos], '/');
		if (!slashAddress) break;
		int slashPos = (uint32_t)slashAddress - (uint32_t)newPathChars;
		int error = newPath->setChar('_', slashPos);
		if (error) return error;
		pos = slashPos + 1;
	}

	return NO_ERROR;
}

AudioFile* AudioFileManager::getAudioFileFromFilename(String* filePath, bool mayReadCard, uint8_t* error,
                                                      FilePointer* suppliedFilePointer, int type,
                                                      bool makeWaveTableWorkAtAllCosts) {

	*error = NO_ERROR;

	String backedUpFilePath;

	// See if it's already in memory.
	bool foundExact;
	int audioFileI = audioFiles.search(filePath->get(), GREATER_OR_EQUAL, &foundExact);

	// If that basic search by the file's "normal" path already found it, then great.
	if (foundExact) {
successfullyFoundInMemory:
		AudioFile* foundAudioFile = (AudioFile*)audioFiles.getElement(audioFileI);

		// If correct type...
		if (foundAudioFile->type == type) {
			return foundAudioFile;
		}

		// Otherwise, see if a neighbouring one has the right type
		int tryOffset = -1;

		if (audioFileI >= 1) {
doTryOffset:
			AudioFile* foundAudioFile2 = (AudioFile*)audioFiles.getElement(audioFileI + tryOffset);

			if (foundAudioFile2->type == type && !strcasecmp(filePath->get(), foundAudioFile2->filePath.get())) {
				return foundAudioFile2;
			}
		}

		if (tryOffset == -1 && audioFileI < audioFiles.getNumElements() - 1) {
			tryOffset = 1;
			goto doTryOffset;
		}

		// If here, we didn't find the correct type, but we did find an AudioFile for the correct filePath, just the wrong type.

		// If we want WaveTable but got Sample, we can convert. (Otherwise, we can't.)
		if (type == AUDIO_FILE_TYPE_WAVETABLE) {

			// Stereo files can never be WaveTables
			if (((Sample*)foundAudioFile)->numChannels != 1) {
				*error = ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO;
				return NULL;
			}

			// And if the user isn't insisting, then some other signs show that we probably don't want to load this as a WaveTable
			if (!makeWaveTableWorkAtAllCosts) {
				if (isAiffFilename(foundAudioFile->filePath.get())) {
notLoadableAsWaveTable:
					*error = ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE;
					return NULL;
				}

				// If this isn't actually a wavetable-specifying file or at least a wavetable-looking length, and the user isn't insisting, then opt not to do it.
				if (!((Sample*)foundAudioFile)->fileExplicitlySpecifiesSelfAsWaveTable) {
					if (((Sample*)foundAudioFile)->lengthInSamples & 2047) goto notLoadableAsWaveTable;
				}
			}

			void* waveTableMemory = generalMemoryAllocator.alloc(sizeof(WaveTable));
			if (!waveTableMemory) {
				*error = ERROR_INSUFFICIENT_RAM;
				return NULL;
			}

			WaveTable* newWaveTable = new (waveTableMemory) WaveTable;

			newWaveTable->addReason(); // So it's protected while setting up.
			foundAudioFile->addReason();

			*error = newWaveTable->setup((Sample*)foundAudioFile);
			if (*error) {
waveTableCloneError:
				newWaveTable->~WaveTable();
				generalMemoryAllocator.dealloc(waveTableMemory);
				return NULL;
			}

			*error = audioFiles.insertElement(newWaveTable);

			newWaveTable->removeReason("E397");
			foundAudioFile->removeReason("E398");

			if (*error) goto waveTableCloneError;

			return newWaveTable;
		}

		// Or if we want Sample but got Wavetable, can't convert, so we'll have to load from file after all. Reset filePath if needed (pretty unlikely scenario).
		else {
			if (!backedUpFilePath.isEmpty()) {
				filePath->set(&backedUpFilePath);
			}
		}
	}

	// Or, if that didn't find the audio file in memory...
	else {

		// If we're loading a preset (not a Song, and not just browsing audio files), we should search in memory for the alternate path
		if (alternateLoadDirStatus == ALTERNATE_LOAD_DIR_MIGHT_EXIST
		    || alternateLoadDirStatus == ALTERNATE_LOAD_DIR_DOES_EXIST) {
			if (thingTypeBeingLoaded != THING_TYPE_SONG) {
				String searchPath;
				searchPath.set(&alternateAudioFileLoadPath);
				*error = searchPath.concatenate("/");
				if (*error) goto tryLoadingFromCard;
				char const* fileName = getFileNameFromEndOfPath(filePath->get());
				*error = searchPath.concatenate(fileName);
				if (*error) goto tryLoadingFromCard;

				audioFileI = audioFiles.search(searchPath.get(), GREATER_OR_EQUAL, &foundExact);
				if (foundExact) {
					// Tiny bit cheeky, but we're going to update the file path actually stored in the AudioFile to reflect this alternate location, which will no longer be considered alternate.
					backedUpFilePath.set(filePath); // First we back up the original filePath.
					filePath->set(&searchPath);
					goto successfullyFoundInMemory;
				}
			}
		}
	}

tryLoadingFromCard:
	// Otherwise, try and load it in
	if (!mayReadCard) return NULL;

	if (cardDisabled) {
		*error = ERROR_SD_CARD;
		return NULL;
	}

	// Deactivated this because it stuff up the sampleI we already found
	/*
	if (!ensureEnoughMemoryForOneMoreSample()) {
		*error = ERROR_INSUFFICIENT_RAM;
		return NULL;
	}
	*/

	String usingAlternateLocation;

	FilePointer effectiveFilePointer;

	// If we got given a FilePointer, it's easy
	if (suppliedFilePointer) {
		effectiveFilePointer = *suppliedFilePointer;
	}

	// Otherwise go on the filePath
	else {

		bool alreadyTriedRegular = false;
		FRESULT result;

		// If we know the alternate load directory actually exists, then we should try that first, cos there's a high chance the file is in there
		if (alternateLoadDirStatus == ALTERNATE_LOAD_DIR_DOES_EXIST) {

tryAlternateDoesExist:
			String proposedFileName;
			char const* proposedFileNamePointer;
			bool alreadyTriedSecondAlternate;

			// We'll first try the long file name, which contains all the folder names from the original path. This is how collect-media saves look - for Songs.
			// But, if that original path didn't begin with "SAMPLES/", we can't do that.
			if (memcasecmp(filePath->get(), "SAMPLES/", 8)) goto tryNextAlternate;

			*error = setupAlternateAudioFilePath(&proposedFileName, 0, filePath);
			if (*error)
				return NULL; // This is to generate just the name of the file - not an entire path with folders - despite the function being called ...Path.

			alreadyTriedSecondAlternate = false;

tryAnAlternate:
			proposedFileNamePointer = proposedFileName.get();
			result = create_name(&alternateLoadDir, &proposedFileNamePointer);
			if (result != FR_OK) goto alternateFailed; // Can only fail if filename too weird.

			result = dir_find(&alternateLoadDir);

			if (result != FR_OK) {
alternateFailed:
				if (!alreadyTriedSecondAlternate) {
tryNextAlternate:
					// Next up we'll try looking for just the filename that the original file had, without any added folder names. This allows users to
					// copy files into folders for instruments more easily, and have them load.
					alreadyTriedSecondAlternate = true;
					char const* fileName = getFileNameFromEndOfPath(filePath->get());
					*error = proposedFileName.set(fileName);
					if (*error) return NULL;
					goto tryAnAlternate;
				}
				if (alreadyTriedRegular) goto notFound;
				else goto tryRegular;
			}

			// Ok, found file - in the alternate location.
			effectiveFilePointer.sclust = ld_clust(&fileSystemStuff.fileSystem, alternateLoadDir.dir);
			effectiveFilePointer.objsize = ld_dword(alternateLoadDir.dir + DIR_FileSize);

			usingAlternateLocation.set(&alternateAudioFileLoadPath);
			*error = usingAlternateLocation.concatenate("/");
			if (*error) return NULL;
			*error = usingAlternateLocation.concatenate(&proposedFileName);
			if (*error) return NULL;

			if (thingTypeBeingLoaded == THING_TYPE_SYNTH || thingTypeBeingLoaded == THING_TYPE_KIT) {
				// Special rule for loading presets with files in their dedicated "alternate" folder: must update the AudioFile's filePath to point to that alternate location - and then treat them as normal (not alternate).
				filePath->set(&usingAlternateLocation);
				usingAlternateLocation.clear();
			}
		}

		// Otherwise, try the regular file path
		else {
tryRegular:
			result = f_open(&fileSystemStuff.currentFile, filePath->get(), FA_READ);

			// If that didn't work, try the alternate load directory, if we didn't already and it potentially exists
			if (result != FR_OK) {

				if (alternateLoadDirStatus == ALTERNATE_LOAD_DIR_MIGHT_EXIST) {

					result = f_opendir(&alternateLoadDir, alternateAudioFileLoadPath.get());
					if (result != FR_OK) {
						alternateLoadDirStatus = ALTERNATE_LOAD_DIR_NOT_FOUND;
						goto notFound;
					}

					alternateLoadDirStatus = ALTERNATE_LOAD_DIR_DOES_EXIST;

					alreadyTriedRegular = true;
					goto tryAlternateDoesExist;
				}

notFound:
				*error = ERROR_FILE_UNREADABLE;
				return NULL;
			}

			// Ok, found file.
			effectiveFilePointer.sclust = fileSystemStuff.currentFile.obj.sclust;
			effectiveFilePointer.objsize = fileSystemStuff.currentFile.obj.objsize;
		}
	}

	// 0-byte files not allowed.
	if (!effectiveFilePointer.objsize) {
		*error = ERROR_FILE_CORRUPTED;
cantLoadFile:
		//if (!suppliedFilePointer) f_close(&fileSystemStuff.currentFile);
		return NULL;
	}

	// Files bigger than 1GB not allowed
	else if (effectiveFilePointer.objsize > MAX_FILE_SIZE) {
		*error = ERROR_FILE_TOO_BIG;
		goto cantLoadFile;
	}

	uint32_t numClusters = ((effectiveFilePointer.objsize - 1) >> clusterSizeMagnitude) + 1;

	int memorySizeNeeded = (type == AUDIO_FILE_TYPE_SAMPLE) ? sizeof(Sample) : sizeof(WaveTable);

	void* audioFileMemory = generalMemoryAllocator.alloc(memorySizeNeeded, NULL, false, true, true); // Stealable!
	if (!audioFileMemory) {
ramError:
		*error = ERROR_INSUFFICIENT_RAM;
		goto cantLoadFile;
	}

	char readerMemory[sizeof(SampleReader)];
	AudioFileReader* reader;

	AudioFile* audioFile;
	if (type == AUDIO_FILE_TYPE_SAMPLE) {
		audioFile = new (audioFileMemory) Sample;
		audioFile->addReason(); // So it's protected while setting up. Must do this before calling initialize().
		*error = ((Sample*)audioFile)->initialize(numClusters);
		if (*error) { // Very rare, only if not enough RAM
			audioFile->~AudioFile();
			generalMemoryAllocator.dealloc(audioFileMemory);
			goto cantLoadFile;
		}

		reader = new (readerMemory) SampleReader;
	}
	else {
		audioFile = new (audioFileMemory) WaveTable;
		audioFile->addReason(); // So it's protected while setting up.
		reader = new (readerMemory) WaveTableReader;
	}

	audioFile->filePath.set(filePath);
	audioFile->loadedFromAlternatePath.set(&usingAlternateLocation);

	reader->currentClusterIndex = -1;
	reader->audioFile = audioFile;
	reader->fileSize = effectiveFilePointer.objsize;
	reader->byteIndexWithinCluster = clusterSize;

	// If Sample, we go directly to god-mode and get the cluster addresses.
	if (type == AUDIO_FILE_TYPE_SAMPLE) {

		// Store the address of each of the file's clusters.
		uint32_t currentClusterIndex = 0;
		uint32_t currentSDCluster =
		    effectiveFilePointer.sclust; // Start with first cluster, whose address we already got.

		while (true) {

			((Sample*)audioFile)->clusters.getElement(currentClusterIndex)->sdAddress =
			    clst2sect(&fileSystemStuff.fileSystem, currentSDCluster);

			currentClusterIndex++;
			if (currentClusterIndex >= numClusters) break;

			currentSDCluster = get_fat_from_fs(&fileSystemStuff.fileSystem, currentSDCluster);

			if (currentSDCluster == 0xFFFFFFFF || currentSDCluster < 2) break;
		}

		//if (!suppliedFilePointer) f_close(&fileSystemStuff.currentFile);

		((SampleReader*)reader)->currentCluster = NULL;
	}

	// Or if WaveTable, we're going to read the file more normally through FatFS, so we want to "open" it.
	else {
		storageManager.openFilePointer(&effectiveFilePointer); // It never returns fail.
	}

	// Read top-level RIFF headers
	uint32_t topHeader[3];
	*error = reader->readBytes((char*)topHeader, 3 * 4);
	if (*error) goto ensureSafeThenCheckError;

	if (topHeader[0] == 0x46464952       // "RIFF"
	    && topHeader[2] == 0x45564157) { // "WAVE"
		*error = audioFile->loadFile(reader, false, makeWaveTableWorkAtAllCosts);
	}
	else if (topHeader[0] == 0x4D524F46       // "FORM"
	         && topHeader[2] == 0x46464941) { // "AIFF"
		*error = audioFile->loadFile(reader, true, makeWaveTableWorkAtAllCosts);
	}
	else {
		*error = ERROR_FILE_UNSUPPORTED;
	}

ensureSafeThenCheckError:
	if (type == AUDIO_FILE_TYPE_SAMPLE) {
		if (((SampleReader*)reader)->currentCluster)
			removeReasonFromCluster(((SampleReader*)reader)->currentCluster, "E030");
	}
	else {
		//f_close(&fileSystemStuff.currentFile);
	}

	if (*error) {
audioFileError:
		audioFile
		    ->~AudioFile(); // Have to call this! This removes the pointers back to the Sample / SampleClusters from any Clusters.
		generalMemoryAllocator.dealloc(audioFileMemory);
		return NULL;
	}

	*error = audioFiles.insertElement(audioFile);
	if (*error) goto audioFileError;

	audioFile->finalizeAfterLoad(effectiveFilePointer.objsize);

	audioFile->removeReason("E399");

	return audioFile;
}

void AudioFileManager::testQueue() {

	/*
	Cluster* loadedSampleChunk = queues[LOADED_SAMPLE_CHUNK_ALLOCATION_QUEUE_NORMAL].first;
	while (loadedSampleChunk != &queues[LOADED_SAMPLE_CHUNK_ALLOCATION_QUEUE_NORMAL].endNode) {


		if (loadedSampleChunk->nextAvailableLoadedSampleChunk == &queues[LOADED_SAMPLE_CHUNK_ALLOCATION_QUEUE_NORMAL].endNode
				&& queues[LOADED_SAMPLE_CHUNK_ALLOCATION_QUEUE_NORMAL].endNode.prevAvailableLoadedSampleChunkPointer != &loadedSampleChunk->nextAvailableLoadedSampleChunk) {
			Uart::println("error ---------------------------------");
			Uart::print(loadedSampleChunk->sample->fileName);
			Uart::print(", part ");
			Uart::println(loadedSampleChunk->chunkIndex);
			return;
		}

		if (loadedSampleChunk->nextAvailableLoadedSampleChunk->prevAvailableLoadedSampleChunkPointer != &loadedSampleChunk->nextAvailableLoadedSampleChunk) {
			Uart::println("abc ---------------------------------");
			Uart::print(loadedSampleChunk->sample->fileName);
			Uart::print(", part ");
			Uart::println(loadedSampleChunk->chunkIndex);
			return;
		}

		loadedSampleChunk = loadedSampleChunk->nextAvailableLoadedSampleChunk;
	}

	Uart::println("queue ok -----------------------");
	*/
}

// Caller must initialize() the Cluster after getting it from this function
Cluster* AudioFileManager::allocateCluster(int type, bool shouldAddReasons, void* dontStealFromThing) {

	void* clusterMemory = generalMemoryAllocator.alloc(clusterObjectSize, NULL, false, false, true, dontStealFromThing);
	if (!clusterMemory) return NULL;

	Cluster* cluster = new (clusterMemory) Cluster();

	cluster->type = type;

	if (shouldAddReasons) {
		addReasonToCluster(cluster);
	}

	return cluster;
}

void AudioFileManager::deallocateCluster(Cluster* cluster) {
	cluster->~Cluster(); // Removes reasons, and / or from stealable list
	generalMemoryAllocator.dealloc(cluster);
}

#define REPORT_LOAD_TIME 0

bool AudioFileManager::loadCluster(Cluster* cluster, int minNumReasonsAfter) {

	if (currentlyAccessingCard)
		return false; // Could happen if we're trying to render a waveform but we're actually already inside the SD routine

	// I don't think these should happen...
	if (clusterBeingLoaded) return false;
	if (AudioEngine::audioRoutineLocked) return false;

	clusterBeingLoaded = cluster;
	minNumReasonsForClusterBeingLoaded = minNumReasonsAfter + 1;

	Sample* sample = cluster->sample;

	if (cluster->type != CLUSTER_SAMPLE)
		numericDriver.freezeWithError("E205"); // Chris F got this, so gonna leave checking in release build

#if ALPHA_OR_BETA_VERSION
	if (cluster->numReasonsToBeLoaded <= 0)
		numericDriver.freezeWithError(
		    "E204"); // Ok, I think we know there's at least 1 reason at the point this function's called, because
		             // it'd only be in the loading queue if it had a "reason".
	if (!sample) numericDriver.freezeWithError("E206");
#endif

	addReasonToCluster(
	    cluster); // So that it can't accidentally hit 0 reasons while we're loading it, cos then it might get deallocated.

	if (false) {
getOutEarly:
		clusterBeingLoaded = NULL;
		removeReasonFromCluster(cluster, "E033");
		return false;
	}

	int clusterIndex = cluster->clusterIndex;

	int numSectors = clusterSize >> 9;

	// If this is the last Cluster, and we do know what the audio data length is...
	if (sample->audioDataLengthBytes && sample->audioDataLengthBytes != 0x8FFFFFFFFFFFFFFF) {
		uint32_t audioDataEndPosBytes = sample->audioDataLengthBytes + sample->audioDataStartPosBytes;
		uint32_t startByteThisCluster = clusterIndex << clusterSizeMagnitude;
		int32_t bytesToRead = audioDataEndPosBytes - startByteThisCluster;
		if (bytesToRead <= 0) {
			Uart::println("fail thing"); // Shouldn't really still happen
			goto getOutEarly;
		}
		if (bytesToRead < clusterSize) {
			numSectors = ((bytesToRead - 1) >> 9) + 1;
		}
		// Otherwise, just leave it at the normal number of sectors
	}

#if ALPHA_OR_BETA_VERSION
	if ((uint32_t)cluster->data & 0b11) {
		Uart::print("SD read address misaligned by ");
		Uart::println((int32_t)((uint32_t)cluster->data & 0b11));
	}
#endif

	AudioEngine::logAction("loadCluster");

#if REPORT_LOAD_TIME
	uint16_t startTime = MTU2.TCNT_0;
#endif

#if ALPHA_OR_BETA_VERSION
	if (cluster->type != CLUSTER_SAMPLE)
		numericDriver.freezeWithError("i023"); // Happened to me while thrash testing with reduced RAM

	if (cluster->numReasonsToBeLoaded < minNumReasonsAfter + 1)
		numericDriver.freezeWithError("i039"); // It's +1 because we haven't removed this function's "reason" yet.
#endif

	DRESULT result = disk_read_without_streaming_first(
	    SD_PORT, (BYTE*)cluster->data, sample->clusters.getElement(cluster->clusterIndex)->sdAddress, numSectors);

#if REPORT_LOAD_TIME
	uint16_t endTime = MTU2.TCNT_0;
	uint16_t duration = endTime - startTime;
	int uSec = timerCountToUS(duration);
	if (uSec > 7000) {
		Uart::println(uSec);
	}
#endif

#if ALPHA_OR_BETA_VERSION
	if (cluster->type != CLUSTER_SAMPLE) numericDriver.freezeWithError("E207");
	if (!cluster->sample) numericDriver.freezeWithError("E208");

	if (cluster->numReasonsToBeLoaded < minNumReasonsAfter + 1)
		numericDriver.freezeWithError("i038"); // It's +1 because we haven't removed this function's "reason" yet.
#endif

	// If that failed, get out
	if (result) goto getOutEarly;

	cluster->convertDataIfNecessary();

#if ALPHA_OR_BETA_VERSION
	if (cluster->numReasonsToBeLoaded < minNumReasonsAfter + 1)
		numericDriver.freezeWithError("i040"); // It's +1 because we haven't removed this function's "reason" yet.
#endif

	int misalignment = sample->audioDataStartPosBytes & 0b11;

	// Give extra bytes to previous Cluster
	if (clusterIndex > 0) {
		Cluster* prevCluster = sample->clusters.getElement(cluster->clusterIndex - 1)->cluster;

		if (prevCluster && prevCluster->loaded) {

			// We first copy our first 7 bytes from here to the end of the prev Cluster...
			memcpy(&prevCluster->data[clusterSize], cluster->data, 7);

			// If 24-bit wrong-endian data...
			if (sample->rawDataFormat == RAW_DATA_ENDIANNESS_WRONG_24) {

				// If we hadn't previously written the "extra" bytes to the end of the prev Cluster and converted them, do so now...
				if (!prevCluster->extraBytesAtEndConverted) {

					uint32_t bytesBeforeStartOfCluster = clusterIndex * clusterSize - sample->audioDataStartPosBytes;
					int bytesUnconvertedBeforeCluster = bytesBeforeStartOfCluster % 3;
					if (bytesUnconvertedBeforeCluster) {

						// There'll be one word in there which hasn't yet been converted. Do it now. (We've probably just copied over the next one and a bit, which already was converted)
						int startPos = clusterSize - bytesUnconvertedBeforeCluster;
						uint8_t* thisNumber = (uint8_t*)&prevCluster->data[startPos];

						uint8_t temp = thisNumber[0];
						thisNumber[0] = thisNumber[2];
						thisNumber[2] = temp;

						// And now, copy 2 bytes back to this Cluster (that's the maximum that the float could have been overhanging the boundary)
						memcpy(cluster->data, &prevCluster->data[clusterSize], 2);
					}

					prevCluster->extraBytesAtEndConverted = true;
				}
			}

			// Or, all other types of raw data conversion
			else if (sample->rawDataFormat) {

				// If we haven't previously written the "extra" bytes to the end of the prev Cluster and converted them, do so now...
				if (!prevCluster->extraBytesAtEndConverted) {

					// If misaligned from the 4-byte boundary
					if (misalignment) {

						// There'll be one word in there which hasn't yet been converted. Do it now. (We've probably also just moved over the next one too, which already was converted)
						int startPos = clusterSize - 4 + misalignment;
						int32_t* thisNumber = (int32_t*)&prevCluster->data[startPos];
						sample->convertOneData(thisNumber);

						// And now, copy 3 bytes back to this Cluster (that's the maximum that the float could have been overhanging the boundary)
						memcpy(cluster->data, &prevCluster->data[clusterSize], 3);
					}

					prevCluster->extraBytesAtEndConverted = true;
				}
			}

			cluster->extraBytesAtStartConverted = true;
		}
	}

	// Grab extra bytes from next Cluster
	if (clusterIndex < sample->clusters.getNumElements() - 1) {
		Cluster* nextCluster = sample->clusters.getElement(cluster->clusterIndex + 1)->cluster;

		if (nextCluster && nextCluster->loaded) {

			// If 24-bit wrong-endian data...
			if (sample->rawDataFormat == RAW_DATA_ENDIANNESS_WRONG_24) {

				uint32_t bytesBeforeStartOfNextCluster =
				    (clusterIndex + 1) * clusterSize - sample->audioDataStartPosBytes;
				int bytesUnconvertedBeforeNextCluster = bytesBeforeStartOfNextCluster % 3;

				// If one word missed conversion...
				if (bytesUnconvertedBeforeNextCluster) {

					// If we had't previously converted the first couple of bytes of the next Cluster...
					if (!nextCluster->extraBytesAtStartConverted) {

						// We first copy the next Cluster first 7 bytes to the end of this Cluster
						memcpy(&cluster->data[clusterSize], nextCluster->data, 7);
					}

					// Or, if we *had* previously converted the first bytes of the next Cluster...
					else {

						// Grab the unconverted bytes back from where we backed them up to
						memcpy(&cluster->data[clusterSize], nextCluster->firstThreeBytesPreDataConversion, 2);
					}

					// There'll be one word in there which hasn't yet been converted. Do it now. (We've probably just copied over the next one and a bit, which already was converted)
					uint8_t* thisNumber = (uint8_t*)&cluster->data[clusterSize - bytesUnconvertedBeforeNextCluster];

					uint8_t temp = thisNumber[0];
					thisNumber[0] = thisNumber[2];
					thisNumber[2] = temp;

					// If we had't previously converted the first couple of bytes of the next Cluster, do so now...
					if (!nextCluster->extraBytesAtStartConverted) {
						nextCluster->extraBytesAtStartConverted = true;

						// And now, copy 2 bytes back to the next Cluster (that's the maximum that the 24-bit int could have been overhanging the boundary)
						memcpy(nextCluster->data, &cluster->data[clusterSize], 2);
					}

					// Or, if we *had* previously converted the first bytes of the next Cluster...
					else {
						goto copy7ToMe;
					}
				}

				// Or if no words missed conversion
				else {
					goto copy7ToMe;
				}
			}

			// Or, all other types of raw data conversion
			else if (sample->rawDataFormat) {

				// If one word missed conversion...
				if (misalignment) {
					int startPos = clusterSize - 4 + misalignment;
					int32_t* thisNumber = (int32_t*)&cluster->data[startPos];

					// If we had't previously converted the first couple of bytes of the next Cluster, do so now...
					if (!nextCluster->extraBytesAtStartConverted) {

						// We first copy the next Cluster first 7 bytes to the end of this Cluster
						memcpy(&cluster->data[clusterSize], nextCluster->data, 7);

						// There'll be one word in there which hasn't yet been converted from float. Do it now
						sample->convertOneData(thisNumber);

						// And now, copy 3 bytes back to the next Cluster (that's the maximum that the float could have been overhanging the boundary)
						memcpy(nextCluster->data, &cluster->data[clusterSize], 3);

						nextCluster->extraBytesAtStartConverted = true;
					}

					// Or, if we *had* previously converted the first bytes of the next Cluster...
					else {

						// Grab the unconverted bytes back from where we backed them up to
						memcpy(&cluster->data[clusterSize], nextCluster->firstThreeBytesPreDataConversion, 3);

						// There'll be one word in there which hasn't yet been converted from float. Do it now
						sample->convertOneData(thisNumber);

						// And now just copy the converted-from-float first bytes from the next Cluster to the end of this one
						goto copy7ToMe;
					}
				}
				else {
					goto copy7ToMe;
				}
			}

			else {
copy7ToMe:
				// We copy the next Cluster's first 7 bytes to the end of this Cluster
				memcpy(&cluster->data[clusterSize], nextCluster->data, 7);
			}

			cluster->extraBytesAtEndConverted = true;
		}
	}

	cluster->loaded = true;

	clusterBeingLoaded = NULL;
	removeReasonFromCluster(cluster, "E034");

#if ALPHA_OR_BETA_VERSION
	if (cluster->numReasonsToBeLoaded < minNumReasonsAfter) numericDriver.freezeWithError("i037");
	if (cluster->sample->clusters.getElement(cluster->clusterIndex)->cluster != cluster)
		numericDriver.freezeWithError("E438");
#endif

	return true;
}

// Only needs calling a couple times per second. Must be called outside of the audio / SD-reading routine
// Call this repeatedly so SD card is re-initialized on re-insert before we actually urgently need audio from it
void AudioFileManager::slowRoutine() {

	// If we know the card's been ejected...
	if (cardEjected) {
		// If it's still ejected, get out
		if (!storageManager.checkSDPresent()) return;

		// Otherwise, see if we can get it
		else {
			int error = storageManager.initSD();
			if (!error) {
				cardEjected = false;
				cardReinserted();
			}
		}
	}

#if ALPHA_OR_BETA_VERSION >= 2
	// Randomly steal a Cluster for fun. Ok sorry, this code is out of date.
	if (((uint32_t)getNoise() >> 18) < 1) { // 16

		int q = getRandom255() % (NUM_LOADED_SAMPLE_CHUNK_ALLOCATION_QUEUES - 1);
		q++;

		int startQ = q;

		int numToSkip = (getRandom255() >> 4) + 1;

		BidirectionalLinkedListNode* node = NULL;
		while (numToSkip) {

			do {
				if (!node) {
					q++;
					if (q == NUM_LOADED_SAMPLE_CHUNK_ALLOCATION_QUEUES) q = 1;
					if (q == startQ) break;

					node = availableClusterQueues[q].getFirst();
				}

				else node = availableClusterQueues[q].getNext(node);
			} while (!node);

			numToSkip--;
		}

		if (node) {
			Uart::print("stealing cluster for fun from queue: ");
			Uart::println(q);
			Cluster* cluster = (Cluster*)node;
			cluster->steal();
			deallocateCluster(cluster);
		}
	}
#endif
}

#define REPORT_AWAY_TIME 0

#if REPORT_AWAY_TIME
uint16_t timeLastFinish;
#endif

void AudioFileManager::loadAnyEnqueuedClusters(int maxNum, bool mayProcessUserActionsBetween) {

	if (currentlyAccessingCard) return;
	if (clusterBeingLoaded)
		return; // One might be having stuff done to it, like having its data converted, but not actually reading the card right now
	if (AudioEngine::audioRoutineLocked) return; // Not sure if this should be neccesary?

	// Cannot call any functions in here which will read the SD card, other than loadCluster(), otherwise that'll re-call this function!

	if (cardEjected || cardDisabled) {

performActionsAndGetOut:
		if (mayProcessUserActionsBetween) {
			playbackHandler.slowRoutine();
		}
		return;
	}

	if (!storageManager.checkSDInitialized()) goto performActionsAndGetOut; // In case the card somehow died

	int count = 0;

#if REPORT_AWAY_TIME
	uint16_t startTime = MTU2.TCNT_0;
	uint16_t awayTime = startTime - timeLastFinish;
	int uSecAway = timerCountToUS(awayTime);
	if (uSecAway > 1000) {
		Uart::print("away ");
		Uart::println(uSecAway);
	}
#endif

	while (true) {

		// We now have an opportunity, since we're not reading the card, to process any pending user actions like undo / redo.
		if (mayProcessUserActionsBetween) {
			playbackHandler.slowRoutine();
		}

		Cluster* cluster = loadingQueue.grabHead();
		if (!cluster) break;

		// cluster has at least 1 "reason". If it didn't, it would have been removed from the load-queue

		// Do the actual loading
		if (cluster->type != CLUSTER_SAMPLE) numericDriver.freezeWithError("E235"); // Cos Chris F got an E205

		allowSomeUserActionsEvenWhenInCardRoutine = true; // Sorry!!
		bool success = loadCluster(cluster);
		allowSomeUserActionsEvenWhenInCardRoutine = false;

		// If that didn't work, presumably because the SD card got ejected...
		if (!success) {
			Uart::println("load Cluster fail");

			// If the Cluster is now down to 0 reasons (i.e. it lost a reason while being loaded), then it's already been made "available" and we don't have a problem
			if (!cluster->numReasonsToBeLoaded) {}

			// Otherwise, there are still "reasons" waiting for this Cluster to become loaded, so we need to put it back in the loading queue.
			// Presumably it won't actually get loaded for a while - only when the user re-inserts the card
			else {

				if (cluster->type != CLUSTER_SAMPLE) numericDriver.freezeWithError("E237"); // Cos Chris F got an E205

				enqueueCluster(cluster); // TODO: If that fails, it'll just get awkwardly forgotten about

				// Also, return now. Normally we stay here til there's nothing left in the load-queue, but now that would leave us in an infinite loop!
				break;
			}
		}

		count++;
		if (count >= maxNum) break; // Keep things sane?
	}

#if REPORT_AWAY_TIME
	timeLastFinish = MTU2.TCNT_0;
#endif
}

// Currently there's no risk of trying to enqueue a cluster multiple times, because this function only gets called after it's freshly allocated
int AudioFileManager::enqueueCluster(Cluster* cluster, uint32_t priorityRating) {
	return loadingQueue.add(cluster, priorityRating);
}

void AudioFileManager::addReasonToCluster(Cluster* cluster) {
	// If it's going to cease to be zero, it's become unavailable
	if (cluster->numReasonsToBeLoaded == 0) {
		cluster->remove();
		//*cluster->getAnyReasonsPointer() = reasonType;
	}

	cluster->numReasonsToBeLoaded++;
}

void AudioFileManager::removeReasonFromCluster(Cluster* cluster, char const* errorCode) {
	cluster->numReasonsToBeLoaded--;

	if (cluster == clusterBeingLoaded && cluster->numReasonsToBeLoaded < minNumReasonsForClusterBeingLoaded) {
		numericDriver.freezeWithError("E041"); // Sven got this!
	}

	// If it's now zero, it's become available
	if (cluster->numReasonsToBeLoaded == 0) {

		// Bug hunting
		if (ALPHA_OR_BETA_VERSION && cluster->numReasonsHeldBySampleRecorder) numericDriver.freezeWithError("E364");

		// If it's still in the load queue, remove it from there. (We know that it isn't in the process of being loaded right now
		// because that would have added a "reason", so we wouldn't be here.)
		if (loadingQueue.removeIfPresent(cluster)) {

			// Tell its Cluster to forget it exists
			cluster->sample->clusters.getElement(cluster->clusterIndex)->cluster = NULL;

			deallocateCluster(cluster); // It contains nothing, so completely recycle it
		}

		else {
			generalMemoryAllocator.putStealableInAppropriateQueue(
			    cluster); // It contains data we may want at some future point, so file it away
		}

		//*cluster->getAnyReasonsPointer() = ANY_REASONS_NO;
	}

	else if (cluster->numReasonsToBeLoaded < 0) {
#if ALPHA_OR_BETA_VERSION
		if (cluster->sample) { // "Should" always be true...
			Uart::print("reason remains on cluster of sample: ");
			Uart::println(cluster->sample->filePath.get());
		}
		numericDriver.freezeWithError(errorCode);
#else
		numericDriver.displayPopup(errorCode); // For non testers, just display the error code without freezing
		cluster->numReasonsToBeLoaded = 0;     // Save it from crashing or anything
#endif
	}
}

bool AudioFileManager::loadingQueueHasAnyLowestPriorityElements() {
	int numElements = loadingQueue.getNumElements();
	return (numElements
	        && ((PriorityQueueElement*)audioFileManager.loadingQueue.getElementAddress(numElements - 1))->priorityRating
	               == 0xFFFFFFFF);
}

// Caller must also set alternateAudioFileLoadPath.
void AudioFileManager::thingBeginningLoading(int newThingType) {
	alternateLoadDirStatus = ALTERNATE_LOAD_DIR_MIGHT_EXIST;
	thingTypeBeingLoaded = newThingType;
}

void AudioFileManager::thingFinishedLoading() {
	alternateAudioFileLoadPath.clear();
	alternateLoadDirStatus = ALTERNATE_LOAD_DIR_NONE_SET;
	thingTypeBeingLoaded = THING_TYPE_NONE;
}
