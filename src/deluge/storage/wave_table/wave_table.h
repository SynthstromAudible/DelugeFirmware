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
#include "dsp-next/fixed/wavetable/wave_table_data.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_file.h"
#include "storage/wave_table/wave_table_band_data.h"
#include "util/containers.h"
#include <utility>

class Sample;
class DeserializerByteSource;

// The render fields (maxPhaseIncrement, cycle geometry, dataAccessAddress) live in the dsp-next
// base @ref deluge::dsp::fixed::WaveTableBand — the prefix the renderer sees via WaveTableData;
// this class adds only the load/steal lifecycle. The base is at offset 0, so the bands array's
// 32-bit key (maxPhaseIncrement) and the view's strided access both read it correctly.
class WaveTableBand : public deluge::dsp::fixed::WaveTableBand {
public:
	// Value-initialize the dsp-next render-field base (the fast_vector grows by default-constructing
	// slots, and the firmware historically zeroed these via default member initializers).
	WaveTableBand() : deluge::dsp::fixed::WaveTableBand{} {}
	~WaveTableBand();

	// The destructor releases `data`, so ownership must transfer on move and copying is forbidden.
	WaveTableBand(const WaveTableBand&) = delete;
	WaveTableBand& operator=(const WaveTableBand&) = delete;
	WaveTableBand(WaveTableBand&& other) noexcept
	    : deluge::dsp::fixed::WaveTableBand(other), // POD render-field prefix (incl. dataAccessAddress)
	      data(std::exchange(other.data, nullptr)) {}
	WaveTableBand& operator=(WaveTableBand&& other) noexcept {
		std::swap(static_cast<deluge::dsp::fixed::WaveTableBand&>(*this),
		          static_cast<deluge::dsp::fixed::WaveTableBand&>(other));
		std::swap(data, other.data);
		return *this;
	}

	WaveTableBandData* data = nullptr; // Might be different than dataAccessAddress if memory has been shortened
};

class WaveTable final : public AudioFile {
public:
	WaveTable();
	~WaveTable() override;

	[[nodiscard]] size_t allocatedSize() const override { return sizeof(WaveTable); }

	uint32_t render(int32_t* outputBuffer, int32_t numSamples, uint32_t phaseIncrementNow, uint32_t phase,
	                bool doOscSync, uint32_t resetterPhase, uint32_t resetterPhaseIncrement, uint32_t retriggerPhase,
	                int32_t waveIndex, int32_t waveIndexIncrement);
	// Build this WaveTable from an already-in-memory Sample (the Sample→WaveTable conversion). Reads the
	// Sample's cluster data; takes its cycle size / channels / byte depth from the Sample.
	Error setupFromSample(Sample& sample);
	// Build this WaveTable by reading audio data from a file, via the deserializer-backed byte source. The
	// header fields come from the parser (parseWaveTableHeader).
	Error setupFromFile(DeserializerByteSource& source, int32_t cycleSize, uint32_t audioDataStartPosBytes,
	                    uint32_t audioDataLengthBytes, int32_t byteDepth, RawDataFormat rawDataFormat);
	void deleteAllBandsAndData();
	void bandDataBeingStolen(WaveTableBandData* bandData);

	/// A read-only, storage-independent view of this wavetable for the dsp-next renderer (the bands
	/// addressed by base + stride, so storage's container stays its own concern).
	[[nodiscard]] deluge::dsp::fixed::WaveTableData view();

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
	// Shared implementation behind setupFromSample / setupFromFile. Exactly one of `sample` (in-memory source)
	// or `byteSource` (file source) is used; the other parameters are ignored in the in-memory case (taken from
	// the Sample). The two public entry points keep this nullable split off the API.
	Error setup(Sample* sample, int32_t rawFileCycleSize, uint32_t audioDataStartPosBytes,
	            uint32_t audioDataLengthBytes, int32_t byteDepth, RawDataFormat rawDataFormat,
	            DeserializerByteSource* byteSource);
};
