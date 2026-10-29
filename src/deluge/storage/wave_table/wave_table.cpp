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
#include "arm_neon_shim.h"
#include "definitions_cxx.hpp"
#include "dsp-next/fixed/fft/real_fft.h"
#include "dsp-next/fixed/interpolation/interpolator.h"
#include "dsp-next/fixed/wavetable/band_builder.h"
#include "dsp/fft/fft_config_manager.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "model/sample/sample_reader_bridge.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/audio/file_byte_source.h"
#include "storage/cluster/cluster.h"
#include "storage/storage_manager.h"
#include "util/fixedpoint.h"
#include <algorithm>
#include <cstring>
#include <ranges>

extern int32_t oscSyncRenderingBuffer[];

using deluge::dsp::fixed::BandBuilder;
using deluge::dsp::fixed::BandGeometry;
using deluge::dsp::fixed::BandTarget;
using deluge::dsp::fixed::kWavetableDuplicateSamplesAtCycleEnd;
using deluge::dsp::fixed::fft::Complex;
using deluge::dsp::fixed::fft::RealFft;

namespace {
// RAII cleanup for WaveTable::setup's working allocations. At scope exit it frees whichever of the two temp
// FFT buffers are still non-null, and — unless the build `succeeded` — calls deleteAllBandsAndData() on the
// partially-built bands. On the success path setup frees the temp buffers early (its deliberate internal-RAM release
// before the band-trim tail), nulls the pointers and sets `succeeded`, so this guard is then a no-op.
struct SetupAllocGuard {
	WaveTable& waveTable;
	int32_t*& currentCycleInt32;
	Complex*& frequencyDomainData;
	// In-memory sample path only: the reusable file-cluster-aligned block buffer and the frame-reader temp,
	// freed unconditionally on any exit (they are not needed past the cycle loop). Null on the file path.
	void* sampleBlockAlloc = nullptr;
	void* sampleFrameAlloc = nullptr;
	bool succeeded = false;

	~SetupAllocGuard() {
		if (frequencyDomainData != nullptr) {
			delugeDealloc(frequencyDomainData);
		}
		if (currentCycleInt32 != nullptr) {
			delugeDealloc(currentCycleInt32);
		}
		if (sampleBlockAlloc != nullptr) {
			deluge::memory::dealloc(sampleBlockAlloc);
		}
		if (sampleFrameAlloc != nullptr) {
			deluge::memory::dealloc(sampleFrameAlloc);
		}
		if (!succeeded) {
			waveTable.deleteAllBandsAndData();
		}
	}
};
} // namespace

WaveTable::WaveTable() : AudioFile(AudioFileType::WAVETABLE) {
}

WaveTable::~WaveTable() {
	deleteAllBandsAndData();
}

void WaveTable::deleteAllBandsAndData() {
	bands.clear(); // Destructors release each band's data
}

deluge::dsp::fixed::WaveTableData WaveTable::view() {
	int32_t numBands = static_cast<int32_t>(bands.size());
	return {
	    numCycles,
	    numCycleTransitionsNextPowerOf2Magnitude,
	    waveIndexMultiplier,
	    numBands ? bands.data() : nullptr,
	    sizeof(WaveTableBand),
	    numBands,
	};
}

Error WaveTable::setupFromSample(Sample& sample) {
	// In-memory conversion: setup() takes cycle size / channels / byte depth / data offset from the Sample,
	// so the remaining parameters are unused here.
	return setup(&sample, 0, 0, 0, 0, RawDataFormat::NATIVE, nullptr);
}

Error WaveTable::setupFromFile(FileByteSource& source, int32_t cycleSize, uint32_t audioDataStartPosBytes,
                               uint32_t audioDataLengthBytes, int32_t byteDepth, RawDataFormat rawDataFormat) {
	return setup(nullptr, cycleSize, audioDataStartPosBytes, audioDataLengthBytes, byteDepth, rawDataFormat, &source);
}

Error WaveTable::setup(Sample* sample, int32_t rawFileCycleSize, uint32_t audioDataStartPosBytes,
                       uint32_t audioDataLengthBytes, int32_t byteDepth, RawDataFormat rawDataFormat,
                       FileByteSource* byteSource) {
	AudioEngine::logAction("WaveTable::setup");

	uint32_t originalSampleLengthInSamples;

	if (sample) {
		filePath = sample->filePath;
		loadedFromAlternatePath = sample->loadedFromAlternatePath;

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

	const BandGeometry geometry = BandGeometry::of(rawFileCycleSize);

	// If cycle size isn't a power of two...
	if (!geometry.source_is_power_of_two) {

		// This is allowed if we're doing just single-cycle.
		if (originalSampleLengthInSamples < (rawFileCycleSize << 1)
		    && originalSampleLengthInSamples >= rawFileCycleSize) {
			// Band 0 is the next power of two above the cycle, so all of its spectrum fits.
			numCycles = 1;
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
		    originalSampleLengthInSamples >> geometry.band0_magnitude; // Will round down, which is what we want.
		if (numCycles < 1) {
			return Error::FILE_NOT_LOADABLE_AS_WAVETABLE; // Surely can't happen, becacuse of the checks / limiting
			                                              // we've done above?
		}
	}

	AudioEngine::logAction("Getting fft config");
	const RealFft* fftCFGForInitialBand = FFTConfigManager::getConfig(geometry.band0_magnitude);
	AudioEngine::logAction("Got fft config");

	// If that returned NULL, normally we can just opt to not do FFTs and have just 1 band.
	// But in the case where the original wasn't a power-of-two size, we're gonna have to do FFTs (as well as one
	// DFT)...
	if (!fftCFGForInitialBand && !geometry.source_is_power_of_two) {

		// Actually screw it, this is so rare, it's easier just to make a rule that for non-power-of-two we *have* to
		// render it out to that bigger size. Cos if we allow a smaller initial band, we don't have enough space in it
		// for the dummy writing we do during load, which could be avoided, but yeah this is so rare.
		return Error::INSUFFICIENT_RAM;
	}

	Error error = Error::NONE; // Only the `bands.resize` catch below sets it; a successful resize must leave it NONE.

	try {
		bands.resize(geometry.num_bands(fftCFGForInitialBand != nullptr));
	} catch (deluge::exception&) {
		error = Error::INSUFFICIENT_RAM;
	}
	if (error != Error::NONE) {
		return error; // bands.resize failed: nothing else is allocated yet, so nothing to clean up.
	}
	for (int32_t b = 0; b < static_cast<int32_t>(bands.size()); b++) {
		WaveTableBand& band = bands[b];
		band.fromCycleNumber = 0;
		band.toCycleNumber = 0;
		band.cycleSizeNoDuplicates = geometry.cycle_size(b);
		band.cycleSizeMagnitude = geometry.cycle_magnitude(b);
		band.maxPhaseIncrement = geometry.max_phase_increment(b);
	}

	// Every band but a power-of-two source's band 0 is rendered through an inverse transform. A band whose transform
	// cannot be built is dropped before anything is allocated for it; the bands either side of it are unaffected.
	for (int32_t b = static_cast<int32_t>(bands.size()) - 1; b >= (geometry.source_is_power_of_two ? 1 : 0); b--) {
		if (FFTConfigManager::getConfig(bands[b].cycleSizeMagnitude) == nullptr) {
			bands.erase(bands.begin() + b);
		}
	}

	// From here on, working allocations are released by allocGuard on any early return. The temp buffers are declared
	// null up front so the guard can free whichever exist; on success setup frees them early and sets
	// allocGuard.succeeded (see below). (No __restrict__: the guard binds references to these, and the qualifier is
	// only an optimisation hint on these one-shot load-time buffers — it never affects the decoded output.)
	int32_t* currentCycleInt32 = nullptr;
	Complex* frequencyDomainData = nullptr;
	SetupAllocGuard allocGuard{*this, currentCycleInt32, frequencyDomainData};

	AudioEngine::logAction("just started wavetable");
	AudioEngine::routineWithClusterLoading(); // TODO: the routine calls in this function might be more than needed - I
	                                          // didn't profile very closely.
	AudioEngine::logAction("about to set up bands");

	for (int32_t b = 0; b < static_cast<int32_t>(bands.size()); b++) {
		int32_t bandSizeSamplesWithDuplicates =
		    numCycles * (bands[b].cycleSizeNoDuplicates + kWavetableDuplicateSamplesAtCycleEnd);
		// All bands hold 16-bit samples, even when the source file is 8-bit.
		bands[b].samples = BandSamples::allocate(bandSizeSamplesWithDuplicates);
		if (!bands[b].samples) {
			// All bands from this one onwards still have no data, so get rid of them before anything else
			// tries to do anything with them; allocGuard frees the ones already built.
			bands.erase(bands.begin() + b, bands.end());
			return Error::INSUFFICIENT_RAM;
		}

		bands[b].dataAccessAddress = bands[b].samples.data();
	}

	AudioEngine::logAction("bands set up");
	AudioEngine::routineWithClusterLoading();
	AudioEngine::logAction("allocating working memory");

	// Create the temporary memory where we'll store the 32-bit int32_t version of the current cycle being read, to
	// perform the FFT on. This will also be used for outputting the time-domain cycle data back out of the inverse FFT,
	// so ensure it's big enough for that if our biggest band is bigger than the cycle size in the file (i.e. if that's
	// not a power-of-two).
	int32_t currentCycleMemorySize = std::max(rawFileCycleSize, geometry.cycle_size(0));
	// Internal RAM is good, and it's only temporary
	currentCycleInt32 = (int32_t*)deluge::memory::alloc_fast(currentCycleMemorySize * sizeof(int32_t));
	if (!currentCycleInt32) {
		return Error::INSUFFICIENT_RAM;
	}

	// And temporary FFT output (frequency domain) buffer. The same sizing considerations as above are needed, and we
	// use that same decision here
	// - except for frequency-domain complex numbers, we only need to store half of it, plus one.
	frequencyDomainData = (Complex*)deluge::memory::alloc_fast(((currentCycleMemorySize >> 1) + 1) * sizeof(Complex));
	if (!frequencyDomainData) {
		return Error::INSUFFICIENT_RAM; // allocGuard frees currentCycleInt32 + the bands.
	}

	AudioEngine::logAction("working memory allocated");
	AudioEngine::routineWithClusterLoading();
	AudioEngine::logAction("finalizing vars");

	BandBuilder builder{
	    {.source_cycle_size = rawFileCycleSize, .band0_magnitude = geometry.band0_magnitude, .num_cycles = numCycles},
	    {currentCycleInt32, static_cast<size_t>(currentCycleMemorySize)},
	    {frequencyDomainData, static_cast<size_t>((currentCycleMemorySize >> 1) + 1)}};
	const auto bandTarget = [&](int32_t b) {
		return BandTarget{.samples = bands[b].dataAccessAddress, .cycle_magnitude = bands[b].cycleSizeMagnitude};
	};

	int32_t clusterIndex = audioDataStartPosBytes >> Cluster::size_magnitude;
	int32_t byteIndexWithinCluster = audioDataStartPosBytes & (Cluster::size - 1);

	if (!sample) {
		byteSource->seekForwardTo(audioDataStartPosBytes); // In case the source wasn't quite up to here yet! Can happen
		                                                   // in AIFF files, with their "offset"
	}

	// For the file path the deserializer owns the cluster buffer; the in-memory path sets it per-cluster below.
	char const* sourceBuffer = sample ? nullptr : byteSource->clusterBuffer();
	uint32_t bytesOverlappingFromLastCluster;

	uint32_t bitMask = 0xFFFFFFFF << ((4 - byteDepth) * 8);

	// In-memory sample path: a single reusable, file-cluster-aligned block buffer holding the native audio bytes,
	// refilled per cluster from the streaming frame reader (deluge_sample_read). It carries CACHE_LINE_SIZE of
	// slack either side — mirroring smDeserializer.fileClusterBuffer — because the band loop below does
	// misaligned 32-bit reads a few bytes BEFORE the buffer start and just PAST its end. The frame temp holds
	// whole stride-sized frames (the reader serves whole frames only) before the straddle copy.
	char* sampleBlockBuffer = nullptr;
	void* sampleFrameTemp = nullptr;
	size_t sampleFrameTempBytes = 0;
	uint32_t sampleSourceId = 0;
	if (sample) {
		allocGuard.sampleBlockAlloc = deluge::memory::alloc_sdram(Cluster::size + CACHE_LINE_SIZE * 2);
		allocGuard.sampleFrameAlloc = deluge::memory::alloc_sdram(Cluster::size + CACHE_LINE_SIZE * 2);
		if (!allocGuard.sampleBlockAlloc || !allocGuard.sampleFrameAlloc) {
			return Error::INSUFFICIENT_RAM; // allocGuard frees whichever alloc succeeded + FFT buffers + bands.
		}
		sampleBlockBuffer = static_cast<char*>(allocGuard.sampleBlockAlloc) + CACHE_LINE_SIZE;
		sampleFrameTemp = allocGuard.sampleFrameAlloc;
		sampleFrameTempBytes = Cluster::size + CACHE_LINE_SIZE * 2;
		sampleSourceId = deluge::sample::source_id_for(*sample);
	}

	int32_t clusterIndexCurrentlyLoaded = -1; // Initially, none is loaded yet.

	bool swappingEndianness =
	    (rawDataFormat == RawDataFormat::ENDIANNESS_WRONG_32 || rawDataFormat == RawDataFormat::ENDIANNESS_WRONG_24
	     || rawDataFormat == RawDataFormat::ENDIANNESS_WRONG_16);

	bool needToMisalignData = (rawDataFormat == RawDataFormat::NATIVE || rawDataFormat == RawDataFormat::UNSIGNED_8);

	// For each wave cycle...
	for (int32_t cycleIndex = 0; cycleIndex < numCycles; cycleIndex++) {

		AudioEngine::logAction("new cycle began");
		AudioEngine::routineWithClusterLoading();
		AudioEngine::logAction("new cycle beginning");

		int32_t* cycleBufferDestination = builder.cycle_input().data();
		int32_t sourceBytesLeftToCopyThisCycle = rawFileCycleSize * byteDepth;

		// Read some clusters of audio data - either from existing Sample in memory, or from file.
		do {

			// If converting an existing Sample from memory into this WaveTable...
			if (sample) {

				// If we need to fill the block buffer for a new Cluster now (which might not be the case if we've
				// just switched into a new Cycle which starts still within the same Cluster)...
				if (clusterIndex != clusterIndexCurrentlyLoaded) {

					// Reproduce the file-cluster-aligned native bytes for `clusterIndex` from the frame reader:
					// sampleBlockBuffer[o] == native byte at file offset clusterIndex*Cluster::size + o, for the
					// audio-data bytes that fall in this cluster. The rest is zero-filled defensively so the band
					// loop's misaligned edge reads never consume an uninitialised byte.
					const uint64_t clusterFileBase = static_cast<uint64_t>(clusterIndex) << Cluster::size_magnitude;
					const uint64_t audioStart = audioDataStartPosBytes;
					const uint64_t audioEnd = audioStart + sample->audioDataLengthBytes;
					const uint64_t fileLo = std::max<uint64_t>(clusterFileBase, audioStart);
					const uint64_t fileHi = std::min<uint64_t>(clusterFileBase + Cluster::size, audioEnd);

					memset(sampleBlockBuffer, 0, Cluster::size);

					if (fileHi > fileLo) {
						const uint32_t stride = byteDepth * numChannels; // frame stride; reader serves whole frames
						const uint64_t audioLo = fileLo - audioStart; // audio-byte index of first byte in this cluster
						const uint64_t audioHi = fileHi - audioStart;
						const uint64_t firstFrame = audioLo / stride;
						const uint64_t lastFrameExcl = (audioHi + stride - 1) / stride; // ceil to a whole frame
						const uint32_t numFrames = static_cast<uint32_t>(lastFrameExcl - firstFrame);

						// The first frame may begin a few bytes BEFORE fileLo when the cluster boundary falls
						// mid-frame; the reader lands whole frames stride-aligned in the temp, and we copy from that
						// in-frame offset so the straddle is stitched together correctly.
						const uint32_t framesRead = deluge_sample_read(sampleSourceId, firstFrame, numFrames,
						                                               sampleFrameTemp, sampleFrameTempBytes);
						if (framesRead < numFrames) {
							// A short/zero read left needed audio bytes unfilled (card failure / EOF): fail loudly
							// (return Error::SD_CARD through allocGuard) rather than silently zero-filling real audio.
							return Error::SD_CARD; // allocGuard frees the sample buffers + FFT buffers + bands.
						}

						const uint32_t inFrameOffset = static_cast<uint32_t>(audioLo % stride);
						const uint32_t copyBytes = static_cast<uint32_t>(audioHi - audioLo);
						const uint32_t blockOffset = static_cast<uint32_t>(fileLo - clusterFileBase);
						memcpy(sampleBlockBuffer + blockOffset,
						       static_cast<const char*>(sampleFrameTemp) + inFrameOffset, copyBytes);
					}

					clusterIndexCurrentlyLoaded = clusterIndex;
					sourceBuffer = sampleBlockBuffer;
				}
			}

			// Or, if reading data afresh from a file...
			else {
				if (byteIndexWithinCluster < 0) {
					byteSource->byteIndexWithinCluster() -=
					    byteIndexWithinCluster; // Will put it right at the start of the next cluster, to force it to
					                            // read from card
				}
				error = byteSource->advanceClustersIfNecessary();
				if (error != Error::NONE) {
					return error; // allocGuard frees both temp buffers + the bands.
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
		*(cycleBufferDestination++) = value32 >> deluge::dsp::fixed::kCycleHeadroomBits;                               \
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
					byteSource->byteIndexWithinCluster() +=
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

			int32_t numSourceBytesLookingAtThisCluster = sourceStopAt - source;
			if (numSourceBytesLookingAtThisCluster > sourceBytesLeftToCopyThisCycle) {
				sourceStopAt = source + sourceBytesLeftToCopyThisCycle;
			}

			const int32_t* const cycleDestinationStartedAt = cycleBufferDestination;

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

			int32_t samplesJustCopied = cycleBufferDestination - cycleDestinationStartedAt;
			int32_t sourceBytesJustRead = samplesJustCopied * byteDepth;
			sourceBytesLeftToCopyThisCycle -= sourceBytesJustRead;
			byteIndexWithinCluster += sourceBytesJustRead;

			if (!sample) {
				byteSource->byteIndexWithinCluster() += sourceBytesJustRead; // Keep this up to date
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

		if (geometry.source_is_power_of_two) {
			// Band 0 is the cycle as read.
			builder.copy_band0(bandTarget(0), cycleIndex);
			if (static_cast<int32_t>(bands.size()) <= 1) {
				bands[0].toCycleNumber = builder.to_cycle_after(cycleIndex);
				continue;
			}
			builder.analyse(fftCFGForInitialBand);
		}
		else {
			// A cycle that is not a power of two takes the DFT, and band 0 is rendered from its spectrum like the rest.
			AudioEngine::logAction("dft start");
			builder.analyse(nullptr);
			AudioEngine::logAction("dft done");
		}

		AudioEngine::logAction("got freq domain data");
		AudioEngine::routineWithClusterLoading();
		AudioEngine::logAction("building bands");

		bands[0].toCycleNumber = builder.to_cycle_after(cycleIndex);
		for (int32_t b = geometry.source_is_power_of_two ? 1 : 0; b < static_cast<int32_t>(bands.size()); b++) {
			bands[b].toCycleNumber = builder.to_cycle_after(cycleIndex);

			AudioEngine::logAction("started band");
			AudioEngine::routineWithClusterLoading();
			AudioEngine::logAction("doing FFT to time domain");

			// Fetched before the first cycle, so it is there.
			builder.render_band(bandTarget(b), cycleIndex, *FFTConfigManager::getConfig(bands[b].cycleSizeMagnitude));
		}
	}

	AudioEngine::logAction("finished all cycles");
	AudioEngine::routineWithClusterLoading();
	AudioEngine::logAction("finalizing wavetable");

	D_PRINTLN("initial num bands:  %d", static_cast<int32_t>(bands.size()));

	// Ok, we've now processed all Cycles. (The in-memory sample block buffer + frame temp are released by
	// allocGuard at scope exit; the file path's cluster buffer is owned by the byte source.)

	if (numCycles > 1) {
		int32_t numCycleTransitions = numCycles - 1;

		numCycleTransitionsNextPowerOf2Magnitude = getMagnitudeOld(numCycleTransitions);

		waveIndexMultiplier = numCycleTransitions << (31 - numCycleTransitionsNextPowerOf2Magnitude);
	}

	// Dispose of temp memory now — release this internal RAM before the band-trimming tail below (deliberate;
	// see SetupAllocGuard). Null the pointers + mark success so the guard then frees nothing and keeps the bands.
	delugeDealloc(currentCycleInt32);
	currentCycleInt32 = nullptr;
	delugeDealloc(frequencyDomainData);
	frequencyDomainData = nullptr;
	allocGuard.succeeded = true;

	// Printout stats
	D_PRINTLN("initial band size if all populated: %d",
	          numCycles * (bands[0].cycleSizeNoDuplicates + kWavetableDuplicateSamplesAtCycleEnd) * 2);
	D_PRINTLN("initial band size after trimming: %d",
	          (bands[0].toCycleNumber - bands[0].fromCycleNumber)
	              * (bands[0].cycleSizeNoDuplicates + kWavetableDuplicateSamplesAtCycleEnd) * 2);
	int32_t total = 0;
	for (WaveTableBand const& band : bands | std::views::drop(1)) {
		total += (band.toCycleNumber - band.fromCycleNumber)
		         * (band.cycleSizeNoDuplicates + kWavetableDuplicateSamplesAtCycleEnd) * 2;
	}
	D_PRINTLN("other bands total size after trimming:  %d", total);

	// Dispose of bands that didn't end up getting used, or such portions of their memory.
	for (int32_t b = static_cast<int32_t>(bands.size()) - 1; b >= 0;
	     b--) { // Traverse backwards because we might delete elements.
		WaveTableBand* band = &bands[b];

		// Delete whole band if no data needed
		if (band->fromCycleNumber >= band->toCycleNumber) {
			bands.erase(bands.begin() + b);
			D_PRINTLN("deleted whole band -  %d", b);
		}

		// Otherwise, shorten its memory to the cycles it holds.
		else {
			if (band->toCycleNumber < numCycles) {
				if (!b) {
					D_PRINTLN("(band 0) ");
				}
				D_PRINTLN("deleting num cycles from right-hand side:  %d", numCycles - band->toCycleNumber);
				band->samples.shrink_to(band->toCycleNumber
				                        * (band->cycleSizeNoDuplicates + kWavetableDuplicateSamplesAtCycleEnd));
			}
		}
	}

	return Error::NONE;
}
