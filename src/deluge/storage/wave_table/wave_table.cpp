/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "storage/wave_table/wave_table.h"
#include "NE10.h"
#include "arm_neon_shim.h"
#include "definitions_cxx.hpp"
#include "dsp/fft/fft_config_manager.h"
#include "dsp/interpolate/interpolate.h"
#include "io/debug/fx_benchmark.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "processing/engines/audio_engine.h"
#include "processing/render_wave.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "storage/storage_manager.h"
#include "storage/wave_table/wave_table_reader.h"
#include "util/fixedpoint.h"
#include <new>

extern int32_t oscSyncRenderingBuffer[];

WaveTableBand::~WaveTableBand() {
	if (data) { // It might be NULL if that BandData was just "stolen".
		data->~WaveTableBandData();
		delugeDealloc(data);
	}
}

WaveTable::WaveTable() : bands(sizeof(WaveTableBand)), AudioFile(AudioFileType::WAVETABLE) {
}

WaveTable::~WaveTable() {
	deleteAllBandsAndData();
}

void WaveTable::deleteAllBandsAndData() {
	for (int32_t b = 0; b < bands.getNumElements(); b++) {
		WaveTableBand* band = (WaveTableBand*)bands.getElementAddress(b);
		band->~WaveTableBand();
	}
	bands.empty();
}

void WaveTable::bandDataBeingStolen(WaveTableBandData* bandData) {
	for (int32_t b = 0; b < bands.getNumElements(); b++) {
		WaveTableBand* band = (WaveTableBand*)bands.getElementAddress(b);
		if (band->data == bandData) {
			band->data = nullptr;
			break;
		}
	}
}

#define numBitsInInput 16
#define numBitsInTableSize 8
#define lshiftAmount (16 + numBitsInTableSize - numBitsInInput)

// This "should" handle inputs up to size 65536. Actually probably a bit bigger. Maybe even 131071?
void dft_r2c(ne10_fft_cpx_int32_t* __restrict__ out, int32_t const* __restrict__ in, uint32_t N) {

	// uint16_t startTime = *TCNT[TIMER_SYSTEM_SLOW];

	// For each input value...
	for (uint32_t i = 0; i <= (N >> 1); i++) {
		int32_t sReal = 0;
		int32_t sIm = 0;

		uint32_t angleIncrement = -((i << numBitsInInput) / N);
		uint32_t angle = 0;

		int32_t const* __restrict__ realReadPos = in;
		int32_t const* const stopAt = realReadPos + N;

		// We compare that input value separately to each input value again...
		do {
			int32_t whichValueSine = angle >> (numBitsInInput - numBitsInTableSize);
			whichValueSine &= (1 << numBitsInTableSize) - 1;
			int32_t sineValue1 = sineWaveSmall[whichValueSine];
			int32_t sineValue2 = sineWaveSmall[whichValueSine + 1];

			int32_t whichValueCos = whichValueSine + (1 << (numBitsInTableSize - 2));
			whichValueCos &= (1 << numBitsInTableSize) - 1;
			int32_t cosValue1 = sineWaveSmall[whichValueCos];
			int32_t cosValue2 = sineWaveSmall[whichValueCos + 1];

			int32_t inputValueReal = *realReadPos;

			uint32_t shifted = angle << lshiftAmount;
			int32_t strength2 = shifted & 65535;

			int32_t sineDiff = sineValue2 - sineValue1;
			int32_t cosDiff = cosValue2 - cosValue1;
			int32_t sineValue = (sineValue1 << 16) + sineDiff * strength2;
			int32_t cosValue = (cosValue1 << 16) + cosDiff * strength2;

			sReal = multiply_accumulate_32x32_rshift32_rounded(
			    sReal, inputValueReal, cosValue); // We lose one order of magnitude here, which is made up for below.
			sIm = multiply_accumulate_32x32_rshift32_rounded(sIm, inputValueReal, sineValue);

			angle += angleIncrement;
			realReadPos++;
		} while (realReadPos < stopAt);

		out[i].r = sReal << 1; // Here we make up for the order of magnitude that was lost above.
		out[i].i = sIm << 1;
	}

	/*
	uint16_t endTime = *TCNT[TIMER_SYSTEM_SLOW];
	uint16_t duration = endTime - startTime;
	D_PRINTLN("dft duration:  %d", duration);
	*/
}

#define NUM_OCTAVES_BETWEEN_WAVETABLE_BANDS 1
#define MAX_POSSIBLE_NUM_BANDS ((FFT_CONFIG_MAX_MAGNITUDE - 1) >> (NUM_OCTAVES_BETWEEN_WAVETABLE_BANDS - 1))

#define WAVETABLE_ALLOW_INTERNAL_MEMORY 0

#define WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE 7 // That's in samples - it'll be twice as many bytes.
#define SHOULD_DISCARD_WAVETABLE_DATA_WITH_INSUFFICIENT_HF_CONTENT 0

Error WaveTable::setup(Sample* sample, int32_t rawFileCycleSize, uint32_t audioDataStartPosBytes,
                       uint32_t audioDataLengthBytes, int32_t byteDepth, RawDataFormat rawDataFormat,
                       WaveTableReader* reader) {
	AudioEngine::logAction("WaveTable::setup");

	uint32_t originalSampleLengthInSamples;

	if (sample) {
		filePath.set(&sample->filePath);
		loadedFromAlternatePath.set(&sample->loadedFromAlternatePath);

		rawFileCycleSize = sample->waveTableCycleSize;
		numChannels = sample->numChannels;
		byteDepth = sample->byteDepth;

		originalSampleLengthInSamples = sample->lengthInSamples;
		audioDataStartPosBytes = sample->audioDataStartPosBytes;
	}
	else {
		originalSampleLengthInSamples = audioDataLengthBytes / (uint8_t)(byteDepth * numChannels);
	}

	if (rawFileCycleSize > originalSampleLengthInSamples) {
		rawFileCycleSize = originalSampleLengthInSamples;
	}

	// We do some checking here for conditions which make it completely impossible for a file to be a WaveTable.
	// As opposed for checks before this function is called, for softer conditions which merely determine that this file
	// might not be *intended* as a WaveTable, but the user have have overridden.
	if (rawFileCycleSize < kWavetableMinCycleSize) {
		return Error::FILE_NOT_LOADABLE_AS_WAVETABLE; // See comment on minimum FFT size - click thru to
		                                              // WAVETABLE_MIN_CYCLE_SIZE.
	}

	int32_t initialBandCycleMagnitude = getMagnitude(rawFileCycleSize);
	bool rawFileCycleSizeIsAPowerOfTwo = (rawFileCycleSize == (1 << initialBandCycleMagnitude));

	// If cycle size isn't a power of two...
	if (!rawFileCycleSizeIsAPowerOfTwo) {

		// This is allowed if we're doing just single-cycle.
		if (originalSampleLengthInSamples < (rawFileCycleSize << 1)
		    && originalSampleLengthInSamples >= rawFileCycleSize) {
			numCycles = 1;

			// In order to represent all frequency information in the cycle once we render it out to a power-of-two
			// size, well we'll need to go slightly bigger rather than slightly smaller. Also, the slightly bigger size
			// is needed so that sufficient memory is allocated to do all the calculations in.
			initialBandCycleMagnitude++;
		}

		// Otherwise, if multiple cycles (wavetable), not allowed - it'd take too long to process, and no one has
		// wavetables of weird sizes anyway.
		else {
			return Error::FILE_NOT_LOADABLE_AS_WAVETABLE;
		}
	}

	// Or, normal case where cycle size *is* a power of two.
	else {
		numCycles =
		    originalSampleLengthInSamples >> initialBandCycleMagnitude; // Will round down, which is what we want.
		if (numCycles < 1) {
			return Error::FILE_NOT_LOADABLE_AS_WAVETABLE; // Surely can't happen, becacuse of the checks / limiting
			                                              // we've done above?
		}
	}

tryGettingFFTConfig:
	AudioEngine::logAction("Getting fft config");
	ne10_fft_r2c_cfg_int32_t fftCFGForInitialBand = FFTConfigManager::getConfig(initialBandCycleMagnitude);
	AudioEngine::logAction("Got fft config");

	// If that returned NULL, normally we can just opt to not do FFTs and have just 1 band.
	// But in the case where the original wasn't a power-of-two size, we're gonna have to do FFTs (as well as one
	// DFT)...
	if (!fftCFGForInitialBand && !rawFileCycleSizeIsAPowerOfTwo) {

		// Actually screw it, this is so rare, it's easier just to make a rule that for non-power-of-two we *have* to
		// render it out to that bigger size. Cos if we allow a smaller initial band, we don't have enough space in it
		// for the dummy writing we do during load, which could be avoided, but yeah this is so rare.
		return Error::INSUFFICIENT_RAM;

		/*
		// So, see if there's a smaller FFT config we can still get.
		if (initialBandCycleMagnitude <= 3) return Error::INSUFFICIENT_RAM; // But, can't go smaller than size 8 (2^3) -
		see comment on min size below. initialBandCycleMagnitude--; goto tryGettingFFTConfig;
		*/
	}

	int32_t initialBandCycleSizeNoDuplicates =
	    1 << initialBandCycleMagnitude; // This will usually be the same as rawFileCycleSize, but not when that's not a
	                                    // power of two.

	Error error;

	// numBands is set such that the "smallest" band will have 8 samples per cycle. Not 4 - because NE10 can't do FFTs
	// that small unless we enable its additional C code, which would take up program size for little advantage.
	{
		int32_t numBands =
		    fftCFGForInitialBand ? ((initialBandCycleMagnitude - 2) >> (NUM_OCTAVES_BETWEEN_WAVETABLE_BANDS - 1)) : 1;

		// Don't refer to numBands after this! (Why? Because we might end up using less after all?)
		error = bands.insertAtIndex(0, numBands);
	}
	if (error != Error::NONE) {
gotError:
		return error;
	}

	if (false) {
gotError2:
		deleteAllBandsAndData();
		goto gotError;
	}

	numCyclesMagnitude = getMagnitude(numCycles);

	AudioEngine::logAction("just started wavetable");
	AudioEngine::routineWithClusterLoading(); // TODO: the routine calls in this function might be more than needed - I
	                                          // didn't profile very closely.
	AudioEngine::logAction("about to set up bands");

	for (int32_t b = 0; b < bands.getNumElements(); b++) {
		int32_t cycleSizeNoDuplicates = initialBandCycleSizeNoDuplicates >> (b * NUM_OCTAVES_BETWEEN_WAVETABLE_BANDS);

		int32_t bandSizeSamplesWithDuplicates =
		    numCycles * (cycleSizeNoDuplicates + WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE);
		int32_t bandSizeBytesWithDuplicates = bandSizeSamplesWithDuplicates << 1; // All bands contain just 16-bit data.
		// Ironically we'll even do that if the source file was just 8-bit, but that's really uncommon.
		void* bandDataMemory =
		    GeneralMemoryAllocator::get().allocStealable(bandSizeBytesWithDuplicates + sizeof(WaveTableBandData));
		if (!bandDataMemory) {
			error = Error::INSUFFICIENT_RAM;
			// All bands from this one onwards still have undefined data, so gotta get rid of them before anything else
			// tries to do anything with them.
			bands.deleteAtIndex(b, bands.getNumElements() - b);
			goto gotError2;
		}

		WaveTableBand* band = (WaveTableBand*)bands.getElementAddress(b);
		band->data = new (bandDataMemory) WaveTableBandData(this);

		band->dataAccessAddress = (int16_t*)(band->data + 1);

		band->fromCycleNumber = 0;
		band->toCycleNumber = 0;
		band->cycleSizeNoDuplicates = cycleSizeNoDuplicates;
		band->cycleSizeMagnitude = initialBandCycleMagnitude - b * NUM_OCTAVES_BETWEEN_WAVETABLE_BANDS;
		band->maxPhaseIncrement = (uint32_t)(0xFFFFFFFF >> (band->cycleSizeMagnitude)) *
#if NUM_OCTAVES_BETWEEN_WAVETABLE_BANDS == 2
		                          2;
#else
		                          1.25;
#endif
	}

	AudioEngine::logAction("bands set up");
	AudioEngine::routineWithClusterLoading();
	AudioEngine::logAction("allocating working memory");

	// Create the temporary memory where we'll store the 32-bit int32_t version of the current cycle being read, to
	// perform the FFT on. This will also be used for outputting the time-domain cycle data back out of the inverse FFT,
	// so ensure it's big enough for that if our biggest band is bigger than the cycle size in the file (i.e. if that's
	// not a power-of-two).
	int32_t currentCycleMemorySize = std::max(rawFileCycleSize, initialBandCycleSizeNoDuplicates);
	// Internal RAM is good, and it's only temporary
	int32_t* __restrict__ currentCycleInt32 =
	    (int32_t*)GeneralMemoryAllocator::get().allocMaxSpeed(currentCycleMemorySize * sizeof(int32_t));
	if (!currentCycleInt32) {
		error = Error::INSUFFICIENT_RAM;
		goto gotError2;
	}

	// And temporary FFT output (frequency domain) buffer. The same sizing considerations as above are needed, and we
	// use that same decision here
	// - except for frequency-domain complex numbers, we only need to store half of it, plus one.
	ne10_fft_cpx_int32_t* __restrict__ frequencyDomainData =
	    (ne10_fft_cpx_int32_t*)GeneralMemoryAllocator::get().allocMaxSpeed(((currentCycleMemorySize >> 1) + 1)
	                                                                       * sizeof(ne10_fft_cpx_int32_t));
	if (!frequencyDomainData) {
		error = Error::INSUFFICIENT_RAM;
gotError4:
		delugeDealloc(currentCycleInt32);
		goto gotError2;
	}

	if (false) {
gotError5:
		delugeDealloc(frequencyDomainData);
		goto gotError4;
	}

	AudioEngine::logAction("working memory allocated");
	AudioEngine::routineWithClusterLoading();
	AudioEngine::logAction("finalizing vars");

	WaveTableBand* initialBand = (WaveTableBand*)bands.getElementAddress(0);
	// Even for non-power-of-two cycle size files, we'll still write the data in here even though it's not needed and
	// will be overwritten, just since we're using the same code as for power-of-two.
	int16_t* __restrict__ initialBandWritePos = initialBand->dataAccessAddress;

	int32_t clusterIndex = audioDataStartPosBytes >> Cluster::size_magnitude;
	int32_t byteIndexWithinCluster = audioDataStartPosBytes & (Cluster::size - 1);

	if (!sample) {
		reader->jumpForwardToBytePos(audioDataStartPosBytes); // In case reader wasn't quite up to here yet! Can happen
		                                                      // in AIFF files, with their "offset"
	}

	char const* sourceBuffer =
	    smDeserializer.fileClusterBuffer; // This will get changed if we're converting an existing Sample in memory.
	uint32_t bytesOverlappingFromLastCluster;

#define MAGNITUDE_REDUCTION_FOR_FFT 12

	uint32_t bitMask = 0xFFFFFFFF << ((4 - byteDepth) * 8);

	Cluster* cluster = nullptr;
	int32_t clusterIndexCurrentlyLoaded = -1; // Initially, none is loaded yet.

	uint32_t startedBandsYet = 0;

	bool swappingEndianness =
	    (rawDataFormat == RawDataFormat::ENDIANNESS_WRONG_32 || rawDataFormat == RawDataFormat::ENDIANNESS_WRONG_24
	     || rawDataFormat == RawDataFormat::ENDIANNESS_WRONG_16);

	bool needToMisalignData = (rawDataFormat == RawDataFormat::NATIVE || rawDataFormat == RawDataFormat::UNSIGNED_8);

	// For each wave cycle...
	for (int32_t cycleIndex = 0; cycleIndex < numCycles; cycleIndex++) {

		AudioEngine::logAction("new cycle began");
		AudioEngine::routineWithClusterLoading();
		AudioEngine::logAction("new cycle beginning");

		int16_t* nativeBandCycleStartPos =
		    initialBandWritePos; // Store this so we can refer to it at the end of the cycle, for duplicating values

		int32_t* cycleBufferDestination = currentCycleInt32;
		int32_t sourceBytesLeftToCopyThisCycle = rawFileCycleSize * byteDepth;

		// Read some clusters of audio data - either from existing Sample in memory, or from file.
		do {

			// If converting an existing Sample from memory into this WaveTable...
			if (sample) {

				// If we need to load a new Cluster now (which might not be the case if we've just switched into a new
				// Cycle which starts still within the same Cluster)...
				if (clusterIndex != clusterIndexCurrentlyLoaded) {

					// First, unload the old Cluster if there was one
					if (cluster) {
						audioFileManager.removeReasonFromCluster(*cluster, "E385");
					}

					cluster = sample->clusters.getElement(clusterIndex)
					              ->getCluster(sample, clusterIndex, CLUSTER_LOAD_IMMEDIATELY, 0, &error);
					if (!cluster) {
						goto gotError5;
					}

					clusterIndexCurrentlyLoaded = clusterIndex;
					sourceBuffer = cluster->data;
				}
			}

			// Or, if reading data afresh from a file...
			else {
				if (byteIndexWithinCluster < 0) {
					reader->byteIndexWithinCluster -=
					    byteIndexWithinCluster; // Will put it right at the start of the next cluster, to force it to
					                            // read from card
				}
				error = reader->advanceClustersIfNecessary();
				if (error != Error::NONE) {
					goto gotError5;
				}
			}

			// Macro setup for below
#define CONVERT_AND_STORE_SAMPLE                                                                                       \
	{                                                                                                                  \
		if (rawDataFormat == RawDataFormat::FLOAT) {                                                                   \
			value32 = q31_from_float(std::bit_cast<float>(value32));                                                   \
		}                                                                                                              \
		else {                                                                                                         \
			if (swappingEndianness) {                                                                                  \
				value32 = swapEndianness32(value32);                                                                   \
			}                                                                                                          \
			value32 &= bitMask;                                                                                        \
			if (rawDataFormat == RawDataFormat::UNSIGNED_8) {                                                          \
				value32 += (1 << 31);                                                                                  \
			}                                                                                                          \
		}                                                                                                              \
		*(cycleBufferDestination++) =                                                                                  \
		    value32 >> MAGNITUDE_REDUCTION_FOR_FFT; /* Store full 32-bit value in cycle buffer (reduced in magnitude   \
		                                               for FFT). */                                                    \
		*(initialBandWritePos++) = value32 >> 16;   /* Bands only store 16-bit data. */                                \
	}

			// If previous Cluster had overlapping sample at end...
			if (byteIndexWithinCluster < 0) {

				int32_t byteIndexWithinClusterMisaligned = byteIndexWithinCluster;
				if (needToMisalignData) {
					byteIndexWithinClusterMisaligned = byteIndexWithinClusterMisaligned - 4 + byteDepth;
				}

				char const* source =
				    sourceBuffer
				    + byteIndexWithinClusterMisaligned; // Misalign pointer for reading start of this buffer, for byte
				                                        // depth and also to correct for the overlap.

				bytesOverlappingFromLastCluster &=
				    ((uint32_t)0xFFFFFFFF >> ((4 + byteIndexWithinClusterMisaligned)
				                              * 8)); // Remember, this data was obtained with the pointer already
				                                     // correctly misaligned for byte depth.
				uint32_t bytesOverlappingThisCluster =
				    *(uint32_t*)source & ((uint32_t)0xFFFFFFFF << (-byteIndexWithinClusterMisaligned * 8));

				int32_t value32 = bytesOverlappingThisCluster | bytesOverlappingFromLastCluster;

				CONVERT_AND_STORE_SAMPLE;

				if (!sample) {
					reader->byteIndexWithinCluster +=
					    byteDepth + byteIndexWithinCluster; // +byteIndexWithinCluster because we already moved forward
					                                        // an extra -byteIndexWithinCluster, above.
				}

				sourceBytesLeftToCopyThisCycle -= byteDepth;
				byteIndexWithinCluster += byteDepth;

				// If now finished this cycle, get out.
				if (sourceBytesLeftToCopyThisCycle <= 0) {
					break;
				}
			}

			const char* source = &sourceBuffer[byteIndexWithinCluster];
			const char* sourceStopAt = &sourceBuffer[Cluster::size];

			// Stop before we get to the final sample that overlaps the end of the cluster, if that happens.
			sourceStopAt = sourceStopAt - byteDepth + 1;

			int32_t numSourceBytesLookingAtThisCluster = (uint32_t)sourceStopAt - (uint32_t)source;
			if (numSourceBytesLookingAtThisCluster > sourceBytesLeftToCopyThisCycle) {
				sourceStopAt = source + sourceBytesLeftToCopyThisCycle;
			}

			const int16_t* const bandDestinationStartedAt = initialBandWritePos;

			if (needToMisalignData) {
				// Misalign source pointers to allow us to read values as 32-bit
				source = source - 4 + byteDepth;
				sourceStopAt = sourceStopAt - 4 + byteDepth;
			}

			// Read all the data we can before reaching either the end of the cycle, or the end of the file cluster
			while (source < sourceStopAt) {
				int32_t value32 = *(int32_t*)source;
				CONVERT_AND_STORE_SAMPLE;
				source += byteDepth;
			}

			int32_t bytesJustWritten = (uint32_t)initialBandWritePos - (uint32_t)bandDestinationStartedAt;
			int32_t samplesJustCopied = bytesJustWritten >> 1;
			int32_t sourceBytesJustRead = samplesJustCopied * byteDepth;
			sourceBytesLeftToCopyThisCycle -= sourceBytesJustRead;
			byteIndexWithinCluster += sourceBytesJustRead;

			if (!sample) {
				reader->byteIndexWithinCluster += sourceBytesJustRead; // Keep this up to date
			}

			// If we're less than one sample from the end of the cluster...
			if (byteIndexWithinCluster > Cluster::size - byteDepth) {
				bytesOverlappingFromLastCluster = *(uint32_t*)source;

				// A negative value here indicates that the start of the next sample is within the current cluster
				byteIndexWithinCluster -= Cluster::size;

				clusterIndex++;
			}
		} while (sourceBytesLeftToCopyThisCycle > 0);

		AudioEngine::logAction("cycle been read");
		AudioEngine::routineWithClusterLoading();
		AudioEngine::logAction("analyzing cycle");

		// Ok, we've finished reading one wave cycle (which potentially spanned multiple file clusters).
		D_PRINTLN("\nCycle:  %d", cycleIndex);

		// If it wasn't a power-of-two size, we have to do a DFT even just to get the first band's useable time-domain
		// data.
		if (!rawFileCycleSizeIsAPowerOfTwo) {
			AudioEngine::logAction("dft start");
			dft_r2c(frequencyDomainData, currentCycleInt32, rawFileCycleSize);
			// That function does indeed take ages, as shown by the AUDIO_LOG. But somehow, I'm not hearing voices
			// drop... how?
			AudioEngine::logAction("dft done");

			// Because the raw file cycle size was a bit smaller than the initial band cycle size that we're going to be
			// expanding it out to, we're going to be feeding a slightly wider bit of (freq domain) data back into the
			// FFT to get time domain data (below), so have to clear the extra memory part to zeroes now.
			for (int32_t i = (rawFileCycleSize >> 1) + 1; i <= (initialBandCycleSizeNoDuplicates >> 1); i++) {
				frequencyDomainData[i].r = 0;
				frequencyDomainData[i].i = 0;
			}

			// We need to move the band write pos forward for the next cycle.
			// Remember, for non-power-of-two raw files, this data just gets overwritten later anyway, and we only write
			// it in the first place just because the same code does this for power-of-two. But we need to at least move
			// the pointer along so we don't overwrite anything we shouldn't. Adctually, because non-power-of-two raw
			// files are only allowed to be single-cycle, this isn't strictly necessary, but let's do it for
			// completeness and in case I ever change that rule.
			initialBandWritePos = &initialBand->dataAccessAddress[(initialBandCycleSizeNoDuplicates
			                                                       + WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE)
			                                                      * (cycleIndex + 1)];
		}

		// Or if it *was* a power-of-two size, we can just do an easier FFT. (And also further down, we won't have to
		// re-render time-domain data for this initial band.)
		else {

			// Write the duplicate values for this initial band - do this now, since we already have the final, useable
			// time-domain data for it.
			for (int32_t i = 0; i < WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE; i++) {
				*(initialBandWritePos++) = *(nativeBandCycleStartPos++);
			}

			// In fact, if there are no further bands (usually won't happen unless out of RAM), we can get out as
			// there's nothing more to do.
			if (bands.getNumElements() <= 1) {
				continue;
			}

			// Perform the FFT, to frequency domain
			ne10_fft_r2c_1d_int32_neon(frequencyDomainData, currentCycleInt32, fftCFGForInitialBand, false);
		}

		AudioEngine::logAction("got freq domain data");
		AudioEngine::routineWithClusterLoading();
		AudioEngine::logAction("scanning freq data");

		int32_t biggestValue = 0;
		int32_t biggestValueIndex;
		int32_t highestSignificantHarmonicIndex = 0;

		// Scan the frequency domain of this native cycle, as loaded from the raw file (so not necessary power-of-two
		// size), looking at harmonics
		for (int32_t i = 1; i <= (rawFileCycleSize >> 1); i++) {
			int32_t thisValue = (fastPythag(frequencyDomainData[i].r, frequencyDomainData[i].i) >> 6)
			                    * i; // Slope the high frequencies up, 6dB per octave, for fair comparison.
			if (thisValue > biggestValue) {
				biggestValue = thisValue;
				biggestValueIndex = i;
			}

			// If this harmonic's magnitude is still not too much quieter than the biggest one, we'd better take note
			// that it exists.
			if (thisValue >= (biggestValue >> 5)) {
				highestSignificantHarmonicIndex = i;
			}
		}

		WaveTableBand* band = initialBand;
		int32_t b = 0;

		// If there's no harmonic content high-enough frequency to warrant this band existing...
		if (SHOULD_DISCARD_WAVETABLE_DATA_WITH_INSUFFICIENT_HF_CONTENT
		    && highestSignificantHarmonicIndex <= (rawFileCycleSize >> (1 + NUM_OCTAVES_BETWEEN_WAVETABLE_BANDS))) {

			// If haven't "started" this band yet, update record to show it starts from this cycle - which it might
			// because the next cycle might be needed, and we have a rule that we start one cycle early.
			if (!(startedBandsYet & 1)) {
				band->fromCycleNumber = cycleIndex;
			}
		}

		// Or if there *is* enough high-freq content, take note that it's all still happening.
		else {
			startedBandsYet |= 1;
			band->toCycleNumber = std::min(cycleIndex + 2, numCycles);
		}

		// If raw file wasn't power-of-two cycle size, then even this initial band needs the same treatment as the
		// higher ones - we have to render its time-domain data by doing an FFT from the freq-domain.
		if (!rawFileCycleSizeIsAPowerOfTwo) {
			goto transformBandToTimeDomain;
		}

		// Otherwise, if we're still here, we'll only need to render time-domain data for bands other than the initial
		// one.

		// For each band "higher" up (smaller cycle; less high harmonics), do all the stuff for it - for this cycle.
		for (b = 1; b < bands.getNumElements(); b++) {

			{
				band = (WaveTableBand*)bands.getElementAddress(b);

				// If we've already chopped off the previous highest significant harmonic, search for a new one.
				if (highestSignificantHarmonicIndex > (band->cycleSizeNoDuplicates >> 1)) {
					highestSignificantHarmonicIndex = 0;

					for (int32_t i = (band->cycleSizeNoDuplicates >> 1); i >= 1; i--) {
						int32_t pythagValue = fastPythag(frequencyDomainData[i].r, frequencyDomainData[i].i);
						int32_t thisValue = (pythagValue >> 6)
						                    * i; // Slope the high frequencies up, 6dB per octave, for fair comparison.

						// If this harmonic's magnitude is still not too much quieter than the biggest one, we'd better
						// take note that it exists.
						if (thisValue >= (biggestValue >> 5)) {
							highestSignificantHarmonicIndex = i;
							break;
						}
					}
				}

				// If there's no harmonic content high-enough frequency to warrant this band existing...
				if (SHOULD_DISCARD_WAVETABLE_DATA_WITH_INSUFFICIENT_HF_CONTENT
				    && highestSignificantHarmonicIndex
				           <= (band->cycleSizeNoDuplicates >> (1 + NUM_OCTAVES_BETWEEN_WAVETABLE_BANDS))) {

					// If haven't "started" this band yet, update record to show it starts from this cycle - which it
					// might because the next cycle might be needed, and we have a rule that we start one cycle early.
					if (!(startedBandsYet & (1 << b))) {
						band->fromCycleNumber = cycleIndex;
					}
				}

				// Or if there *is* enough high-freq content...
				else {
					startedBandsYet |= (1 << b);
					band->toCycleNumber = std::min(cycleIndex + 2, numCycles);
				}

				// The highest frequency we can represent (the Nyquist freq) is about to lose all trace of its imaginary
				// component (fun fact - that's just what happens)
				// - so cheat a little and put that into the real component.
				ne10_fft_cpx_int32_t* nyquistFreq = &frequencyDomainData[(band->cycleSizeNoDuplicates >> 1)];
				int32_t pythagValue = fastPythag(nyquistFreq->r, nyquistFreq->i);
				if (nyquistFreq->r < 0) {
					pythagValue = -pythagValue;
				}
				nyquistFreq->r = pythagValue;
				nyquistFreq->i = 0;
			}

transformBandToTimeDomain:
			ne10_fft_r2c_cfg_int32_t fftCFGThisBand =
			    FFTConfigManager::getConfig(band->cycleSizeMagnitude); // Just gets the config, doesn't do the FFT.

			// If couldn't get FFT config, gotta delete this band
			if (!fftCFGThisBand) {
#if ALPHA_OR_BETA_VERSION
				if (!b) {
					FREEZE_WITH_ERROR("E390");
				}
#endif
				band->~WaveTableBand();
				bands.deleteAtIndex(b);
				b--;
				continue;
			}

			AudioEngine::logAction("started band");
			AudioEngine::routineWithClusterLoading();
			AudioEngine::logAction("doing FFT to time domain");

			// Do the FFT, putting the output, time-domain data back in the temp currentCycleInt32 buffer.
			ne10_fft_c2r_1d_int32_neon(currentCycleInt32, frequencyDomainData, fftCFGThisBand, false);

			int16_t* __restrict__ destination =
			    &band->dataAccessAddress[(band->cycleSizeNoDuplicates + WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE)
			                             * cycleIndex];

			// Copy 32-bit time domain data to final 16-bit destination
			for (int32_t i = 0; i < band->cycleSizeNoDuplicates; i++) {
				destination[i] = signed_saturate<32 - 16>(
				    currentCycleInt32[i]
				    >> (16 - MAGNITUDE_REDUCTION_FOR_FFT
				        + initialBandCycleMagnitude)); // This saturation is possibly a temporary solution...
			}

			// And copy the duplicate values again
			for (int32_t i = 0; i < WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE; i++) {
				destination[i + band->cycleSizeNoDuplicates] = destination[i];
			}
		}
	}

	AudioEngine::logAction("finished all cycles");
	AudioEngine::routineWithClusterLoading();
	AudioEngine::logAction("finalizing wavetable");

	D_PRINTLN("initial num bands:  %d", bands.getNumElements());

	// Ok, we've now processed all Cycles.

	// There could be a Cluster with a reason we still need to remove.
	if (cluster != nullptr) {
		audioFileManager.removeReasonFromCluster(*cluster, "E385");
	}

	if (numCycles > 1) {
		int32_t numCycleTransitions = numCycles - 1;

		numCycleTransitionsNextPowerOf2Magnitude = getMagnitudeOld(numCycleTransitions);
		numCycleTransitionsNextPowerOf2 = 1 << numCycleTransitionsNextPowerOf2Magnitude;

		waveIndexMultiplier = numCycleTransitions << (31 - numCycleTransitionsNextPowerOf2Magnitude);
	}

	// Dispose of temp memory
	delugeDealloc(currentCycleInt32);
	delugeDealloc(frequencyDomainData);

	// Printout stats
	D_PRINTLN("initial band size if all populated: %d",
	          numCycles * (initialBand->cycleSizeNoDuplicates + WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE) * 2);
	D_PRINTLN("initial band size after trimming: %d",
	          (initialBand->toCycleNumber - initialBand->fromCycleNumber)
	              * (initialBand->cycleSizeNoDuplicates + WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE) * 2);
	int32_t total = 0;
	for (int32_t b = 1; b < bands.getNumElements(); b++) {
		WaveTableBand* band = (WaveTableBand*)bands.getElementAddress(b);
		total += (band->toCycleNumber - band->fromCycleNumber)
		         * (band->cycleSizeNoDuplicates + WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE) * 2;
	}
	D_PRINTLN("other bands total size after trimming:  %d", total);

	// Dispose of bands that didn't end up getting used, or such portions of their memory.
	for (int32_t b = bands.getNumElements() - 1; b >= 0; b--) { // Traverse backwards because we might delete elements.
		WaveTableBand* band = (WaveTableBand*)bands.getElementAddress(b);

		// Delete whole band if no data needed
		if (band->fromCycleNumber >= band->toCycleNumber) {
			band->~WaveTableBand();
			bands.deleteAtIndex(b);
			D_PRINTLN("deleted whole band -  %d", b);
		}

		// Otherwise, can we shorten the memory?
		else {

			// Right-hand side
			if (band->toCycleNumber < numCycles) {
				if (!b) {
					D_PRINTLN("(band 0) ");
				}
				D_PRINTLN("deleting num cycles from right-hand side:  %d", numCycles - band->toCycleNumber);
				int32_t newSize = band->toCycleNumber
				                      * (band->cycleSizeNoDuplicates + WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE)
				                      * sizeof(int16_t)
				                  + sizeof(WaveTableBandData);
				GeneralMemoryAllocator::get().shortenRight(band->data, newSize);
			}

			// Left-hand side
			if (band->fromCycleNumber) {
				uint32_t idealAmountToShorten =
				    band->fromCycleNumber
				    * (band->cycleSizeNoDuplicates + WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE) * sizeof(int16_t);
				uint32_t amountShortened = GeneralMemoryAllocator::get().shortenLeft(
				    band->data, idealAmountToShorten,
				    sizeof(WaveTableBandData)); // Tell it to move the WaveTableBandData "header" forward
				band->data = (WaveTableBandData*)((uint32_t)band->data + amountShortened);
			}
		}
	}

	return Error::NONE;
}

__attribute__((optimize("unroll-loops"))) void
WaveTable::doRenderingLoopSingleCycle(int32_t* __restrict__ thisSample, int32_t const* bufferEnd,
                                      WaveTableBand* __restrict__ bandHere, uint32_t phase, uint32_t phaseIncrement,
                                      const int16_t* __restrict__ kernel) {

	int32_t bandCycleSizeMagnitude = bandHere->cycleSizeMagnitude;
	int16_t const* __restrict__ table = bandHere->dataAccessAddress;

	do {
		phase += phaseIncrement;

		// Work out the location of the waveform data in memory
		int32_t whichValueCentral = (phase >> (32 - bandCycleSizeMagnitude));
		uint32_t whichValue = whichValueCentral - (kInterpolationMaxNumSamples >> 1);
		int32_t whichValueStored[2];

		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {
			whichValue = whichValue & ((1 << bandCycleSizeMagnitude) - 1);
			whichValueStored[i] = whichValue;
			whichValue += 8;
		}

		// Grab the actual waveform data from memory
		int16x8x2_t interpolationBuffer;
		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {
			interpolationBuffer.val[i] = vld1q_s16(&table[whichValueStored[i]]);
		}

// Get the windowed sinc kernel that we need for this individual audio-sample
#define numBitsInWindowedSyncTableSize 8
#define rshiftAmount ((32 + kInterpolationMaxNumSamplesMagnitude) - 16 - numBitsInWindowedSyncTableSize + 1)

		uint32_t rshifted =
		    ((uint32_t)-phase)
		    >> (rshiftAmount - bandCycleSizeMagnitude); // Warning - rshiftAmount is 13, so bandCycleSizeMagnitude
		                                                // better not be bigger than that!
		int16_t strength2 = rshifted & 32767;

		int32_t windowedSincTableLineOffsetBytes =
		    ((uint32_t)-phase)
		    >> (32 + kInterpolationMaxNumSamplesMagnitude - numBitsInWindowedSyncTableSize - 5
		        - bandCycleSizeMagnitude); // The -5 is for 32 bytes (16 samples) per line in the windowed sinc table.
		windowedSincTableLineOffsetBytes &= 0b111100000;
		int16_t const* __restrict__ sincKernelReadPos =
		    (int16_t const*)((uint32_t)&kernel[0] + windowedSincTableLineOffsetBytes);

		int16x8_t kernelVector[kInterpolationMaxNumSamples >> 3];

		/*
		int16x8x4_t windowedSincReadValues = vld4q_s16(sincKernelReadPos); // Insanely, I could not get this to work.
		Tried reading 2 values, too. I just get some noise from final output.

		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {
		    int16x8_t difference = vsubq_s16(windowedSincReadValues.val[i + 2], windowedSincReadValues.val[i]);
		    int16x8_t multipliedDifference = vqdmulhq_n_s16(difference, strength2);
		    kernelVector[i] = vaddq_s16(windowedSincReadValues.val[i], multipliedDifference);
		}
		*/

		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {
			int16x8_t value1 = vld1q_s16(sincKernelReadPos + (i << 3));
			int16x8_t value2 = vld1q_s16(sincKernelReadPos + 16 + (i << 3));

			int16x8_t difference = vsubq_s16(value2, value1);
			int16x8_t multipliedDifference = vqdmulhq_n_s16(difference, strength2);
			kernelVector[i] = vaddq_s16(value1, multipliedDifference);
		}

		// Apply the windowed sinc kernel to the waveform data
		int32x4_t multiplied;
		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {

			if (i == 0) {
				multiplied = vmull_s16(vget_low_s16(kernelVector[i]), vget_low_s16(interpolationBuffer.val[i]));
			}
			else {
				multiplied =
				    vmlal_s16(multiplied, vget_low_s16(kernelVector[i]), vget_low_s16(interpolationBuffer.val[i]));
			}

			multiplied =
			    vmlal_s16(multiplied, vget_high_s16(kernelVector[i]), vget_high_s16(interpolationBuffer.val[i]));
		}

		int32x2_t twosies = vadd_s32(vget_high_s32(multiplied), vget_low_s32(multiplied));
		int32x2_t onesie = vpadd_s32(twosies, twosies);

		int32_t singleCycleFinalValue = vget_lane_s32(onesie, 0);

		*thisSample = singleCycleFinalValue;

	} while (++thisSample != bufferEnd);
}

#define NUM_BITS_IN_WAVE_INDEX_SCALED_INPUT 30

__attribute__((optimize("unroll-loops"))) void
WaveTable::doRenderingLoop(int32_t* __restrict__ thisSample, int32_t const* bufferEnd, int32_t firstCycleNumber,
                           WaveTableBand* __restrict__ bandHere, uint32_t phase, uint32_t phaseIncrement,
                           uint32_t crossCycleStrength2, int32_t crossCycleStrength2Increment,
                           const int16_t* __restrict__ kernel) {

	int32_t bandCycleSizeMagnitude = bandHere->cycleSizeMagnitude;
	int32_t bandCycleSizeWithDuplicates =
	    bandHere->cycleSizeNoDuplicates + WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE;
	const int16_t* bandData = bandHere->dataAccessAddress;
	int16_t const* __restrict__ table1 = &bandData[firstCycleNumber * bandCycleSizeWithDuplicates];
	int16_t const* __restrict__ table2 = table1 + bandCycleSizeWithDuplicates;

	do {
		phase += phaseIncrement;

		// Work out the location of the waveform data in memory
		int32_t whichValueCentral = (phase >> (32 - bandCycleSizeMagnitude));
		uint32_t whichValue = whichValueCentral - (kInterpolationMaxNumSamples >> 1);
		int32_t whichValueStored[2];

		for (int32_t i = 0; i < kInterpolationMaxNumSamples >> 3; i++) {
			whichValue = whichValue & ((1 << bandCycleSizeMagnitude) - 1);
			whichValueStored[i] = whichValue;
			whichValue += 8;
		}

		// Grab the actual waveform data from memory, for both cycles that we need for this sample
		int16x8x2_t interpolationBuffer[2];
		for (int32_t i = 0; i < kInterpolationMaxNumSamples >> 3; i++) {
			interpolationBuffer[0].val[i] = vld1q_s16(&table1[whichValueStored[i]]);
		}
		for (int32_t i = 0; i < kInterpolationMaxNumSamples >> 3; i++) {
			interpolationBuffer[1].val[i] = vld1q_s16(&table2[whichValueStored[i]]);
		}

// Get the windowed sinc kernel that we need for this individual audio-sample
#define numBitsInWindowedSyncTableSize 8
#define rshiftAmount ((32 + kInterpolationMaxNumSamplesMagnitude) - 16 - numBitsInWindowedSyncTableSize + 1)

		uint32_t rshifted =
		    ((uint32_t)-phase)
		    >> (rshiftAmount - bandCycleSizeMagnitude); // Warning - rshiftAmount is 13, so bandCycleSizeMagnitude
		                                                // better not be bigger than that!
		int16_t strength2 = rshifted & 32767;

		int32_t windowedSincTableLineOffsetBytes =
		    ((uint32_t)-phase)
		    >> (32 + kInterpolationMaxNumSamplesMagnitude - numBitsInWindowedSyncTableSize - 5
		        - bandCycleSizeMagnitude); // The -5 is for 32 bytes (16 samples) per line in the windowed sinc table.
		windowedSincTableLineOffsetBytes &= 0b111100000;
		int16_t const* __restrict__ sincKernelReadPos =
		    (int16_t const*)((uint32_t)&kernel[0] + windowedSincTableLineOffsetBytes);

		int16x8_t kernelVector[kInterpolationMaxNumSamples >> 3];

		/*
		int16x8x4_t windowedSincReadValues = vld4q_s16(sincKernelReadPos); // Insanely, I could not get this to work.
		Tried reading 2 values, too. I just get some noise from final output.

		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {
		    int16x8_t difference = vsubq_s16(windowedSincReadValues.val[i + 2], windowedSincReadValues.val[i]);
		    int16x8_t multipliedDifference = vqdmulhq_n_s16(difference, strength2);
		    kernelVector[i] = vaddq_s16(windowedSincReadValues.val[i], multipliedDifference);
		}
		*/

		for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {
			int16x8_t value1 = vld1q_s16(sincKernelReadPos + (i << 3));
			int16x8_t value2 = vld1q_s16(sincKernelReadPos + 16 + (i << 3));

			int16x8_t difference = vsubq_s16(value2, value1);
			int16x8_t multipliedDifference = vqdmulhq_n_s16(difference, strength2);
			kernelVector[i] = vaddq_s16(value1, multipliedDifference);
		}

		// Apply the windowed sinc kernel to the waveform data
		int32x2_t twosies[2];
		for (int32_t c = 0; c < 2; c++) {
			int32x4_t multiplied;
			for (int32_t i = 0; i < (kInterpolationMaxNumSamples >> 3); i++) {

				if (i == 0) {
					multiplied = vmull_s16(vget_low_s16(kernelVector[i]), vget_low_s16(interpolationBuffer[c].val[i]));
				}
				else {
					multiplied = vmlal_s16(multiplied, vget_low_s16(kernelVector[i]),
					                       vget_low_s16(interpolationBuffer[c].val[i]));
				}

				multiplied =
				    vmlal_s16(multiplied, vget_high_s16(kernelVector[i]), vget_high_s16(interpolationBuffer[c].val[i]));
			}

			twosies[c] = vadd_s32(vget_high_s32(multiplied), vget_low_s32(multiplied));
		}

		// We now have one value for each cycle, so linearly interpolate between those.
		int32x2_t onesie = vpadd_s32(twosies[0], twosies[1]);
		int32_t value1 = vget_lane_s32(onesie, 0);
		int32_t difference = vget_lane_s32(onesie, 1) - value1;

		int32_t waveTableFinalValue = multiply_accumulate_32x32_rshift32_rounded(
		    value1 >> 1, difference,
		    crossCycleStrength2 >> 1); // Have to make value1 a magnitude smaller, because the difference is getting a
		                               // magnitude smaller as a multiplication like this always does.

		*thisSample = waveTableFinalValue;

		crossCycleStrength2 += crossCycleStrength2Increment;

	} while (++thisSample != bufferEnd);
}

const int16_t* getKernel(int32_t phaseIncrement, int32_t bandMaxPhaseIncrement) {
	int32_t whichKernel = 0;
#if NUM_OCTAVES_BETWEEN_WAVETABLE_BANDS == 2
	if (phaseIncrement >= (band->maxPhaseIncrement * 0.65))
		whichKernel = 3;
	else if (phaseIncrement >= (band->maxPhaseIncrement * 0.5))
		whichKernel = 2;
	else if (phaseIncrement >= (band->maxPhaseIncrement * 0.354))
		whichKernel = 1;
	if (!getRandom255()) {
		D_PRINTLN("kernel:  %d", whichKernel);
	}
#else
	uint32_t phaseIncrementHere = phaseIncrement;
	while (phaseIncrementHere >= bandMaxPhaseIncrement && whichKernel < 6) {
		whichKernel += 2;
		phaseIncrementHere >>= 1;
	}
	if (whichKernel < 6 && phaseIncrementHere >= (bandMaxPhaseIncrement * 0.707)) {
		whichKernel++;
	}
#endif

	return &windowedSincKernel[whichKernel][0][0];
}

// waveIndex comes in as a 31-bit number
uint32_t WaveTable::render(int32_t* __restrict__ outputBuffer, int32_t numSamples, uint32_t phaseIncrement,
                           uint32_t phase, bool doOscSync, uint32_t resetterPhaseThisCycle,
                           uint32_t resetterPhaseIncrement, int32_t resetterDivideByPhaseIncrement,
                           uint32_t retriggerPhase, int32_t waveIndex, int32_t waveIndexIncrement) {

#if ENABLE_FX_BENCHMARK
	FX_BENCH_DECLARE(bench_wt_render, "wavetable", "render");
	FX_BENCH_START(bench_wt_render);
#endif

	// Decide on ideal band
	int32_t bHere = bands.search(phaseIncrement, GREATER_OR_EQUAL);
	if (bHere >= bands.getNumElements()) {
		bHere--;
	}
	WaveTableBand* bandHere = (WaveTableBand*)bands.getElementAddress(bHere);

	// If we're an actual wave table with more than one cycle...
	if (numCycles > 1) [[likely]] {
		int32_t numSamplesLeftToDo = numSamples;

		// Haven't yet investigated why, but we do need to use the "rounded" multiply functions here, otherwise get a
		// click when moving to wav pos 0 if numSamples is 32 (when testing performance).
		int32_t waveIndexScaled = multiply_32x32_rshift32_rounded(waveIndexMultiplier, waveIndex);
		int32_t waveIndexIncrementScaled = multiply_32x32_rshift32_rounded(waveIndexMultiplier, waveIndexIncrement);

		int32_t lshiftAmountToGetCrossCycleStrength =
		    32 + numCycleTransitionsNextPowerOf2Magnitude - NUM_BITS_IN_WAVE_INDEX_SCALED_INPUT; // Always positive.
		int32_t crossCycleStrength2Increment = waveIndexIncrementScaled << lshiftAmountToGetCrossCycleStrength;

startRenderingACycle:
		int32_t numSamplesThisCycle = numSamplesLeftToDo;

		int32_t firstCycleNumber =
		    waveIndexScaled >> (NUM_BITS_IN_WAVE_INDEX_SCALED_INPUT - numCycleTransitionsNextPowerOf2Magnitude);

		int32_t firstCycleNumberAfterAllIncrements =
		    (waveIndexScaled + waveIndexIncrementScaled * (numSamplesLeftToDo - 1))
		    >> (NUM_BITS_IN_WAVE_INDEX_SCALED_INPUT - numCycleTransitionsNextPowerOf2Magnitude);

		// See if this rendering window will span multiple cycles as waveIndex is incremented?
		if (firstCycleNumber != firstCycleNumberAfterAllIncrements) {
			int32_t cycleSizeInWaveIndex =
			    1 << (NUM_BITS_IN_WAVE_INDEX_SCALED_INPUT - numCycleTransitionsNextPowerOf2Magnitude);
			int32_t waveIndexPlaceWithinCycle = waveIndexScaled & (cycleSizeInWaveIndex - 1);
			int32_t waveIndexDistanceUntilFinalBigValueStillInThisCycle =
			    (waveIndexIncrementScaled >= 0) ? (cycleSizeInWaveIndex - 1 - waveIndexPlaceWithinCycle)
			                                    : (waveIndexPlaceWithinCycle);
			int32_t waveIndexIncrementScaledAbs = waveIndexIncrementScaled;
			if (waveIndexIncrementScaledAbs < 0) {
				waveIndexIncrementScaledAbs = -waveIndexIncrementScaledAbs;
			}
			int32_t numIncrementsWeCanDoNow =
			    (uint32_t)waveIndexDistanceUntilFinalBigValueStillInThisCycle / (uint32_t)waveIndexIncrementScaledAbs;
			numSamplesThisCycle =
			    numIncrementsWeCanDoNow + 1; // +1 because we can only have to increment *between* the samples we're
			                                 // rendering. If can only do 1 increment, we can still render 2 samples.
			if (ALPHA_OR_BETA_VERSION && numSamplesThisCycle > numSamplesLeftToDo) {
				FREEZE_WITH_ERROR("E386");
			}
		}

		uint32_t crossCycleStrength2 = waveIndexScaled << lshiftAmountToGetCrossCycleStrength;

		// If this band doesn't have data for either of the two cycle indexes we'll be interpolating between...
		while (bandHere->fromCycleNumber > firstCycleNumber || bandHere->toCycleNumber <= firstCycleNumber + 1) {

			bHere++;
			if (bHere == bands.getNumElements()) {
				goto doneRenderingACycle;
			}

			bandHere = (WaveTableBand*)bands.getElementAddress(bHere);
		}

		{
			const int16_t* kernel = getKernel(phaseIncrement, bandHere->maxPhaseIncrement);

			if (doOscSync) {
				int32_t* bufferStartThisSync = outputBuffer;
				uint32_t resetterPhase = resetterPhaseThisCycle;
				int32_t numSamplesThisOscSyncSession = numSamplesThisCycle;
				renderOscSync(
				    [&](int32_t const* const bufferEndThisSyncRender, uint32_t phase, int32_t* __restrict__ writePos) {
					    doRenderingLoop(bufferStartThisSync, bufferEndThisSyncRender, firstCycleNumber, bandHere, phase,
					                    phaseIncrement, crossCycleStrength2, crossCycleStrength2Increment, kernel);
				    },
				    [&](uint32_t samplesIncludingNextCrossoverSample) {
					    crossCycleStrength2 += crossCycleStrength2Increment * (samplesIncludingNextCrossoverSample - 1);
				    },
				    phase, phaseIncrement, resetterPhase, resetterPhaseIncrement, resetterDivideByPhaseIncrement,
				    retriggerPhase, numSamplesThisOscSyncSession, bufferStartThisSync);
			}
			else {
				int32_t const* bufferEnd = outputBuffer + numSamplesThisCycle;
				doRenderingLoop(outputBuffer, bufferEnd, firstCycleNumber, bandHere, phase, phaseIncrement,
				                crossCycleStrength2, crossCycleStrength2Increment, kernel);
				phase += phaseIncrement * numSamplesThisCycle;
			}
		}

doneRenderingACycle:
		numSamplesLeftToDo -= numSamplesThisCycle;
		if (numSamplesLeftToDo) {
			outputBuffer += numSamplesThisCycle;
			waveIndexScaled += waveIndexIncrementScaled * numSamplesThisCycle;
			resetterPhaseThisCycle += resetterPhaseIncrement * numSamplesThisCycle;
			goto startRenderingACycle;
		}
	}

	// Or, if we're actually a single-cycle waveform, with only one cycle...
	else {
		const int16_t* kernel = getKernel(phaseIncrement, bandHere->maxPhaseIncrement);
		if (doOscSync) {
			int32_t* bufferStartThisSync = outputBuffer;
			uint32_t resetterPhase = resetterPhaseThisCycle;
			int32_t numSamplesThisOscSyncSession = numSamples;
			renderOscSync(
			    [&](int32_t const* const bufferEndThisSyncRender, uint32_t phase, int32_t* __restrict__ writePos) {
				    doRenderingLoopSingleCycle(bufferStartThisSync, bufferEndThisSyncRender, bandHere, phase,
				                               phaseIncrement, kernel);
			    },
			    [](uint32_t) {}, phase, phaseIncrement, resetterPhase, resetterPhaseIncrement,
			    resetterDivideByPhaseIncrement, retriggerPhase, numSamplesThisOscSyncSession, bufferStartThisSync);
		}
		else {
			int32_t const* bufferEnd = outputBuffer + numSamples;
			doRenderingLoopSingleCycle(outputBuffer, bufferEnd, bandHere, phase, phaseIncrement, kernel);
			phase += phaseIncrement * numSamples;
		}
	}

#if ENABLE_FX_BENCHMARK
	FX_BENCH_STOP(bench_wt_render);
#endif

	return phase;
}

void WaveTable::numReasonsIncreasedFromZero() {

	// Remove all bands' data from Stealable-queue, as it may no longer be stolen.
	for (int32_t b = bands.getNumElements() - 1; b >= 0; b--) {
		WaveTableBand* band = (WaveTableBand*)bands.getElementAddress(b);
		if (band->data) {
			band->data->remove();
		}
	}
}

void WaveTable::numReasonsDecreasedToZero(char const* errorCode) {

	// Put all bands' data in queue to be stolen.
	for (int32_t b = bands.getNumElements() - 1; b >= 0; b--) {
		WaveTableBand* band = (WaveTableBand*)bands.getElementAddress(b);
		if (band->data) {
#if ALPHA_OR_BETA_VERSION
			if (band->data->list) {
				FREEZE_WITH_ERROR("E388");
			}
#endif
			GeneralMemoryAllocator::get().putStealableInQueue(band->data, StealableQueue::NO_SONG_WAVETABLE_BAND_DATA);
		}
	}
}

/*
    for (int32_t b = bands.getNumElements() - 1; b >= 0; b--) {
        WaveTableBand* band = (WaveTableBand*)bands.getElementAddress(b);

    }
 */