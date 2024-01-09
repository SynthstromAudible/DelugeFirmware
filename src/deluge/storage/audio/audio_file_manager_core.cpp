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
#include "storage/storage_manager.h"

#include "io/debug/print.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "storage/cluster/cluster.h"

AudioFileManager audioFileManager{};

AudioFileManager::AudioFileManager() {
	cardDisabled = false;
	alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	thingTypeBeingLoaded = ThingType::NONE;

	for (int32_t i = 0; i < kNumAudioRecordingFolders; i++) {
		highestUsedAudioRecordingNumber[i] = -1;
		highestUsedAudioRecordingNumberNeedsReChecking[i] = true;
	}
}

void AudioFileManager::init() {

	clusterBeingLoaded = NULL;

	int32_t error = storageManager.initSD();
	if (!error) {
		setClusterSize(fileSystemStuff.fileSystem.csize * 512);

		Debug::print("clusterSize ");
		Debug::println(clusterSize);
		Debug::print("clusterSizeMagnitude ");
		Debug::println(clusterSizeMagnitude);
		cardEjected = false;
	}

	else {
		clusterSize = 32768;
		clusterSizeMagnitude = 15;
		cardEjected = true;
	}

	clusterSizeAtBoot = clusterSize;

	void* temp = GeneralMemoryAllocator::get().allocLowSpeed(clusterSizeAtBoot + CACHE_LINE_SIZE * 2);
	storageManager.fileClusterBuffer = (char*)temp + CACHE_LINE_SIZE;

	clusterObjectSize = sizeof(Cluster) + clusterSize;
}

void AudioFileManager::addReasonToCluster(Cluster* cluster) {
	// If it's going to cease to be zero, it's become unavailable
	if (cluster->numReasonsToBeLoaded == 0) {
		cluster->remove();
		//*cluster->getAnyReasonsPointer() = reasonType;
	}

	cluster->numReasonsToBeLoaded++;
}

Cluster* AudioFileManager::allocateCluster(ClusterType type, bool shouldAddReasons, void* dontStealFromThing) {

	void* clusterMemory = GeneralMemoryAllocator::get().allocStealable(clusterObjectSize, dontStealFromThing);
	if (!clusterMemory) {
		return NULL;
	}

	Cluster* cluster = new (clusterMemory) Cluster();

	cluster->type = type;

	if (shouldAddReasons) {
		addReasonToCluster(cluster);
	}

	return cluster;
}

void AudioFileManager::deallocateCluster(Cluster* cluster) {
	cluster->~Cluster(); // Removes reasons, and / or from stealable list
	delugeDealloc(cluster);
}

void AudioFileManager::deleteUnusedAudioFileFromMemory(AudioFile* audioFile, int32_t i) {

	// Remove AudioFile from memory
	audioFiles.removeElement(i);
	//audioFile->remove(); // Remove from the unused AudioFiles list, where this already must have been. Actually no, the destructor does this anyway.
	audioFile->~AudioFile();
	delugeDealloc(audioFile);
}

void AudioFileManager::deleteUnusedAudioFileFromMemoryIndexUnknown(AudioFile* audioFile) {
	int32_t i = audioFiles.searchForExactObject(audioFile);
	if (i < 0) {
#if ALPHA_OR_BETA_VERSION
		FREEZE_WITH_ERROR("E401"); // Leo got. And me! But now I've solved.
#endif
	}
	else {
		deleteUnusedAudioFileFromMemory(audioFile, i);
	}
}

// Currently there's no risk of trying to enqueue a cluster multiple times, because this function only gets called after it's freshly allocated
int32_t AudioFileManager::enqueueCluster(Cluster* cluster, uint32_t priorityRating) {
	return loadingQueue.add(cluster, priorityRating);
}

bool AudioFileManager::ensureEnoughMemoryForOneMoreAudioFile() {

	return audioFiles.ensureEnoughSpaceAllocated(1);
}

void AudioFileManager::removeReasonFromCluster(Cluster* cluster, char const* errorCode) {
	cluster->numReasonsToBeLoaded--;

	if (cluster == clusterBeingLoaded && cluster->numReasonsToBeLoaded < minNumReasonsForClusterBeingLoaded) {
		FREEZE_WITH_ERROR("E041"); // Sven got this!
	}

	// If it's now zero, it's become available
	if (cluster->numReasonsToBeLoaded == 0) {

		// Bug hunting
		if (ALPHA_OR_BETA_VERSION && cluster->numReasonsHeldBySampleRecorder) {
			FREEZE_WITH_ERROR("E364");
		}

		// If it's still in the load queue, remove it from there. (We know that it isn't in the process of being loaded right now
		// because that would have added a "reason", so we wouldn't be here.)
		if (loadingQueue.removeIfPresent(cluster)) {

			// Tell its Cluster to forget it exists
			cluster->sample->clusters.getElement(cluster->clusterIndex)->cluster = NULL;

			deallocateCluster(cluster); // It contains nothing, so completely recycle it
		}

		else {
			GeneralMemoryAllocator::get().putStealableInAppropriateQueue(
			    cluster); // It contains data we may want at some future point, so file it away
		}

		//*cluster->getAnyReasonsPointer() = ANY_REASONS_NO;
	}

	else if (cluster->numReasonsToBeLoaded < 0) {
#if ALPHA_OR_BETA_VERSION
		if (cluster->sample) { // "Should" always be true...
			Debug::print("reason remains on cluster of sample: ");
			Debug::println(cluster->sample->filePath.get());
		}
		FREEZE_WITH_ERROR(errorCode);
#else
		display->displayPopup(errorCode);  // For non testers, just display the error code without freezing
		cluster->numReasonsToBeLoaded = 0; // Save it from crashing or anything
#endif
	}
}

int32_t AudioFileManager::setupAlternateAudioFileDir(String* newPath, char const* rootDir,
                                                     String* songFilenameWithoutExtension) {

	int32_t error = newPath->set(rootDir);
	if (error) {
		return error;
	}

	error = newPath->concatenate("/");
	if (error) {
		return error;
	}

	error = newPath->concatenate(songFilenameWithoutExtension);
	if (error) {
		return error;
	}

	return NO_ERROR;
}

void AudioFileManager::thingBeginningLoading(ThingType newThingType) {
	alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;
	thingTypeBeingLoaded = newThingType;
}

void AudioFileManager::thingFinishedLoading() {
	alternateAudioFileLoadPath.clear();
	alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	thingTypeBeingLoaded = ThingType::NONE;
}
