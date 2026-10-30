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
#include "storage/wave_table/band_samples.h"
#include "util/containers.h"

class Sample;
class FileByteSource;
class WaveTableCycleReader;

// The render fields (maxPhaseIncrement, cycle geometry, dataAccessAddress) live in the dsp-next
// base @ref deluge::dsp::fixed::WaveTableBand — the prefix the renderer sees via WaveTableData;
// this class adds ownership of the band's samples. The base is at offset 0, so the bands array's
// 32-bit key (maxPhaseIncrement) and the view's strided access both read it correctly.
class WaveTableBand : public deluge::dsp::fixed::WaveTableBand {
public:
	// Value-initialize the dsp-next render-field base: the fast_vector grows by default-constructing slots.
	WaveTableBand() : deluge::dsp::fixed::WaveTableBand{} {}

	BandSamples samples; ///< Owns the memory dataAccessAddress points at.
};
static_assert(sizeof(WaveTableBand) == sizeof(deluge::dsp::fixed::WaveTableBand) + sizeof(void*));

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

	/// A read-only, storage-independent view of this wavetable for the dsp-next renderer (the bands
	/// addressed by base + stride, so storage's container stays its own concern).
	[[nodiscard]] deluge::dsp::fixed::WaveTableData view();

	int32_t numCycles{};
	int32_t numCycleTransitionsNextPowerOf2Magnitude{};
	int32_t waveIndexMultiplier{};
	deluge::fast_vector<WaveTableBand> bands{}; // Sorted ascending by maxPhaseIncrement

private:
	/// @brief Builds the bands from @p reader's cycles: the shared half of setupFromSample() and setupFromFile().
	/// @param reader              The source's cycles, not yet opened.
	/// @param sourceCycleSize     Samples per source cycle, already clamped to @p sourceLengthSamples.
	/// @param sourceLengthSamples The source's length in samples.
	/// @return Error::NONE on success; on any error the table keeps no bands.
	Error setup(WaveTableCycleReader& reader, int32_t sourceCycleSize, uint32_t sourceLengthSamples);
};
