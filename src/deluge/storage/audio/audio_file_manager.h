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

#pragma once
#include "definitions_cxx.hpp"
#include "storage/audio/audio_file_vector.h"
#include "storage/cluster/cluster.h"
#include "storage/cluster/cluster_priority_queue.h"
#include <array>
#include <cstdint>

extern "C" {
#include "fatfs/ff.h"
}

class Sample;
class SampleCache;
class String;
class SampleRecorder;
class Output;

enum class AlternateLoadDirStatus {
	NONE_SET,
	NOT_FOUND,
	MIGHT_EXIST,
	DOES_EXIST,
};

char const* const audioRecordingFolderNames[] = {
    "SAMPLES/CLIPS",
    "SAMPLES/RECORD",
    "SAMPLES/RESAMPLE",
    "SAMPLES/EXPORTS",
};

/*
 * ===================== SD card audio streaming ==================
 *
 * Audio streaming (for Samples and AudioClips) from the SD card functions by loading
 * and caching Clusters of audio data from the SD card. A formatted card will have a
 * cluster size for the filesystem - often 32kB, but it could be as small as 4kB, or even smaller maybe?
 * The Deluge deals in these Clusters, whatever size they may be for the card, which makes
 * sense because one Cluster always exists in one physical place on the SD card (or any disk),
 * so may be easily loaded in one operation by DMA. Whereas consecutive clusters making up an
 * (audio) file are often placed in completely different physical locations.
 *
 * For a Sample associated with a Sound or AudioClip, the Deluge keeps the first two Clusters of that file
 * (from its set start-point and subject to reversing) permanently loaded in RAM, so playback of the
 * Sample may begin instantly when the Sound or AudioClip is played. And if the Sample has a loop-start point,
 * it keeps the first two Clusters from that point permanently loaded too.
 *
 * Then as the Sample plays, the currently-playing Cluster and the next one are kept loaded in RAM.
 * Or rather, as soon as the “play-head” enters a new Cluster, the Deluge immediately enqueues
 * the following Cluster to be loaded from the card ASAP.
 *
 * And then also, loaded Clusters remain loaded/cached in RAM for as long as possible while that RAM
 * isn’t needed for something more important, so they may be played again without having to reload
 * them from the card. Details on that process below.
 *
 * Quick note - Cluster objects are also used (in RAM) to store SampleCache data (which caches
 * Sample data post-repitching or post-pitch-shifting), and “percussive” audio data (“perc” for short)
 * which is condensed data for use by the time-stretching algorithm. The reason for these types
 * of data being housed in Cluster objects is largely legacy, but it also is handy because all
 * Cluster objects are made to be the same size in RAM, so “stealing” one will always make the
 * right amount of space for another (see below to see what “stealing” means).
 */

class AudioFileManager {
public:
	AudioFileManager();

	AudioFileVector audioFiles;

	void init();
	AudioFile* getAudioFileFromFilename(String& fileName, bool mayReadCard, Error* error, FilePointer* filePointer,
	                                    AudioFileType type, bool makeWaveTableWorkAtAllCosts = false);
	bool loadCluster(Cluster& cluster, int32_t minNumReasonsAfter = 0);
	void loadAnyEnqueuedClusters(int32_t maxNum = 128, bool mayProcessUserActionsBetween = false);
	void removeReasonFromCluster(Cluster& cluster, char const* errorCode, bool deletingSong = false);

	/// @brief Holds a Cluster that's likely to be requested soon, loading it first if need be.
	///
	/// prefetches_ is a central list of the Clusters we expect to be asked for next. We hold a reason on each, so
	/// unlike a merely cached Cluster it can't be stolen before it's used. The reason is held until claimPrefetch() is
	/// called for it, or it's released some other way below.
	/// Does nothing if it's already held, or if the list is full of Clusters that haven't loaded yet.
	///
	/// @param sample The Sample to load from.
	/// @param clusterIndex Which of its Clusters to load. Must be a valid index into sample->clusters.
	void prefetchCluster(Sample* sample, int32_t clusterIndex);

	/// @brief Lets go of a prefetched Cluster, now that something that's going to play it holds its own reason on it.
	///
	/// Does nothing if @p cluster isn't prefetched.
	void claimPrefetch(Cluster const* cluster);

	/// @brief Lets go of every prefetched Cluster of @p sample, e.g. because what was going to play it has stopped.
	void releasePrefetchesForSample(Sample const* sample);

	/// @brief Lets go of prefetched Clusters that will never load because they're unloadable.
	void releaseUnloadablePrefetches();

	/// @brief Lets go of every prefetched Cluster and its Sample, loaded or not.
	void releaseAllPrefetches();

	bool ensureEnoughMemoryForOneMoreAudioFile();

	void slowRoutine();

	Error setupAlternateAudioFilePath(String& newPath, int32_t dirPathLength, String& oldPath);
	Error setupAlternateAudioFileDir(String& newPath, char const* rootDir, const char* songFilenameWithoutExtension);
	/// Whether any Samples' first Clusters, which let them start instantly, are still waiting to load
	bool loadingQueueHasAnySampleStarts();
	/// If songname isn't supplied the file is placed in the main recording folder and named as samples/folder/REC###.
	/// If song and channel are supplied then it's placed in samples/folder/song/channel_###
	Error getUnusedAudioRecordingFilePath(String& filePath, String* tempFilePathForRecording,
	                                      AudioRecordingFolder folder, uint32_t* getNumber, const char* channelName,
	                                      String* songName);
	void deleteAnyTempRecordedSamplesFromMemory();
	void deleteUnusedAudioFileFromMemory(AudioFile& audioFile, int32_t i);
	void deleteUnusedAudioFileFromMemoryIndexUnknown(AudioFile& audioFile);
	bool tryToDeleteAudioFileFromMemoryIfItExists(char const* filePath);

	void thingBeginningLoading(ThingType newThingType);
	void thingFinishedLoading();

	void setCardRead() { cardReadOnce = true; }
	void setCardEjected() { cardEjected = true; }
	// Whether loadAnyEnqueuedClusters() is able to read the card at all
	bool canLoadClusters() const { return !cardEjected && !cardDisabled; }

	ClusterPriorityQueue loadingQueue;

	Cluster* clusterBeingLoaded;
	int32_t minNumReasonsForClusterBeingLoaded; // Only valid when clusterBeingLoaded is set. And this exists for bug
	                                            // hunting only.

	String alternateAudioFileLoadPath;
	AlternateLoadDirStatus alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
	ThingType thingTypeBeingLoaded = ThingType::NONE;
	DIR alternateLoadDir;

	std::array<int32_t, kNumAudioRecordingFolders> highestUsedAudioRecordingNumber;
	std::bitset<kNumAudioRecordingFolders> highestUsedAudioRecordingNumberNeedsReChecking;
	void firstCardRead();

private:
	/// @brief A Cluster that prefetchCluster() is holding. We hold one reason on it and one on its Sample, so the
	/// Sample can't be deleted while we still point at it.
	struct Prefetch {
		Cluster* cluster;
		Sample* sample;
	};

	/// @brief Most Clusters held at once. Bounds the RAM they can pin, and keeps them from crowding out Clusters
	/// something's actually waiting on.
	static constexpr size_t kMaxPrefetches = 32;
	/// Oldest first
	std::array<Prefetch, kMaxPrefetches> prefetches_;
	size_t numPrefetches_ = 0;

	/// @brief Removes the reasons prefetches_[i] holds, and removes it from prefetches_, keeping the order.
	void releasePrefetch(size_t i);
	/// @brief Releases every prefetch @p shouldRelease returns true for.
	void releasePrefetchesIf(auto shouldRelease);

	bool cardReadOnce{false};
	bool cardEjected;
	bool cardDisabled = false;

	uint32_t clusterSizeAtBoot{0};

	void cardReinserted();
	int32_t readBytes(char* buffer, int32_t num, int32_t* byteIndexWithinCluster, Cluster** currentCluster,
	                  uint32_t* currentClusterIndex, uint32_t fileSize, Sample* sample);
	int32_t loadAiff(Sample* newSample, uint32_t fileSize, Cluster** currentCluster, uint32_t* currentClusterIndex);
	int32_t loadWav(Sample* newSample, uint32_t fileSize, Cluster** currentCluster, uint32_t* currentClusterIndex);
};

extern AudioFileManager audioFileManager;
