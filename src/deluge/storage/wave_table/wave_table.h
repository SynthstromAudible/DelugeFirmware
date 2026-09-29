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
class FileByteSource;

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
};
