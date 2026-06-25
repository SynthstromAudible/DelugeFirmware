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

#include "storage/audio/audio_file_manager.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "io/midi/midi_device_manager.h"
#include "libdeluge/block_device.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"

#include "deluge_resource.h" // resource manager: unlease manager-owned SAMPLE clusters
#include "model/sample/sample_cache.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/cluster_byte_source.h"
#include "storage/audio/deserializer_byte_source.h"
#include "storage/cluster/cluster.h"
#include "storage/storage_manager.h"
#include "storage/wave_table/wave_table.h"
#include "util/string.h"
#include "util/try.h"
#include <cstddef>

#include <new>
#include <string.h>

extern "C" {
#include "fatfs/diskio.h"
#include "fatfs/ff.h"

DWORD get_fat_from_fs(                      /* 0xFFFFFFFF:Disk error, 1:Internal error, 2..0x7FFFFFFF:Cluster status */
                      FATFS* fs, DWORD clst /* Cluster number to get the value */
);

LBA_t clst2sect(           /* !=0:Sector number, 0:Failed (invalid cluster#) */
                FATFS* fs, /* Filesystem object */
                DWORD clst /* Cluster# to be converted */
);

DRESULT disk_read_without_streaming_first(BYTE pdrv, BYTE* buff, DWORD sector, UINT count);
DRESULT disk_write_without_streaming_first(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count);

extern uint8_t currentlyAccessingCard;
extern int32_t pendingGlobalMIDICommandNumClustersWritten;
extern int currentlySearchingForCluster;

// FatFs porting symbols. Service the audio cluster-streaming queue before every FatFs
// sector access (an app priority concern), then do the plain sector I/O. Inverts what
// used to be a HAL->app upcall (diskio.c calling loadAnyEnqueuedClustersRoutine): the
// streaming policy now lives in the app and calls *down* into the block device.
DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
	audioFileManager.loadAnyEnqueuedClusters(); // always ensure SD streaming is fulfilled first

	DRESULT result = disk_read_without_streaming_first(pdrv, buff, sector, count);

	if (currentlySearchingForCluster) {
		pendingGlobalMIDICommandNumClustersWritten++;
	}

	return result;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
	audioFileManager.loadAnyEnqueuedClusters(); // always ensure SD streaming is fulfilled first
	return disk_write_without_streaming_first(pdrv, buff, sector, count);
}
}

AudioFileManager audioFileManager{};

// === Resource-manager adopt for AudioFile objects (the unified SDRAM reclaim coordinator) =====
// An AudioFile (Sample/WaveTable) object is adopted as an evictable block: its reasons are manager
// leases (AudioFile::add/removeReason), and when unleased the manager may reclaim it under memory
// pressure via audioFileEvict — erasing it from `audioFiles` and destructing it, which frees its
// clusters (Sample → release_asset) and its wavetable bands (~WaveTable). Replaces the CacheManager
// NO_SONG_AUDIO_FILE_OBJECTS queue. (Note: more correct than the legacy steal path, which freed the
// object's raw memory without ~AudioFile — leaking clusters/bands.)
namespace {
void audioFileEvict(void* /*ctx*/, void* ptr) {
	auto* audioFile = static_cast<AudioFile*>(ptr);
	int32_t i = audioFileManager.audioFiles.searchForExactObject(audioFile);
	if (i >= 0) {
		audioFileManager.audioFiles.erase(audioFileManager.audioFiles.begin() + i);
	}
	audioFile->~AudioFile(); // the manager frees the object's backing right after this
}
} // namespace

// Adopt a freshly placement-new'd AudioFile (call before its first addReason). The manager is the
// sole SDRAM evictor, so adoption must succeed — a missing manager / exhausted chunk table is fatal
// (no legacy fallback). On eviction the manager runs `audioFileEvict` + frees the backing.
void AudioFileManager::adoptAudioFileObject(AudioFile* audioFile) {
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	// Cost OBJECT is modest (a re-scan), but the object is tiny — so the manager's cost-per-byte
	// eviction keeps the descriptor resident over the fat clusters it owns. Pass its allocated size.
	void* p = (mgr != nullptr) ? deluge_resource_adopt(mgr, audioFile, audioFile->allocatedSize(),
	                                                   DELUGE_RESOURCE_COST_OBJECT, nullptr, audioFileEvict)
	                           : nullptr;
	if (p == nullptr) {
		FREEZE_WITH_ERROR("RAF1"); // resource chunk table exhausted adopting an AudioFile object
	}
	// Record the slot handle so the object's reason count (leaseCount / isProjectReferenced) is O(1).
	audioFile->resourceSlot = deluge_resource_slot_of(mgr, p);
}

// Destruct + free an AudioFile object (caller has already removed it from `audioFiles` if present).
// Every AudioFile is manager-adopted, so the free routes through the manager (un-adopt).
void AudioFileManager::destroyAudioFileObject(AudioFile& audioFile) {
	audioFile.~AudioFile();
	deluge_resource_evict_chunk(GeneralMemoryAllocator::get().resourceManager(), &audioFile);
}

AudioFileManager::AudioFileManager() {
	highestUsedAudioRecordingNumber.fill(-1);
	highestUsedAudioRecordingNumberNeedsReChecking.set();
}

void AudioFileManager::firstCardRead() {
	if (cardReadOnce) {
		cardReinserted();
	}
	else {
		init();
	}
}

void AudioFileManager::init() {

	clusterBeingLoaded = nullptr;

	Error error = StorageManager::initSD();
	if (error == Error::NONE) {
		Cluster::setSize(fileSystem.csize * 512);

		D_PRINTLN("Cluster::size  %d clusterSizeMagnitude  %d", Cluster::size, Cluster::size_magnitude);
		cardEjected = false;
		cardReadOnce = true;
	}

	else {
		Cluster::setSize(Cluster::kSizeFAT16Max);
		cardEjected = true;
	}

	clusterSizeAtBoot = Cluster::size;
}

void AudioFileManager::cardReinserted() {

	cardDisabled = false;
	for (int32_t i = 0; i < kNumAudioRecordingFolders; i++) {
		highestUsedAudioRecordingNumberNeedsReChecking[i] = true;
	}

	// If cluster size has increased, we're in trouble
	if (fileSystem.csize * 512 > Cluster::size) {

		// But, if it's still not as big as it was when we booted up, that's still manageable
		if (fileSystem.csize * 512 <= clusterSizeAtBoot) {
			goto clusterSizeChangedButItsOk;
		}

		D_PRINTLN("cluster size increased and we're in trouble");
		cardDisabled = true;
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_REBOOT_TO_USE_THIS_SD_CARD));
	}

	// If cluster size decreased, we have to stop all current samples from ever sounding again. Pretty big trouble
	// really...
	else if (fileSystem.csize * 512 < Cluster::size) {

clusterSizeChangedButItsOk:
		D_PRINTLN("cluster size changed, and smaller than original so it's ok");
		AudioEngine::killAllVoices(); // Will also stop synth voices - too bad.

		for (int32_t e = 0; e < static_cast<int32_t>(audioFiles.size()); e++) {
			AudioFile* thisAudioFile = audioFiles[e];

			// If AudioFile isn't used currently, take this opportunity to remove it from memory
			if (!thisAudioFile->isProjectReferenced()) {
				deleteUnusedAudioFileFromMemory(*thisAudioFile, e);
				e--;
			}

			// Otherwise, mark the sample as unplayable
			else {
				if (thisAudioFile->type == AudioFileType::SAMPLE) {
					((Sample*)thisAudioFile)->unplayable = true;
				}
			}
		}

		// That was all a pain, but now we can update the cluster size
		Cluster::setSize(fileSystem.csize * 512);
	}

	// Or if cluster size stayed the same...
	else {
		// Go through every Sample in memory
		for (int32_t e = 0; e < static_cast<int32_t>(audioFiles.size()); e++) {

			AudioFile* thisAudioFile = audioFiles[e];

			// If Sample isn't used currently, take this opportunity to remove it from memory
			if (!thisAudioFile->isProjectReferenced()) {
				deleteUnusedAudioFileFromMemory(*thisAudioFile, e);
				e--;
			}

			// Or if it is still used by someone...
			else {
				if (thisAudioFile->type == AudioFileType::SAMPLE) {
					// Check the Sample's file still exists
					FIL sampleFile;
					char const* filePath = ((Sample*)thisAudioFile)->tempFilePathForRecording.c_str();
					if (!*filePath) {
						filePath = thisAudioFile->filePath.c_str();
					}

					FRESULT result = f_open(&sampleFile, filePath, FA_READ);
					if (result != FR_OK) {
						D_PRINTLN("couldn't open file");
						((Sample*)thisAudioFile)->markAsUnloadable();
						continue;
					}

					uint32_t firstSector = clst2sect(&fileSystem, sampleFile.obj.sclust);

					f_close(&sampleFile);

					// If address of first sector remained unchanged, we can be sure enough that the file hasn't been
					// changed
					if (firstSector == ((Sample*)thisAudioFile)->clusters[0].sdAddress) {}

					// Otherwise
					else {
						((Sample*)thisAudioFile)->markAsUnloadable();
						continue;
					}

					// Or if we're still here, the file's fine - who knows, maybe it's even fine again after it wasn't
					// for a while (e.g. if the user temporarily had a different card inserted)
					((Sample*)thisAudioFile)->unloadable = false;
				}
			}
		}
	}

	MIDIDeviceManager::readDevicesFromFile(); // Hopefully we can do this now. It'll only happen if it
	                                          // wasn't able to do it before.
}

// Call this after deleting the current (or in other words previous) Song from memory - meaning there won't be any
// further reason we'd ever move these temp samples into the permanent sample folder, meaning we don't want them in
// memory listed with their would-be real permanent filenames. Also, we won't be needing to play them back again. You
// must not call this during the card or audio routines.
void AudioFileManager::deleteAnyTempRecordedSamplesFromMemory() {

	// Also though, in case any of these Samples were still being recorded before the Song-delete, we need to make sure
	// SampleRecorder::cardRoutine() gets called first to "detach" the Sample from the recorder. So, do this:
	AudioEngine::doRecorderCardRoutines();

	for (int32_t e = 0; e < static_cast<int32_t>(audioFiles.size()); e++) {
		AudioFile* audioFile = audioFiles[e];

		if (audioFile->type == AudioFileType::SAMPLE) {
			// If it's a temp-recorded one
			if (!((Sample*)audioFile)->tempFilePathForRecording.empty()) {

				// if (ALPHA_OR_BETA_VERSION && audioFile->numReasons) FREEZE_WITH_ERROR("E281"); // It definitely
				// shouldn't still have any reasons
				//  No - it could still have a reason - the reason of its SampleRecorder. Scenario where this happened
				//  was: recording AudioClip (instance) into Arranger when loading a new song, first causes Arranger
				//  playback to switch to Session playback, which causes finishLinearRecording() on AudioClip, so when
				//  song-swap does happen, the AudioClip no longer has a recorder, so the recorder doesn't clear stuff,
				//  and it's still not quite yet finalized the file, so still holds the "reason" to the Sample.
				//  TODO: although the Sample doesn't store a pointer to the SampleRecorder, we could easily search for
				//  it - and delete it and its "reason"?

				// We know Sample belonged to an AudioClip originally because only those ones can be TEMP
				highestUsedAudioRecordingNumberNeedsReChecking[util::to_underlying(AudioRecordingFolder::CLIPS)] = true;

				// We may have deleted several, so do make sure we go and re-check from 0
				highestUsedAudioRecordingNumber[util::to_underlying(AudioRecordingFolder::CLIPS)] = -1;

				deleteUnusedAudioFileFromMemory(*audioFile, e);
				e--;
			}
		}
	}
}

// Oi, don't even think about modifying this to take a Sample* pointer - cos the whole Sample could get deleted during
// the card access.
Error AudioFileManager::getUnusedAudioRecordingFilePath(std::string& filePath, std::string* tempFilePathForRecording,
                                                        AudioRecordingFolder folder, uint32_t* getNumber,
                                                        const char* channelName, std::string* songName) {
	const auto folderID = util::to_underlying(folder);

	Error error = StorageManager::initSD();
	if (error != Error::NONE) {
		return error;
	}
	// this caches the last used numbers in the main folders to avoid repeated reads. This is probably good there since
	// some people have hundreds of recordings, but it's unnecessary for song specific folders since the number of
	// recordings will be much smaller
	if (highestUsedAudioRecordingNumberNeedsReChecking[folderID]) {

		auto maybeDIR = staticDIR.open(audioRecordingFolderNames[folderID]);
		if (maybeDIR) {
			staticDIR = *maybeDIR;

			while (true) {
				loadAnyEnqueuedClusters();
				/* Read a directory item */
				staticFNO = D_TRY_CATCH(staticDIR.read(), error, {
					return Error::SD_CARD; // error if invalid
				});

				if (__builtin_expect((*(uint32_t*)staticFNO.altname & 0x00FFFFFF) == 0x00434552, 1)) { // "REC"
					if (*(uint32_t*)&staticFNO.altname[8] == 0x5641572E) {                             // ".WAV"

						int32_t thisSlot = memToUIntOrError(&staticFNO.altname[3], &staticFNO.altname[8]);
						if (thisSlot == -1) {
							continue;
						}

						if (thisSlot > highestUsedAudioRecordingNumber[folderID]) {
							highestUsedAudioRecordingNumber[folderID] = thisSlot;
						}
					}
				}
				else if (!staticFNO.altname[0]) {
					break; /* Break on end of dir */
				}
			}
			// f_closedir(&staticDIR);
		}

		highestUsedAudioRecordingNumberNeedsReChecking[folderID] = false;
	}

	highestUsedAudioRecordingNumber[folderID]++;

	D_PRINTLN("new file: --------------  %d", highestUsedAudioRecordingNumber[folderID]);

	filePath = audioRecordingFolderNames[folderID];

	bool doingTempFolder = (folder == AudioRecordingFolder::CLIPS);
	if (doingTempFolder) {
		(*tempFilePathForRecording) = audioRecordingFolderNames[folderID];
		(*tempFilePathForRecording).append("/TEMP");
	}

	// default to putting it in the main folder if the song isn't named
	if (songName->empty()) {
		filePath.append("/REC");
		filePath.append(deluge::string::fromInt(highestUsedAudioRecordingNumber[folderID], 5));
		filePath.append(".WAV");

		if (doingTempFolder) {
			(*tempFilePathForRecording).append(&filePath.c_str()[strlen(audioRecordingFolderNames[folderID])]);
		}
	}
	// otherwise file it under the song name
	else {
		char namedPath[255]{0};
		char tempPath[255]{0};
		int i = 0;
		bool changed = true;
		// iterate through the main and temp folders until we find a path that's free in both
		while (changed) {
			changed = false;
			snprintf(namedPath, sizeof(namedPath), "%s/%s/%s_%03d.wav", filePath.c_str(), songName->c_str(),
			         channelName, i);
			while (StorageManager::fileExists(namedPath)) {
				snprintf(namedPath, sizeof(namedPath), "%s/%s/%s_%03d.wav", filePath.c_str(), songName->c_str(),
				         channelName, i);
				i++;
				changed = true;
			}
			if (doingTempFolder) {
				snprintf(tempPath, sizeof(tempPath), "%s/%s/%s_%03d.wav", tempFilePathForRecording->c_str(),
				         songName->c_str(), channelName, i);

				while (StorageManager::fileExists(tempPath)) {
					snprintf(tempPath, sizeof(tempPath), "%s/%s/%s_%03d.wav", tempFilePathForRecording->c_str(),
					         songName->c_str(), channelName, i);
					i++;
					changed = true;
				}
			}
		}
		filePath = namedPath;
		if (doingTempFolder) {
			(*tempFilePathForRecording) = tempPath;
		}
	}

	*getNumber = highestUsedAudioRecordingNumber[folderID];

	return Error::NONE;
}

// Returns false if exists but can't be deleted
bool AudioFileManager::tryToDeleteAudioFileFromMemoryIfItExists(char const* filePath) {
	bool foundExact;

	for (int32_t t = 0; t < 2; t++) { // Got to do this twice, just in case there's a Sample and a WaveTable.

		int32_t i = audioFiles.search(filePath, &foundExact);
		if (!foundExact) {
			return true; // We're fine, it didn't exist
		}

		// Ok, it's in memory. Can we delete it - is it unused?
		AudioFile* audioFile = audioFiles[i];
		if (audioFile->isProjectReferenced()) {
			return false; // Alert - not only is it in memory, but it also can't be deleted
		}

		// Ok, it's unused. Delete it.
		deleteUnusedAudioFileFromMemory(*audioFile, i);
	}
	return true; // We're fine - it got deleted
}

void AudioFileManager::deleteUnusedAudioFileFromMemoryIndexUnknown(AudioFile& audioFile) {
	int32_t i = audioFiles.searchForExactObject(&audioFile);
	if (i < 0) {
#if ALPHA_OR_BETA_VERSION
		FREEZE_WITH_ERROR("E401"); // Leo got. And me! But now I've solved.
#endif
	}
	else {
		deleteUnusedAudioFileFromMemory(audioFile, i);
	}
}

void AudioFileManager::deleteUnusedAudioFileFromMemory(AudioFile& audioFile, int32_t i) {

	// Remove AudioFile from memory
	audioFiles.erase(audioFiles.begin() + i);
	// audioFile->remove(); // Remove from the unused AudioFiles list, where this already must have been. Actually
	// no, the destructor does this anyway.
	destroyAudioFileObject(audioFile); // ~AudioFile + free, routed through the manager if adopted
}

bool AudioFileManager::ensureEnoughMemoryForOneMoreAudioFile() {
	try {
		audioFiles.reserve(audioFiles.size() + 1);
		return true;
	} catch (deluge::exception&) {
		return false;
	}
}

Error AudioFileManager::setupAlternateAudioFileDir(std::string& newPath, char const* rootDir,
                                                   const char* songFilenameWithoutExtension) {

	newPath = rootDir;

	newPath.append("/");

	newPath.append(songFilenameWithoutExtension);

	return Error::NONE;
}

Error AudioFileManager::setupAlternateAudioFilePath(std::string& newPath, int32_t dirPathLength, std::string& oldPath) {
	newPath.resize(dirPathLength);
	newPath.append(&oldPath.c_str()[8]);
	Error error = Error::NONE; // The [8] skips us past "SAMPLES/"
	if (error != Error::NONE) {
		return error;
	}

	int32_t pos = dirPathLength;

	while (true) {
		char const* newPathChars = newPath.c_str();
		char const* slashAddress = strchr(&newPathChars[pos], '/');
		if (!slashAddress) {
			break;
		}
		int32_t slashPos = (uint32_t)slashAddress - (uint32_t)newPathChars;
		newPath[slashPos] = '_';
		pos = slashPos + 1;
	}

	return Error::NONE;
}

AudioFile* AudioFileManager::getAudioFileFromFilename(std::string& filePath, bool mayReadCard, Error* error,
                                                      FilePointer* suppliedFilePointer, AudioFileType type,
                                                      bool makeWaveTableWorkAtAllCosts) {

	*error = Error::NONE;

	std::string backedUpFilePath;

	// See if it's already in memory.
	bool foundExact;
	int32_t audioFileI = audioFiles.search(filePath.c_str(), &foundExact);

	// If that basic search by the file's "normal" path already found it, then great.
	if (foundExact) {
successfullyFoundInMemory:
		AudioFile* foundAudioFile = audioFiles[audioFileI];

		// If correct type...
		if (foundAudioFile->type == type) {
			return foundAudioFile;
		}

		// Otherwise, see if a neighbouring one has the right type
		int32_t tryOffset = -1;

		if (audioFileI >= 1) {
doTryOffset:
			AudioFile* foundAudioFile2 = audioFiles[audioFileI + tryOffset];

			if (foundAudioFile2->type == type && !strcasecmp(filePath.c_str(), foundAudioFile2->filePath.c_str())) {
				return foundAudioFile2;
			}
		}

		if (tryOffset == -1 && audioFileI < static_cast<int32_t>(audioFiles.size()) - 1) {
			tryOffset = 1;
			goto doTryOffset;
		}

		// If here, we didn't find the correct type, but we did find an AudioFile for the correct filePath, just the
		// wrong type.

		// If we want WaveTable but got Sample, we can convert. (Otherwise, we can't.)
		if (type == AudioFileType::WAVETABLE) {

			// Stereo files can never be WaveTables
			if (((Sample*)foundAudioFile)->numChannels != 1) {
				*error = Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO;
				return NULL;
			}

			// And if the user isn't insisting, then some other signs show that we probably don't want to load this
			// as a WaveTable
			if (!makeWaveTableWorkAtAllCosts) {
				if (isAiffFilename(foundAudioFile->filePath.c_str())) {
notLoadableAsWaveTable:
					*error = Error::FILE_NOT_LOADABLE_AS_WAVETABLE;
					return NULL;
				}

				// If this isn't actually a wavetable-specifying file or at least a wavetable-looking length, and
				// the user isn't insisting, then opt not to do it.
				if (!((Sample*)foundAudioFile)->fileExplicitlySpecifiesSelfAsWaveTable) {
					if (((Sample*)foundAudioFile)->lengthInSamples & 2047) {
						goto notLoadableAsWaveTable;
					}
				}
			}

			void* waveTableMemory = deluge::memory::alloc_external(sizeof(WaveTable), 16);
			if (!waveTableMemory) {
				*error = Error::INSUFFICIENT_RAM;
				return NULL;
			}

			WaveTable* newWaveTable = new (waveTableMemory) WaveTable;

			adoptAudioFileObject(newWaveTable); // resource-manager evictable object (before addReason)
			newWaveTable->addReason();          // So it's protected while setting up.
			foundAudioFile->addReason();

			*error = newWaveTable->setup((Sample*)foundAudioFile);
			if (*error != Error::NONE) {
waveTableCloneError:
				destroyAudioFileObject(*newWaveTable);
				return NULL;
			}

			auto inserted = audioFiles.insertElement(newWaveTable);
			*error = inserted ? Error::NONE : inserted.error();

			newWaveTable->removeReason("E397");
			foundAudioFile->removeReason("E398");

			if (!inserted) {
				goto waveTableCloneError;
			}

			return newWaveTable;
		}

		// Or if we want Sample but got Wavetable, can't convert, so we'll have to load from file after all. Reset
		// filePath if needed (pretty unlikely scenario).
		else {
			if (!backedUpFilePath.empty()) {
				filePath = backedUpFilePath;
			}
		}
	}

	// Or, if that didn't find the audio file in memory...
	else {

		// If we're loading a preset (not a Song, and not just browsing audio files), we should search in memory for
		// the alternate path
		if (alternateLoadDirStatus == AlternateLoadDirStatus::MIGHT_EXIST
		    || alternateLoadDirStatus == AlternateLoadDirStatus::DOES_EXIST) {
			if (thingTypeBeingLoaded != ThingType::SONG) {
				std::string searchPath;
				searchPath = alternateAudioFileLoadPath;
				searchPath.append("/");
				char const* fileName = getFileNameFromEndOfPath(filePath.c_str());
				searchPath.append(fileName);

				audioFileI = audioFiles.search(searchPath.c_str(), &foundExact);
				if (foundExact) {
					// Tiny bit cheeky, but we're going to update the file path actually stored in the AudioFile to
					// reflect this alternate location, which will no longer be considered alternate.
					backedUpFilePath = filePath; // First we back up the original filePath.
					filePath = searchPath;
					goto successfullyFoundInMemory;
				}
			}
		}
	}

tryLoadingFromCard:
	// Otherwise, try and load it in
	if (!mayReadCard) {
		return NULL;
	}

	if (cardDisabled) {
		*error = Error::SD_CARD;
		return NULL;
	}

	// Deactivated this because it stuff up the sampleI we already found
	/*
	if (!ensureEnoughMemoryForOneMoreSample()) {
	    *error = Error::INSUFFICIENT_RAM;
	    return NULL;
	}
	*/

	std::string usingAlternateLocation;

	FilePointer effectiveFilePointer;

	// If we got given a FilePointer, it's easy
	if (suppliedFilePointer) {
		effectiveFilePointer = *suppliedFilePointer;
	}

	// Otherwise go on the filePath
	else {

		bool alreadyTriedRegular = false;
		FRESULT result;

		// If we know the alternate load directory actually exists, then we should try that first, cos there's a
		// high chance the file is in there
		if (alternateLoadDirStatus == AlternateLoadDirStatus::DOES_EXIST) {

tryAlternateDoesExist:
			std::string proposedFileName;
			char const* proposedFileNamePointer;
			bool alreadyTriedSecondAlternate;

			// We'll first try the long file name, which contains all the folder names from the original path. This
			// is how collect-media saves look - for Songs. But, if that original path didn't begin with "SAMPLES/",
			// we can't do that.
			if (memcasecmp(filePath.c_str(), "SAMPLES/", 8)) {
				goto tryNextAlternate;
			}

			*error = setupAlternateAudioFilePath(proposedFileName, 0, filePath);
			if (*error != Error::NONE) {
				return nullptr; // This is to generate just the name of the file - not an entire path with folders -
				                // despite the function being called ...Path.
			}

			alreadyTriedSecondAlternate = false;

tryAnAlternate:
			proposedFileNamePointer = proposedFileName.c_str();
			result = create_name(&alternateLoadDir, &proposedFileNamePointer);
			if (result != FR_OK) {
				goto alternateFailed; // Can only fail if filename too weird.
			}

			result = dir_find(&alternateLoadDir);

			if (result != FR_OK) {
alternateFailed:
				if (!alreadyTriedSecondAlternate) {
tryNextAlternate:
					// Next up we'll try looking for just the filename that the original file had, without any added
					// folder names. This allows users to copy files into folders for instruments more easily, and
					// have them load.
					alreadyTriedSecondAlternate = true;
					char const* fileName = getFileNameFromEndOfPath(filePath.c_str());
					proposedFileName = fileName;
					goto tryAnAlternate;
				}
				if (alreadyTriedRegular) {
					goto notFound;
				}
				else {
					goto tryRegular;
				}
			}

			// Ok, found file - in the alternate location.
			effectiveFilePointer.sclust = ld_clust(&fileSystem, alternateLoadDir.dir);
			effectiveFilePointer.objsize = ld_dword(alternateLoadDir.dir + DIR_FileSize);

			usingAlternateLocation = alternateAudioFileLoadPath;
			usingAlternateLocation.append("/");
			usingAlternateLocation.append(proposedFileName);

			if (thingTypeBeingLoaded == ThingType::SYNTH || thingTypeBeingLoaded == ThingType::KIT) {
				// Special rule for loading presets with files in their dedicated "alternate" folder: must update
				// the AudioFile's filePath to point to that alternate location - and then treat them as normal (not
				// alternate).
				filePath = usingAlternateLocation;
				usingAlternateLocation.clear();
			}
		}

		// Otherwise, try the regular file path
		else {
tryRegular:
			result = f_open(&smDeserializer.readFIL, filePath.c_str(), FA_READ);

			// If that didn't work, try the alternate load directory, if we didn't already and it potentially exists
			if (result != FR_OK) {

				if (alternateLoadDirStatus == AlternateLoadDirStatus::MIGHT_EXIST) {

					result = f_opendir(&alternateLoadDir, alternateAudioFileLoadPath.c_str());
					if (result != FR_OK) {
						alternateLoadDirStatus = AlternateLoadDirStatus::NOT_FOUND;
						goto notFound;
					}

					alternateLoadDirStatus = AlternateLoadDirStatus::DOES_EXIST;

					alreadyTriedRegular = true;
					goto tryAlternateDoesExist;
				}

notFound:
				*error = Error::FILE_UNREADABLE;
				return NULL;
			}

			// Ok, found file.
			effectiveFilePointer.sclust = activeDeserializer->readFIL.obj.sclust;
			effectiveFilePointer.objsize = activeDeserializer->readFIL.obj.objsize;
		}
	}

	return buildAudioFileFromCard(filePath, usingAlternateLocation, effectiveFilePointer, type,
	                              makeWaveTableWorkAtAllCosts, error);
}

AudioFile* AudioFileManager::buildAudioFileFromCard(const std::string& filePath,
                                                    const std::string& usingAlternateLocation,
                                                    FilePointer& effectiveFilePointer, AudioFileType type,
                                                    bool makeWaveTableWorkAtAllCosts, Error* error) {
	// 0-byte files not allowed.
	if (effectiveFilePointer.objsize == 0) {
		*error = Error::FILE_CORRUPTED;
		return nullptr;
	}
	// Files bigger than 1GB not allowed.
	if (effectiveFilePointer.objsize > kMaxFileSize) {
		*error = Error::FILE_TOO_BIG;
		return nullptr;
	}

	const uint32_t numClusters = ((effectiveFilePointer.objsize - 1) >> Cluster::size_magnitude) + 1;
	const int32_t memorySizeNeeded = (type == AudioFileType::SAMPLE) ? sizeof(Sample) : sizeof(WaveTable);

	void* audioFileMemory = deluge::memory::alloc_external(memorySizeNeeded, 16);
	if (audioFileMemory == nullptr) {
		*error = Error::INSUFFICIENT_RAM;
		return nullptr;
	}

	AudioFile* audioFile;
	if (type == AudioFileType::SAMPLE) {
		audioFile = new (audioFileMemory) Sample;
		adoptAudioFileObject(audioFile); // resource-manager evictable object (before addReason)
		audioFile->addReason(); // So it's protected while setting up. Must do this before calling initialize().
		*error = static_cast<Sample*>(audioFile)->initialize(numClusters);
		if (*error != Error::NONE) { // Very rare, only if not enough RAM
			destroyAudioFileObject(*audioFile);
			return nullptr;
		}

		audioFile->filePath = filePath;
		audioFile->loadedFromAlternatePath = usingAlternateLocation;

		// Go directly to god-mode and store the address of each of the file's clusters.
		uint32_t currentClusterIndex = 0;
		uint32_t currentSDCluster = effectiveFilePointer.sclust; // First cluster, whose address we already got.
		while (true) {
			static_cast<Sample*>(audioFile)->clusters[currentClusterIndex].sdAddress =
			    clst2sect(&fileSystem, currentSDCluster);

			currentClusterIndex++;
			if (currentClusterIndex >= numClusters) {
				break;
			}
			currentSDCluster = get_fat_from_fs(&fileSystem, currentSDCluster);
			if (currentSDCluster == 0xFFFFFFFF || currentSDCluster < 2) {
				break;
			}
		}

		// The byte source streams the clusters; its destructor releases the held cluster's reason.
		ClusterByteSource source{static_cast<Sample&>(*audioFile), effectiveFilePointer.objsize};
		*error = audioFile->loadFile(source, makeWaveTableWorkAtAllCosts);
	}
	else {
		audioFile = new (audioFileMemory) WaveTable;
		adoptAudioFileObject(audioFile); // resource-manager evictable object (before addReason)
		audioFile->addReason();          // So it's protected while setting up.

		audioFile->filePath = filePath;
		audioFile->loadedFromAlternatePath = usingAlternateLocation;

		// WaveTable reads the file more normally through FatFS, so "open" it.
		StorageManager::openFilePointer(&effectiveFilePointer, smDeserializer); // It never returns fail.

		// One deserializer-backed source serves both the header parse (via the AudioByteSource surface) and
		// WaveTable::setup's zero-copy band read (via its cluster accessors) — hence passed both ways.
		DeserializerByteSource source{static_cast<uint32_t>(effectiveFilePointer.objsize)};
		*error = audioFile->loadFile(source, makeWaveTableWorkAtAllCosts, &source);
	}

	if (*error != Error::NONE) {
		// ~AudioFile removes the pointers back to the Sample / SampleClusters from any Clusters;
		// destroyAudioFileObject also un-adopts + frees (through the manager if adopted).
		destroyAudioFileObject(*audioFile);
		return nullptr;
	}

	const auto insertedFile = audioFiles.insertElement(audioFile);
	if (!insertedFile) {
		*error = insertedFile.error();
		destroyAudioFileObject(*audioFile);
		return nullptr;
	}

	audioFile->finalizeAfterLoad(effectiveFilePointer.objsize);
	audioFile->removeReason("E399"); // Setup done; drop the protect-during-setup reason (the caller re-leases).
	return audioFile;
}

#define REPORT_LOAD_TIME 0

bool AudioFileManager::loadCluster(Cluster& cluster, int32_t minNumReasonsAfter) {

	if (currentlyAccessingCard) {
		return false; // Could happen if we're trying to render a waveform but we're actually already inside the SD
		              // routine
	}

	// I don't think these should happen...
	if (clusterBeingLoaded != nullptr) {
		return false;
	}

	if (AudioEngine::audioRoutineLocked) {
		return false;
	}

	clusterBeingLoaded = &cluster;
	minNumReasonsForClusterBeingLoaded = minNumReasonsAfter + 1;

	Sample* sample = cluster.sample;

	if (cluster.type != Cluster::Type::SAMPLE) {
		FREEZE_WITH_ERROR("E205"); // Chris F got this, so gonna leave checking in release build
	}

#if ALPHA_OR_BETA_VERSION
	if (cluster.leaseCount() == 0) {
		// Ok, I think we know there's at least 1 reason at the point this function's called, because
		FREEZE_WITH_ERROR("E204");
	}
	// it'd only be in the loading queue if it had a "reason".
	if (!sample) {
		FREEZE_WITH_ERROR("E206");
	}
#endif

	// So that it can't accidentally hit 0 reasons while we're loading it,
	// cos then it might get deallocated.
	cluster.addReason();

	bool ok = readClusterData(cluster, minNumReasonsAfter);

	clusterBeingLoaded = nullptr;
	removeReasonFromCluster(cluster, ok ? "E034" : "E033");

#if ALPHA_OR_BETA_VERSION
	if (ok) {
		if (static_cast<int32_t>(cluster.leaseCount()) < minNumReasonsAfter) {
			FREEZE_WITH_ERROR("i037");
		}
		if (cluster.sample->clusters[cluster.clusterIndex].cluster != &cluster) {
			FREEZE_WITH_ERROR("E438");
		}
	}
#endif

	return ok;
}

// The cluster data reader extracted from loadCluster (Step 1 of the resource-manager
// integration): the pure data work — sector count, read from the card, conversion, and the
// inter-cluster boundary fixups. No orchestration (the card-state guards, clusterBeingLoaded,
// the loading "reason", and the loadingQueue stay in loadCluster). This is the seam the resource
// manager will use as a materialize Source. `minNumReasonsAfter` only feeds the ALPHA sanity checks.
bool AudioFileManager::readClusterData(Cluster& cluster, [[maybe_unused]] int32_t minNumReasonsAfter) {
	Sample* sample = cluster.sample;
	int32_t clusterIndex = cluster.clusterIndex;

	// Failure exits jump here (kept above the local inits so the backward gotos don't cross them).
	if (false) {
getOutEarly:
		return false;
	}

	int32_t numSectors = Cluster::size >> 9;

	// If this is the last Cluster, and we do know what the audio data length is...
	if (sample->audioDataLengthBytes && sample->audioDataLengthBytes != 0x8FFFFFFFFFFFFFFF) {
		uint32_t audioDataEndPosBytes = sample->audioDataLengthBytes + sample->audioDataStartPosBytes;
		uint32_t startByteThisCluster = clusterIndex << Cluster::size_magnitude;
		int32_t bytesToRead = audioDataEndPosBytes - startByteThisCluster;
		if (bytesToRead <= 0) {
			D_PRINTLN("fail thing"); // Shouldn't really still happen
			goto getOutEarly;
		}
		if (bytesToRead < Cluster::size) {
			numSectors = ((bytesToRead - 1) >> 9) + 1;
		}
		// Otherwise, just leave it at the normal number of sectors
	}

#if ALPHA_OR_BETA_VERSION
	if ((uint32_t)cluster.data & 0b11) {
		D_PRINTLN("SD read address misaligned by  %d", (int32_t)((uint32_t)cluster.data & 0b11));
	}
#endif

	AudioEngine::logAction("loadCluster");

#if REPORT_LOAD_TIME
	uint16_t startTime = MTU2.TCNT_0;
#endif

#if ALPHA_OR_BETA_VERSION
	if (cluster.type != Cluster::Type::SAMPLE) {
		FREEZE_WITH_ERROR("i023"); // Happened to me while thrash testing with reduced RAM
	}

	if (static_cast<int32_t>(cluster.leaseCount()) < minNumReasonsAfter + 1) {
		FREEZE_WITH_ERROR("i039"); // It's +1 because we haven't removed this function's "reason" yet.
	}
#endif

	DRESULT result = disk_read_without_streaming_first(deluge_block_sd_unit(), (BYTE*)cluster.data,
	                                                   sample->clusters[cluster.clusterIndex].sdAddress, numSectors);

#if REPORT_LOAD_TIME
	uint16_t endTime = MTU2.TCNT_0;
	uint16_t duration = endTime - startTime;
	int32_t uSec = timerCountToUS(duration);
	if (uSec > 7000) {
		D_PRINTLN(uSec);
	}
#endif

#if ALPHA_OR_BETA_VERSION
	if (cluster.type != Cluster::Type::SAMPLE) {
		FREEZE_WITH_ERROR("E207");
	}
	if (cluster.sample == nullptr) {
		FREEZE_WITH_ERROR("E208");
	}

	if (static_cast<int32_t>(cluster.leaseCount()) < minNumReasonsAfter + 1) {
		FREEZE_WITH_ERROR("i038"); // It's +1 because we haven't removed this function's "reason" yet.
	}
#endif

	// If that failed, get out
	if (result != 0u) {
		goto getOutEarly;
	}

	cluster.convertDataIfNecessary();

#if ALPHA_OR_BETA_VERSION
	if (static_cast<int32_t>(cluster.leaseCount()) < minNumReasonsAfter + 1) {
		FREEZE_WITH_ERROR("i040"); // It's +1 because we haven't removed this function's "reason" yet.
	}
#endif

	int32_t misalignment = sample->audioDataStartPosBytes & 0b11;

	// Give extra bytes to previous Cluster
	if (clusterIndex > 0) {
		Cluster* prevCluster = sample->clusters[cluster.clusterIndex - 1].cluster;

		if (prevCluster && prevCluster->loaded) {

			// We first copy our first 7 bytes from here to the end of the prev Cluster...
			memcpy(&prevCluster->data[Cluster::size], cluster.data, 7);

			// If 24-bit wrong-endian data...
			if (sample->rawDataFormat == RawDataFormat::ENDIANNESS_WRONG_24) {

				// If we hadn't previously written the "extra" bytes to the end of the prev Cluster and converted
				// them, do so now...
				if (!prevCluster->extraBytesAtEndConverted) {

					uint32_t bytesBeforeStartOfCluster = clusterIndex * Cluster::size - sample->audioDataStartPosBytes;
					int32_t bytesUnconvertedBeforeCluster = bytesBeforeStartOfCluster % 3;
					if (bytesUnconvertedBeforeCluster) {

						// There'll be one word in there which hasn't yet been converted. Do it now. (We've probably
						// just copied over the next one and a bit, which already was converted)
						int32_t startPos = Cluster::size - bytesUnconvertedBeforeCluster;
						uint8_t* thisNumber = (uint8_t*)&prevCluster->data[startPos];

						uint8_t temp = thisNumber[0];
						thisNumber[0] = thisNumber[2];
						thisNumber[2] = temp;

						// And now, copy 2 bytes back to this Cluster (that's the maximum that the float could have
						// been overhanging the boundary)
						memcpy(cluster.data, &prevCluster->data[Cluster::size], 2);
					}

					prevCluster->extraBytesAtEndConverted = true;
				}
			}

			// Or, all other types of raw data conversion
			else if (sample->rawDataFormat != RawDataFormat::NATIVE) {

				// If we haven't previously written the "extra" bytes to the end of the prev Cluster and converted
				// them, do so now...
				if (!prevCluster->extraBytesAtEndConverted) {

					// If misaligned from the 4-byte boundary
					if (misalignment) {

						// There'll be one word in there which hasn't yet been converted. Do it now. (We've probably
						// also just moved over the next one too, which already was converted)
						int32_t startPos = Cluster::size - 4 + misalignment;
						auto& thisNumber = reinterpret_cast<int32_t&>(prevCluster->data[startPos]);
						thisNumber = sample->convertToNative(thisNumber);

						// And now, copy 3 bytes back to this Cluster (that's the maximum that the float could have
						// been overhanging the boundary)
						memcpy(cluster.data, &prevCluster->data[Cluster::size], 3);
					}

					prevCluster->extraBytesAtEndConverted = true;
				}
			}

			cluster.extraBytesAtStartConverted = true;
		}
	}

	// Grab extra bytes from next Cluster
	if (clusterIndex < static_cast<int32_t>(sample->clusters.size()) - 1) {
		Cluster* nextCluster = sample->clusters[cluster.clusterIndex + 1].cluster;

		if (nextCluster && nextCluster->loaded) {

			// If 24-bit wrong-endian data...
			if (sample->rawDataFormat == RawDataFormat::ENDIANNESS_WRONG_24) {

				uint32_t bytesBeforeStartOfNextCluster =
				    (clusterIndex + 1) * Cluster::size - sample->audioDataStartPosBytes;
				int32_t bytesUnconvertedBeforeNextCluster = bytesBeforeStartOfNextCluster % 3;

				// If one word missed conversion...
				if (bytesUnconvertedBeforeNextCluster) {

					// If we had't previously converted the first couple of bytes of the next Cluster...
					if (!nextCluster->extraBytesAtStartConverted) {

						// We first copy the next Cluster first 7 bytes to the end of this Cluster
						memcpy(&cluster.data[Cluster::size], nextCluster->data, 7);
					}

					// Or, if we *had* previously converted the first bytes of the next Cluster...
					else {

						// Grab the unconverted bytes back from where we backed them up to
						memcpy(&cluster.data[Cluster::size], nextCluster->firstThreeBytesPreDataConversion, 2);
					}

					// There'll be one word in there which hasn't yet been converted. Do it now. (We've probably
					// just copied over the next one and a bit, which already was converted)
					uint8_t* thisNumber = (uint8_t*)&cluster.data[Cluster::size - bytesUnconvertedBeforeNextCluster];

					uint8_t temp = thisNumber[0];
					thisNumber[0] = thisNumber[2];
					thisNumber[2] = temp;

					// If we had't previously converted the first couple of bytes of the next Cluster, do so now...
					if (!nextCluster->extraBytesAtStartConverted) {
						nextCluster->extraBytesAtStartConverted = true;

						// And now, copy 2 bytes back to the next Cluster (that's the maximum that the 24-bit
						// int32_t could have been overhanging the boundary)
						memcpy(nextCluster->data, &cluster.data[Cluster::size], 2);
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
			else if (sample->rawDataFormat != RawDataFormat::NATIVE) {

				// If one word missed conversion...
				if (misalignment) {
					int32_t startPos = Cluster::size - 4 + misalignment;
					auto& thisNumber = reinterpret_cast<int32_t&>(cluster.data[startPos]);

					// If we had't previously converted the first couple of bytes of the next Cluster, do so now...
					if (!nextCluster->extraBytesAtStartConverted) {

						// We first copy the next Cluster first 7 bytes to the end of this Cluster
						memcpy(&cluster.data[Cluster::size], nextCluster->data, 7);

						// There'll be one word in there which hasn't yet been converted from float. Do it now
						thisNumber = sample->convertToNative(thisNumber);

						// And now, copy 3 bytes back to the next Cluster (that's the maximum that the float could
						// have been overhanging the boundary)
						memcpy(nextCluster->data, &cluster.data[Cluster::size], 3);

						nextCluster->extraBytesAtStartConverted = true;
					}

					// Or, if we *had* previously converted the first bytes of the next Cluster...
					else {

						// Grab the unconverted bytes back from where we backed them up to
						memcpy(&cluster.data[Cluster::size], nextCluster->firstThreeBytesPreDataConversion, 3);

						// There'll be one word in there which hasn't yet been converted from float. Do it now
						thisNumber = sample->convertToNative(thisNumber);

						// And now just copy the converted-from-float first bytes from the next Cluster to the end
						// of this one
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
				memcpy(&cluster.data[Cluster::size], nextCluster->data, 7);
			}

			cluster.extraBytesAtEndConverted = true;
		}
	}

	cluster.loaded = true;
	// Manager-owned readiness: a chunk fetched via `request` (CLUSTER_ENQUEUE prefetch) was reserved in
	// the Loading state; now its data is read, signal the manager so the async/RT `try_acquire` path
	// sees it ready. `cluster.loaded` stays the C++ sync-path field; this keeps the manager in sync.
	{
		DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
		if (mgr != nullptr) {
			deluge_resource_mark_ready(mgr, &cluster);
		}
	}
	return true;
}

// Only needs calling a couple times per second. Must be called outside of the audio / SD-reading routine
// Call this repeatedly so SD card is re-initialized on re-insert before we actually urgently need audio from it
void AudioFileManager::slowRoutine() {

	// Drain card-detect events from the BSP (pull-based; the card-detect ISR
	// records them, we poll here). An ejection marks the card unusable until it
	// is re-initialised by the re-insert path below.
	if (deluge_block_poll_card_event(deluge_block_sd_unit()) == DELUGE_CARD_EVENT_EJECTED) {
		setCardEjected();
	}

	// If we know the card's been ejected...
	if (cardEjected && !isSDRoutineActive()) {
		Error error = StorageManager::initSD();
		if (error == Error::NONE) {
			cardEjected = false;
		}
	}

	// NOTE: (Kate) There was dead code here referencing things that no longer
	// exist (NUM_LOADED_SAMPLE_CHUNK_ALLOCATION_QUEUES, availableClusterQueues)
	// It has been removed.
	// see
	// https://github.com/SynthstromAudible/DelugeFirmware/blob/866a71d0394e259a5b3db9d4fde605511bd1c67d/src/deluge/storage/audio/audio_file_manager.cpp#L1238
	// for a copy if ever needed
}

#define REPORT_AWAY_TIME 0

#if REPORT_AWAY_TIME
uint16_t timeLastFinish;
#endif

void AudioFileManager::loadAnyEnqueuedClusters(int32_t maxNum, bool mayProcessUserActionsBetween) {

	if (currentlyAccessingCard) {
		return;
	}
	if (clusterBeingLoaded) {
		return; // One might be having stuff done to it, like having its data converted, but not actually reading
		        // the card right now
	}
	if (AudioEngine::audioRoutineLocked) {
		return; // Not sure if this should be neccesary?
	}

	// Cannot call any functions in here which will read the SD card, other than loadCluster(), otherwise that'll
	// re-call this function!

	if (cardEjected || cardDisabled) {

performActionsAndGetOut:
		if (mayProcessUserActionsBetween) {
			playbackHandler.slowRoutine();
		}
		return;
	}

	if (!StorageManager::checkSDInitialized()) {
		goto performActionsAndGetOut; // In case the card somehow died
	}

	int32_t count = 0;

#if REPORT_AWAY_TIME
	uint16_t startTime = MTU2.TCNT_0;
	uint16_t awayTime = startTime - timeLastFinish;
	int32_t uSecAway = timerCountToUS(awayTime);
	if (uSecAway > 1000) {
		D_PRINTLN("away  %d", uSecAway);
	}
#endif

	while (true) {

		// We now have an opportunity, since we're not reading the card, to process any pending user actions like
		// undo / redo.
		if (mayProcessUserActionsBetween) {
			playbackHandler.slowRoutine();
		}

		// Pop the most-urgent queued + still-leased cluster's backing (the manager skips/de-queues
		// abandoned-unleased ones). This prevents loading clusters quickly culled after enqueue.
		void* p = deluge_resource_loader_next(GeneralMemoryAllocator::get().resourceManager());

		// no more clusters to load, so exit
		if (p == nullptr) {
			return;
		}
		Cluster* cluster = reinterpret_cast<Cluster*>(p);

		// The unloadable domain-filter stays here (the manager doesn't know it). markAsUnloadable
		// already de-queues, so this is the safety net — loader_next has cleared its queued flag, so
		// skipping won't loop.
		if (cluster->unloadable) {
			continue;
		}

		// cluster has at least 1 "reason". If it didn't, it would have been removed from the load-queue

		// Do the actual loading
		if (cluster->type != Cluster::Type::SAMPLE) {
			FREEZE_WITH_ERROR("E235"); // Cos Chris F got an E205
		}

		allowSomeUserActionsEvenWhenInCardRoutine = true; // Sorry!!
		bool success;
		if (cluster->type == Cluster::Type::SAMPLE && cluster->sample != nullptr
		    && cluster->sample->resourceAssetId != DELUGE_RESOURCE_NO_ASSET) {
			// Manager-owned cluster: it's already constructed + leased (via request), so just do the
			// read directly. NOT loadCluster — its addReason/removeReason would desync the manager
			// lease, and its `audioRoutineLocked` guard would refuse to load during the offline render
			// (the headless-render streaming starvation we're fixing). The lease persists; the read
			// just flips loaded=true (or fails, handled below as for legacy).
			success = readClusterData(*cluster, 0);
		}
		else {
			success = loadCluster(*cluster);
		}
		allowSomeUserActionsEvenWhenInCardRoutine = false;

		// If that didn't work, presumably because the SD card got ejected...
		if (!success) {
			D_PRINTLN("load Cluster fail");

			// If the Cluster is now down to 0 reasons (i.e. it lost a reason while being loaded), then it's already
			// been made "available" and we don't have a problem
			if (!cluster->leaseCount()) {}

			// Otherwise, there are still "reasons" waiting for this Cluster to become loaded, so we need to put it
			// back in the loading queue. Presumably it won't actually get loaded for a while - only when the user
			// re-inserts the card
			else {

				if (cluster->type != Cluster::Type::SAMPLE) {
					FREEZE_WITH_ERROR("E237"); // Cos Chris F got an E205
				}

				// TODO: If that fails, it'll just get awkwardly forgotten about
				deluge_resource_loader_enqueue(GeneralMemoryAllocator::get().resourceManager(), cluster->resourceSlot,
				                               0xFFFFFFFF); // lowest priority

				// Also, return now. Normally we stay here til there's nothing left in the load-queue, but now that
				// would leave us in an infinite loop!
				break;
			}
		}

		count++;
		if (count >= maxNum) {
			break; // Keep things sane?
		}
	}

#if REPORT_AWAY_TIME
	timeLastFinish = MTU2.TCNT_0;
#endif
}

void AudioFileManager::removeReasonFromCluster(Cluster& cluster, [[maybe_unused]] char const* errorCode,
                                               bool deletingSong) {
	(void)deletingSong;
	// Every cluster is a manager-owned chunk — SAMPLE / PERC leased via the owner's Asset, SAMPLE_CACHE
	// resident via the cache's Asset (and leased by the low-level reader while it streams it). A removed
	// reason is just a manager lease drop: the cluster stays resident (cached, evictable under pressure),
	// never enqueued/destroyed here. The lease count lives in the manager's chunk slot (leaseCount()).
	if (ALPHA_OR_BETA_VERSION && cluster.leaseCount() == 0) {
		FREEZE_WITH_ERROR(errorCode); // removing a reason that was never there
	}
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	if (mgr != nullptr) {
		deluge_resource_release(mgr, &cluster); // unlease (backing ptr == the Cluster slot)
	}
}

bool AudioFileManager::loadingQueueHasAnyLowestPriorityElements() {
	return deluge_resource_loader_has_lowest(GeneralMemoryAllocator::get().resourceManager());
}

// Caller must also set alternateAudioFileLoadPath.
void AudioFileManager::thingBeginningLoading(ThingType newThingType) {
	alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;
	thingTypeBeingLoaded = newThingType;
}

void AudioFileManager::thingFinishedLoading() {
	alternateAudioFileLoadPath.clear();
	alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	thingTypeBeingLoaded = ThingType::NONE;
}
