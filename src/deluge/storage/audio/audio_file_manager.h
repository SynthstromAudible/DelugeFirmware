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
#include "audio_file.h"
#include "definitions_cxx.hpp"
#include "storage/cluster/cluster.h"
#include "storage/cluster/cluster_priority_queue.h"
#include <array>
#include <cstdint>
#include <expected>
#include <strings.h>

extern "C" {
#include "fatfs/ff.h"
}

class Sample;
class SampleCache;
class String;
class SampleRecorder;
class Output;
class WaveTable;

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
	struct StringLessThan {
		bool operator()(String* const& lhs, String* const& rhs) const { return strcasecmp(lhs->get(), rhs->get()) < 0; }
	};

public:
	AudioFileManager();

	deluge::fast_map<String*, Sample*, StringLessThan> sampleFiles;
	deluge::fast_map<String*, WaveTable*, StringLessThan> wavetableFiles;

	void init();
	std::expected<AudioFile*, Error> getAudioFileFromFilename(String& fileName, bool mayReadCard,
	                                                          FilePointer* filePointer, AudioFileType type,
	                                                          bool makeWaveTableWorkAtAllCosts = false);
	bool loadCluster(Cluster& cluster, int32_t minNumReasonsAfter = 0);
	void loadAnyEnqueuedClusters(int32_t maxNum = 128, bool mayProcessUserActionsBetween = false);
	void removeReasonFromCluster(Cluster& cluster, char const* errorCode, bool deletingSong = false);

	void slowRoutine();

	std::expected<void, Error> setupAlternateAudioFilePath(String& newPath, int32_t dirPathLength, String& oldPath);
	std::expected<void, Error> setupAlternateAudioFileDir(String& newPath, char const* rootDir,
	                                                      String& songFilenameWithoutExtension);
	bool loadingQueueHasAnyLowestPriorityElements();
	/// If songname isn't supplied the file is placed in the main recording folder and named as samples/folder/REC###.
	/// If song and channel are supplied then it's placed in samples/folder/song/channel_###
	Error getUnusedAudioRecordingFilePath(String& filePath, String* tempFilePathForRecording,
	                                      AudioRecordingFolder folder, uint32_t* getNumber, const char* channelName,
	                                      String* songName);
	void deleteAnyTempRecordedSamplesFromMemory();

	void releaseFile(Sample& sample);
	void releaseFile(WaveTable& wavetable);
	bool releaseSampleAtFilePath(String& filePath);
	void releaseAllUnused();

	void thingBeginningLoading(ThingType newThingType);
	void thingFinishedLoading();

	void setCardRead() { cardReadOnce = true; }
	void setCardEjected() { cardEjected = true; }

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
