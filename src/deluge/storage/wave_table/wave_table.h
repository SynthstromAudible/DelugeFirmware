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

#pragma once

#include "definitions_cxx.hpp"
#include "model/sample/sample.h"
#include "storage/audio/audio_file.h"
#include "storage/wave_table/wave_table_band_data.h"
#include "util/containers.h"
#include <utility>

class Sample;
class FileByteSource;

class WaveTableBand {
public:
	WaveTableBand() = default;
	~WaveTableBand();

	// The destructor releases `data`, so ownership must transfer on move and copying is forbidden
	WaveTableBand(const WaveTableBand&) = delete;
	WaveTableBand& operator=(const WaveTableBand&) = delete;
	WaveTableBand(WaveTableBand&& other) noexcept
	    : maxPhaseIncrement(other.maxPhaseIncrement), fromCycleNumber(other.fromCycleNumber),
	      toCycleNumber(other.toCycleNumber), cycleSizeNoDuplicates(other.cycleSizeNoDuplicates),
	      cycleSizeMagnitude(other.cycleSizeMagnitude),
	      intendedForLinearInterpolation(other.intendedForLinearInterpolation),
	      dataAccessAddress(other.dataAccessAddress), data(std::exchange(other.data, nullptr)) {}
	WaveTableBand& operator=(WaveTableBand&& other) noexcept {
		std::swap(maxPhaseIncrement, other.maxPhaseIncrement);
		std::swap(fromCycleNumber, other.fromCycleNumber);
		std::swap(toCycleNumber, other.toCycleNumber);
		std::swap(cycleSizeNoDuplicates, other.cycleSizeNoDuplicates);
		std::swap(cycleSizeMagnitude, other.cycleSizeMagnitude);
		std::swap(intendedForLinearInterpolation, other.intendedForLinearInterpolation);
		std::swap(dataAccessAddress, other.dataAccessAddress);
		std::swap(data, other.data);
		return *this;
	}

	uint32_t maxPhaseIncrement = 0;
	int32_t fromCycleNumber = 0;
	int32_t toCycleNumber = 0;
	uint16_t cycleSizeNoDuplicates = 0;
	uint8_t cycleSizeMagnitude = 0;
	bool intendedForLinearInterpolation = false;
	int16_t* dataAccessAddress = nullptr;
	WaveTableBandData* data = nullptr; // Might be different than the above if memory has been shortened
};

class WaveTable final : public AudioFile {
public:
	WaveTable();
	~WaveTable() override;

	[[nodiscard]] size_t allocatedSize() const override { return sizeof(WaveTable); }

	uint32_t render(int32_t* outputBuffer, int32_t numSamples, uint32_t phaseIncrementNow, uint32_t phase,
	                bool doOscSync, uint32_t resetterPhase, uint32_t resetterPhaseIncrement,
	                int32_t resetterDivideByPhaseIncrement, uint32_t retriggerPhase, int32_t waveIndex,
	                int32_t waveIndexIncrement);
	/// @brief Build this WaveTable from a resident Sample's cluster data (the Sample→WaveTable conversion); takes its
	///        cycle size, channels and byte depth from the Sample.
	/// @pre Every audio cluster of @p sample is resident and pinned: the reads must not miss.
	Error setupFromSample(Sample& sample);
	/// @brief Build this WaveTable by reading audio data from a file, via a FileByteSource.
	///
	/// The header fields come from the parser (parseWaveTableHeader).
	/// @param source                 Byte source positioned to read the file's audio data.
	/// @param cycleSize              Sample count per cycle.
	/// @param audioDataStartPosBytes File offset of the start of the audio data.
	/// @param audioDataLengthBytes   Length of the audio data in bytes.
	/// @param byteDepth              Sample byte depth.
	/// @param rawDataFormat          Raw sample data format.
	/// @return Error::NONE on success.
	Error setupFromFile(FileByteSource& source, int32_t cycleSize, uint32_t audioDataStartPosBytes,
	                    uint32_t audioDataLengthBytes, int32_t byteDepth, RawDataFormat rawDataFormat);
	void deleteAllBandsAndData();
	void bandDataBeingStolen(WaveTableBandData* bandData);

	int32_t numCycles{};
	int32_t numCyclesMagnitude{};

	int32_t numCycleTransitionsNextPowerOf2{};
	int32_t numCycleTransitionsNextPowerOf2Magnitude{};
	int32_t waveIndexMultiplier{};
	deluge::fast_vector<WaveTableBand> bands{}; // Sorted ascending by maxPhaseIncrement

protected:
	void numReasonsIncreasedFromZero() override;
	void numReasonsDecreasedToZero(char const* errorCode) override;

private:
	/// @brief Shared implementation behind setupFromSample() and setupFromFile().
	///
	/// Exactly one of @p sample (in-memory source) or @p byteSource (file source) is used; the
	/// other parameters are ignored in the in-memory case (taken from the Sample). The two public
	/// entry points keep this nullable split off the API.
	/// @param sample                 In-memory source, or nullptr when reading from a file.
	/// @param rawFileCycleSize       Sample count per cycle (file-source case only).
	/// @param audioDataStartPosBytes File offset of the start of the audio data (file-source case only).
	/// @param audioDataLengthBytes   Length of the audio data in bytes (file-source case only).
	/// @param byteDepth              Sample byte depth (file-source case only).
	/// @param rawDataFormat          Raw sample data format (file-source case only).
	/// @param byteSource             Byte source, or nullptr when reading from an in-memory Sample.
	/// @return Error::NONE on success.
	Error setup(Sample* sample, int32_t rawFileCycleSize, uint32_t audioDataStartPosBytes,
	            uint32_t audioDataLengthBytes, int32_t byteDepth, RawDataFormat rawDataFormat,
	            FileByteSource* byteSource);

	void doRenderingLoop(int32_t* __restrict__ thisSample, int32_t const* bufferEnd, int32_t firstCycleNumber,
	                     WaveTableBand* __restrict__ bandHere, uint32_t phase, uint32_t phaseIncrement,
	                     uint32_t waveIndexScaled, int32_t waveIndexIncrementScaled,
	                     const int16_t* __restrict__ kernel);

	void doRenderingLoopSingleCycle(int32_t* __restrict__ thisSample, int32_t const* bufferEnd,
	                                WaveTableBand* __restrict__ bandHere, uint32_t phase, uint32_t phaseIncrement,
	                                const int16_t* __restrict__ kernel);
};
