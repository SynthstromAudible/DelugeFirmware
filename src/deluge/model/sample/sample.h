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
#include "model/sample/sample_cluster.h"
#include "model/sample/sample_perc_cache_zone.h"
#include "storage/audio/audio_file.h"
#include "util/containers.h"
#include "util/fixedpoint.h"
#include "util/functions.h"
#include <array>
#include <bit>
#include <cstdint>

#define SAMPLE_DO_LOCKS (ALPHA_OR_BETA_VERSION)

enum class RawDataFormat : uint8_t {
	NATIVE = 0,
	FLOAT = 1,
	UNSIGNED_8 = 2,
	ENDIANNESS_WRONG_16 = 3,
	ENDIANNESS_WRONG_24 = 4,
	ENDIANNESS_WRONG_32 = 5,
};

const float MIDI_NOTE_UNSET = (-999);
const float MIDI_NOTE_ERROR = (-1000);
class LoadedSamplePosReason;
class SampleCache;

/// One entry in Sample::caches, keyed by the playback parameters the cache was rendered with
struct SampleCacheElement {
	int32_t phaseIncrement;
	int32_t timeStretchRatio;
	int32_t skipSamplesAtStart;
	bool reversed;
	SampleCache* cache;

	[[nodiscard]] std::array<uint32_t, 4> key() const {
		return {static_cast<uint32_t>(phaseIncrement), static_cast<uint32_t>(timeStretchRatio),
		        static_cast<uint32_t>(skipSamplesAtStart), static_cast<uint32_t>(reversed)};
	}
};
class MultisampleRange;
class TimeStretcher;
class SampleHolder;

class Sample final : public AudioFile {
public:
	Sample();
	~Sample() override;

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
	/// Lazily define this Sample's resource-manager Asset (its SAMPLE clusters are the Asset's
	/// Chunks: materialize = readClusterData, on_evict = drop + ~Cluster). Returns the asset id. The
	/// manager is the sole SDRAM evictor, so this never returns NO_ASSET — a missing manager / full
	/// asset table is fatal (FREEZE), no legacy fallback.
	uint32_t ensureResourceAsset();
	int32_t getMaxPeakFromZero();
	int32_t getFoundValueCentrePoint();
	int32_t getValueSpan();
	void finalizeAfterLoad(uint32_t fileSize) override;

	// Floating point
	[[nodiscard]] q31_t convertToNative(float value) const { return q31_from_float(value); }

	[[nodiscard]] q31_t convertToNative(int32_t value) const {
		switch (rawDataFormat) {
		case RawDataFormat::FLOAT:
			return q31_from_float(std::bit_cast<float>(value));

		case RawDataFormat::ENDIANNESS_WRONG_32: // Or endianness swap
			return swapEndianness32(value);

		case RawDataFormat::ENDIANNESS_WRONG_16:
			return swapEndianness2x16(value);

		case RawDataFormat::UNSIGNED_8:
			return value ^ 0x80808080;

		case RawDataFormat::ENDIANNESS_WRONG_24:
			// handled by caller
			[[fallthrough]];

		case RawDataFormat::NATIVE:
			// nothing to be done
			break;
		}
		return value;
	}

	std::string tempFilePathForRecording{};
	uint8_t byteDepth{0};
	uint32_t sampleRate{44100};
	uint32_t audioDataStartPosBytes; // That is, the offset from the start of the WAV file
	uint64_t audioDataLengthBytes;
	uint32_t bitMask{0};
	bool audioStartDetected;

	uint64_t lengthInSamples;

	// These two are for holding a value loaded from file
	uint32_t fileLoopStartSamples; // And during recording, this one also stores the final value once known
	uint32_t fileLoopEndSamples;

	float midiNoteFromFile; // -1 means none

	RawDataFormat rawDataFormat;

	bool unloadable{false}; // Only gets set to true if user has re-inserted the card and the sample appears to have
	                        // been deleted / moved / modified
	bool unplayable{false};
	bool partOfFolderBeingLoaded;
	bool fileExplicitlySpecifiesSelfAsWaveTable{false};

#if SAMPLE_DO_LOCKS
	bool lock;
#endif

	float midiNote; // -999 means not worked out yet. -1000 means error working out

	// int32_t valueSpan; // -2147483648 means both these are uninitialized
	int32_t minValueFound;
	int32_t maxValueFound;

	deluge::fast_vector<SampleCacheElement> caches{}; // Sorted ascending by SampleCacheElement::key()

	uint8_t* percCacheMemory[2]{nullptr, nullptr}; // One for each play-direction: 0=forwards; 1=reversed
	// One for each play-direction: 0=forwards; 1=reversed. Sorted ascending by startPos. On the external
	// (non-stealable) region so that growing a zone list can never steal this sample's own perc-cache clusters,
	// which would re-enter these arrays mid-modification.
	deluge::vector<SamplePercCacheZone> percCacheZones[2]{};

	Cluster** percCacheClusters[2]{nullptr, nullptr}; // One for each play-direction: 0=forwards; 1=reversed
	int32_t numPercCacheClusters{};
	// Resource-manager Asset id per play-direction for the perc-cache clusters (construct-only,
	// leased-while-nearby by the TimeStretcher). DELUGE_RESOURCE_NO_ASSET (0xFFFFFFFF) = legacy.
	uint32_t percCacheAssetId[2]{0xFFFFFFFFu, 0xFFFFFFFFu};

	int32_t beginningOffsetForPitchDetection;
	bool beginningOffsetForPitchDetectionFound;

	uint32_t waveTableCycleSize{0}; // In case this later gets used for a WaveTable

	deluge::fast_vector<SampleCluster> clusters{};

	// Resource-manager Asset id for this Sample's SAMPLE clusters (DELUGE_RESOURCE_NO_ASSET
	// until defined on first stream). Owns raw-cluster residency once routed through the
	// manager; released in ~Sample. 0xFFFFFFFF == DELUGE_RESOURCE_NO_ASSET (kept as a literal
	// here so the widely-included header needn't pull in deluge_resource.h).
	uint32_t resourceAssetId{0xFFFFFFFFu};

protected:
	// Project-relevance hooks (the object's hard-lease 0↔1 transitions): toggle the soft-reference on
	// this sample's resource assets so current-song data is kept resident over no-song data under
	// memory pressure. Present in all builds (the bug-check inside DecreasedToZero is ALPHA-only).
	void numReasonsIncreasedFromZero() override;
	void numReasonsDecreasedToZero(char const* errorCode) override;

private:
	// Soft-reference (on=true) / un-reference (on=false) this sample's cluster asset + every derived
	// cache (repitch caches + the two perc-cache directions) that is currently defined.
	void applyProjectReference(bool on);

protected:
private:
	int32_t investigateFundamentalPitch(int32_t fundamentalIndexProvided, int32_t tableSize, int32_t* heightTable,
	                                    uint64_t* sumTable, float* floatIndexTable, float* getFreq,
	                                    int32_t numDoublings, bool doPrimeTest);
};
