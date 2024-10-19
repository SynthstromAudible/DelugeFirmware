/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "model/sample/sample_cluster.h"
#include "model/sample/sample_cluster_array.h"
#include "storage/audio/audio_file.h"
#include "util/container/array/ordered_resizeable_array.h"
#include "util/container/array/ordered_resizeable_array_with_multi_word_key.h"
#include "util/functions.h"

#define SAMPLE_DO_LOCKS (ALPHA_OR_BETA_VERSION)

#define RAW_DATA_FINE 0
#define RAW_DATA_FLOAT 1
#define RAW_DATA_UNSIGNED_8 2
#define RAW_DATA_ENDIANNESS_WRONG_16 3
#define RAW_DATA_ENDIANNESS_WRONG_24 4
#define RAW_DATA_ENDIANNESS_WRONG_32 5

#define MIDI_NOTE_UNSET -999
#define MIDI_NOTE_ERROR -1000

class LoadedSamplePosReason;
class SampleCache;
class MultisampleRange;
class TimeStretcher;
class SampleHolder;
class AbsValueFollower;

class Sample final : public AudioFile {
public:
	Sample();
	~Sample();

	void workOutBitMask();
	Error initialize(int32_t numClusters);
	void markAsUnloadable();
	float determinePitch(bool doingSingleCycle, float minFreqHz, float maxFreqHz, bool doPrimeTest);
	void workOutMIDINote(bool doingSingleCycle, float minFreqHz = 20, float maxFreqHz = 10000, bool doPrimeTest = true);
	uint32_t getLengthInMSec();
	SampleCache* getOrCreateCache(SampleHolder* sampleHolder, int32_t phaseIncrement, int32_t timeStretchRatio,
	                              bool reversed, bool mayCreate, bool* created);
	void deleteCache(SampleCache* cache);
	int32_t getFirstClusterIndexWithAudioData();
	int32_t getFirstClusterIndexWithNoAudioData();
	Error fillPercCache(TimeStretcher* timeStretcher, int32_t startPosSamples, int32_t endPosSamples,
	                    int32_t playDirection, int32_t maxNumSamplesToProcess);
	void percCacheClusterStolen(Cluster* cluster);
	void deletePercCache(bool beingDestructed = false);
	uint8_t* prepareToReadPercCache(int32_t pixellatedPos, int32_t playDirection, int32_t* earliestPixellatedPos,
	                                int32_t* latestPixellatedPos);
	bool getAveragesForCrossfade(int32_t* totals, int32_t startBytePos, int32_t crossfadeLengthSamples,
	                             int32_t playDirection, int32_t lengthToAverageEach);
	void convertDataOnAnyClustersIfNecessary();
	int32_t getMaxPeakFromZero();
	int32_t getFoundValueCentrePoint();
	int32_t getValueSpan();
	void finalizeAfterLoad(uint32_t fileSize);

	inline void convertOneData(int32_t* value) {
		// Floating point
		if (rawDataFormat == RAW_DATA_FLOAT)
			convertFloatToIntAtMemoryLocation((uint32_t*)value);

		// Or endianness swap
		else if (rawDataFormat == RAW_DATA_ENDIANNESS_WRONG_32) {
			*value = swapEndianness32(*value);
		}

		else if (rawDataFormat == RAW_DATA_ENDIANNESS_WRONG_16) {
			*value = swapEndianness2x16(*value);
		}

		else if (rawDataFormat == RAW_DATA_UNSIGNED_8) {
			*value ^= 0x80808080;
		}
	}

	String tempFilePathForRecording;
	uint8_t byteDepth;
	uint32_t sampleRate;
	uint32_t audioDataStartPosBytes; // That is, the offset from the start of the WAV file
	uint64_t audioDataLengthBytes;
	uint32_t bitMask;
	bool audioStartDetected;

	uint64_t lengthInSamples;

	// These two are for holding a value loaded from file
	uint32_t fileLoopStartSamples; // And during recording, this one also stores the final value once known
	uint32_t fileLoopEndSamples;

	float midiNoteFromFile; // -1 means none

	uint8_t rawDataFormat;

	bool unloadable; // Only gets set to true if user has re-inserted the card and the sample appears to have been
	                 // deleted / moved / modified
	bool unplayable;
	bool partOfFolderBeingLoaded;
	bool fileExplicitlySpecifiesSelfAsWaveTable;

#if SAMPLE_DO_LOCKS
	bool lock;
#endif

	float midiNote; // -999 means not worked out yet. -1000 means error working out

	// int32_t valueSpan; // -2147483648 means both these are uninitialized
	int32_t minValueFound;
	int32_t maxValueFound;

	OrderedResizeableArrayWithMultiWordKey caches;

	uint8_t* percCacheMemory[2];                          // One for each play-direction: 0=forwards; 1=reversed
	OrderedResizeableArrayWith32bitKey percCacheZones[2]; // One for each play-direction: 0=forwards; 1=reversed

	Cluster** percCacheClusters[2]; // One for each play-direction: 0=forwards; 1=reversed
	int32_t numPercCacheClusters;

	int32_t beginningOffsetForPitchDetection;
	bool beginningOffsetForPitchDetectionFound;

	uint32_t waveTableCycleSize; // In case this later gets used for a WaveTable

	SampleClusterArray clusters;

	AbsValueFollower envelopeFollower{};

protected:
#if ALPHA_OR_BETA_VERSION
	void numReasonsDecreasedToZero(char const* errorCode);
#endif

private:
	int32_t investigateFundamentalPitch(int32_t fundamentalIndexProvided, int32_t tableSize, int32_t* heightTable,
	                                    uint64_t* sumTable, float* floatIndexTable, float* getFreq,
	                                    int32_t numDoublings, bool doPrimeTest);
};
