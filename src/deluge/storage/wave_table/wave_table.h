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
#include "util/container/array/ordered_resizeable_array.h"

class Sample;
class WaveTableReader;

class WaveTableBand {
public:
	~WaveTableBand();
	uint32_t maxPhaseIncrement;
	int32_t fromCycleNumber;
	int32_t toCycleNumber;
	uint16_t cycleSizeNoDuplicates;
	uint8_t cycleSizeMagnitude;
	bool intendedForLinearInterpolation;
	int16_t* dataAccessAddress;
	WaveTableBandData* data; // Might be different than the above if memory has been shortened
};

class WaveTable final : public AudioFile {
public:
	WaveTable();
	~WaveTable() override;
	int32_t cloneFromSample(Sample* sample);
	uint32_t render(int32_t* outputBuffer, int32_t numSamples, uint32_t phaseIncrementNow, uint32_t phase,
	                bool doOscSync, uint32_t resetterPhase, uint32_t resetterPhaseIncrement,
	                int32_t resetterDivideByPhaseIncrement, uint32_t retriggerPhase, int32_t waveIndex,
	                int32_t waveIndexIncrement);
	Error setup(Sample* sample, int32_t nativeNumSamplesPerCycle = 0, uint32_t audioDataStartPosBytes = 0,
	            uint32_t audioDataLengthBytes = 0, int32_t byteDepth = 0,
	            RawDataFormat rawDataFormat = RawDataFormat::NATIVE, WaveTableReader* reader = nullptr);
	void deleteAllBandsAndData();
	void bandDataBeingStolen(WaveTableBandData* bandData);

	int32_t numCycles;
	int32_t numCyclesMagnitude;

	int32_t numCycleTransitionsNextPowerOf2;
	int32_t numCycleTransitionsNextPowerOf2Magnitude;
	int32_t waveIndexMultiplier;
	OrderedResizeableArrayWith32bitKey bands;

protected:
	void numReasonsIncreasedFromZero() override;
	void numReasonsDecreasedToZero(char const* errorCode) override;

private:
	void doRenderingLoop(int32_t* __restrict__ thisSample, int32_t const* bufferEnd, int32_t firstCycleNumber,
	                     WaveTableBand* __restrict__ bandHere, uint32_t phase, uint32_t phaseIncrement,
	                     uint32_t waveIndexScaled, int32_t waveIndexIncrementScaled,
	                     const int16_t* __restrict__ kernel);

	void doRenderingLoopSingleCycle(int32_t* __restrict__ thisSample, int32_t const* bufferEnd,
	                                WaveTableBand* __restrict__ bandHere, uint32_t phase, uint32_t phaseIncrement,
	                                const int16_t* __restrict__ kernel);
};
