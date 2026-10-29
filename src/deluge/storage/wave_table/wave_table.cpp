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
#include "definitions_cxx.hpp"
#include "dsp-next/fixed/fft/real_fft.h"
#include "dsp-next/fixed/wavetable/band_builder.h"
#include "dsp/fft/fft_config_manager.h"
#include "memory/allocate_unique.h"
#include "memory/fast_allocator.h"
#include "model/sample/sample.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/file_byte_source.h"
#include "storage/wave_table/wave_table_cycle_reader.h"
#include "util/functions.h"
#include <algorithm>

using deluge::dsp::fixed::BandBuilder;
using deluge::dsp::fixed::BandGeometry;
using deluge::dsp::fixed::BandTarget;
using deluge::dsp::fixed::kWavetableDuplicateSamplesAtCycleEnd;
using deluge::dsp::fixed::fft::Complex;
using deluge::dsp::fixed::fft::RealFft;

namespace {
/// @brief Clears a table's bands unless setup reached its end, so a failed build keeps none.
struct ClearBandsUnlessBuilt {
	WaveTable& table;
	bool built = false;

	~ClearBandsUnlessBuilt() {
		if (!built) {
			table.deleteAllBandsAndData();
		}
	}
};

/// @brief Sizes @p bands by @p geometry and allocates each one's samples: one band per octave, or band 0 alone without
///        its transform, less any band whose inverse transform cannot be built. Every band but a power-of-two source's
///        band 0 is rendered through one; dropping one leaves the bands either side of it unaffected.
Error layOutBands(deluge::fast_vector<WaveTableBand>& bands, const BandGeometry& geometry, bool band0HasTransform,
                  int32_t numCycles) {
	try {
		bands.resize(geometry.num_bands(band0HasTransform));
	} catch (deluge::exception&) {
		return Error::INSUFFICIENT_RAM;
	}
	for (int32_t b = 0; b < static_cast<int32_t>(bands.size()); b++) {
		bands[b].cycleSizeNoDuplicates = geometry.cycle_size(b);
		bands[b].cycleSizeMagnitude = geometry.cycle_magnitude(b);
		bands[b].maxPhaseIncrement = geometry.max_phase_increment(b);
	}
	for (int32_t b = static_cast<int32_t>(bands.size()) - 1; b >= (geometry.source_is_power_of_two ? 1 : 0); b--) {
		if (FFTConfigManager::getConfig(bands[b].cycleSizeMagnitude) == nullptr) {
			bands.erase(bands.begin() + b);
		}
	}
	AudioEngine::routineWithClusterLoading();
	for (WaveTableBand& band : bands) {
		// Every band holds 16-bit samples, even when the source file is 8-bit.
		band.samples =
		    BandSamples::allocate(numCycles * (band.cycleSizeNoDuplicates + kWavetableDuplicateSamplesAtCycleEnd));
		if (!band.samples) {
			return Error::INSUFFICIENT_RAM;
		}
		band.dataAccessAddress = band.samples.data();
	}
	return Error::NONE;
}
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
	filePath = sample.filePath;
	loadedFromAlternatePath = sample.loadedFromAlternatePath;
	numChannels = sample.numChannels;

	const auto sourceLengthSamples = static_cast<uint32_t>(sample.lengthInSamples);
	const auto cycleSize = static_cast<int32_t>(std::min(sample.waveTableCycleSize, sourceLengthSamples));
	WaveTableCycleReader reader{sample,
	                            {.cycle_size = cycleSize,
	                             .byte_depth = sample.byteDepth,
	                             .num_channels = sample.numChannels,
	                             .raw_data_format = RawDataFormat::NATIVE,
	                             .audio_data_start_bytes = sample.audioDataStartPosBytes,
	                             .audio_data_length_bytes = static_cast<uint32_t>(sample.audioDataLengthBytes)}};
	return setup(reader, cycleSize, sourceLengthSamples);
}

Error WaveTable::setupFromFile(FileByteSource& source, int32_t cycleSize, uint32_t audioDataStartPosBytes,
                               uint32_t audioDataLengthBytes, int32_t byteDepth, RawDataFormat rawDataFormat) {
	const uint32_t sourceLengthSamples = audioDataLengthBytes / static_cast<uint8_t>(byteDepth * numChannels);
	if (static_cast<uint32_t>(cycleSize) > sourceLengthSamples) {
		cycleSize = static_cast<int32_t>(sourceLengthSamples);
	}
	WaveTableCycleReader reader{source,
	                            {.cycle_size = cycleSize,
	                             .byte_depth = byteDepth,
	                             .num_channels = numChannels,
	                             .raw_data_format = rawDataFormat,
	                             .audio_data_start_bytes = audioDataStartPosBytes,
	                             .audio_data_length_bytes = audioDataLengthBytes}};
	return setup(reader, cycleSize, sourceLengthSamples);
}

Error WaveTable::setup(WaveTableCycleReader& reader, int32_t sourceCycleSize, uint32_t sourceLengthSamples) {
	AudioEngine::logAction("WaveTable::setup");

	// The checks no wavetable can pass; whether a file was meant as one is decided before this.
	if (sourceCycleSize < kWavetableMinCycleSize) {
		return Error::FILE_NOT_LOADABLE_AS_WAVETABLE;
	}
	const BandGeometry geometry = BandGeometry::of(sourceCycleSize);
	if (geometry.source_is_power_of_two) {
		numCycles = static_cast<int32_t>(sourceLengthSamples >> geometry.band0_magnitude);
	}
	// A cycle that is not a power of two is allowed only as the file's one cycle: transforming many takes too long.
	else if (sourceLengthSamples < static_cast<uint32_t>(sourceCycleSize << 1)) {
		numCycles = 1;
	}
	else {
		return Error::FILE_NOT_LOADABLE_AS_WAVETABLE;
	}

	const RealFft* band0Transform = FFTConfigManager::getConfig(geometry.band0_magnitude);
	// Without band 0's transform a power-of-two table keeps band 0 alone; any other cannot be built.
	if (band0Transform == nullptr && !geometry.source_is_power_of_two) {
		return Error::INSUFFICIENT_RAM;
	}

	ClearBandsUnlessBuilt clearBands{*this};
	if (const Error error = layOutBands(bands, geometry, band0Transform != nullptr, numCycles); error != Error::NONE) {
		return error;
	}
	AudioEngine::routineWithClusterLoading();

	// The source cycle in and each inverse transform out, so as large as band 0 when that is larger than the source.
	const int32_t workingSize = std::max(sourceCycleSize, geometry.cycle_size(0));
	auto cycle = deluge::allocate_unique<int32_t, deluge::memory::fast_allocator>(workingSize);
	auto spectrum = deluge::allocate_unique<Complex, deluge::memory::fast_allocator>((workingSize >> 1) + 1);
	if (!cycle || !spectrum) {
		return Error::INSUFFICIENT_RAM;
	}
	AudioEngine::routineWithClusterLoading();
	if (const Error error = reader.open(); error != Error::NONE) {
		return error;
	}

	BandBuilder builder{
	    {.source_cycle_size = sourceCycleSize, .band0_magnitude = geometry.band0_magnitude, .num_cycles = numCycles},
	    {cycle->get(), static_cast<size_t>(workingSize)},
	    {spectrum->get(), static_cast<size_t>((workingSize >> 1) + 1)}};
	const auto bandTarget = [&](int32_t b) {
		return BandTarget{.samples = bands[b].dataAccessAddress, .cycle_magnitude = bands[b].cycleSizeMagnitude};
	};

	for (int32_t cycleIndex = 0; cycleIndex < numCycles; cycleIndex++) {
		AudioEngine::routineWithClusterLoading();
		if (const Error error = reader.read_cycle(builder.cycle_input()); error != Error::NONE) {
			return error;
		}
		AudioEngine::routineWithClusterLoading();

		if (geometry.source_is_power_of_two) {
			// Band 0 is the cycle as read.
			builder.copy_band0(bandTarget(0), cycleIndex);
			if (bands.size() == 1) {
				bands[0].toCycleNumber = builder.to_cycle_after(cycleIndex);
				continue;
			}
			builder.analyse(band0Transform);
		}
		else {
			// A cycle that is not a power of two takes the DFT, and band 0 is rendered from its spectrum like the rest.
			builder.analyse(nullptr);
		}
		AudioEngine::routineWithClusterLoading();

		bands[0].toCycleNumber = builder.to_cycle_after(cycleIndex);
		for (int32_t b = geometry.source_is_power_of_two ? 1 : 0; b < static_cast<int32_t>(bands.size()); b++) {
			bands[b].toCycleNumber = builder.to_cycle_after(cycleIndex);
			AudioEngine::routineWithClusterLoading();
			// Fetched before the first cycle, so it is there.
			builder.render_band(bandTarget(b), cycleIndex, *FFTConfigManager::getConfig(bands[b].cycleSizeMagnitude));
		}
	}
	AudioEngine::routineWithClusterLoading();

	if (numCycles > 1) {
		const int32_t numCycleTransitions = numCycles - 1;
		numCycleTransitionsNextPowerOf2Magnitude = getMagnitudeOld(numCycleTransitions);
		waveIndexMultiplier = numCycleTransitions << (31 - numCycleTransitionsNextPowerOf2Magnitude);
	}

	// Give the working buffers' fast RAM back before trimming the bands.
	cycle->reset();
	spectrum->reset();

	// Each band keeps only the cycles it holds.
	for (WaveTableBand& band : bands) {
		if (band.toCycleNumber < numCycles) {
			band.samples.shrink_to(band.toCycleNumber
			                       * (band.cycleSizeNoDuplicates + kWavetableDuplicateSamplesAtCycleEnd));
		}
	}
	clearBands.built = true;
	return Error::NONE;
}
