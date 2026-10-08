/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// @file
/// @brief Pure arithmetic used by WaveformRenderer to find per-column waveform peaks.
///
/// Nothing here touches the engine or hardware, so it's covered by the host unit tests
/// (tests/unit/waveform_peak_math_tests.cpp).

#pragma once

#include <cstdint>
#include <optional>

namespace deluge::gui::waveform {

/// @brief log2 of the most samples scanPeak() reads per call; longer spans are subsampled.
constexpr int32_t kMaxScanSamplesLog2 = 9;

/// @brief Minimum and maximum of a run of raw 32-bit-aligned sample values.
struct Peak {
	int32_t min; ///< Smallest value read.
	int32_t max; ///< Largest value read.
};

/// @brief Finds the subsampled min/max over part of a Cluster's raw audio data.
///
/// Reads at most about 2^kMaxScanSamplesLog2 samples, striding over the rest. For stereo the stride is forced odd so
/// both channels get sampled. Each read is a 32-bit load offset by `byte_depth - 4`, so it lands on a sample's most
/// significant bytes. For 8/16/24-bit data that indexes slightly before @p start, which is valid because Clusters keep
/// padding bytes before their data.
///
/// @param data Cluster::data of the Cluster being read.
/// @param start First byte to read, relative to @p data. Should be frame-aligned.
/// @param end Byte after the last one to read, relative to @p data.
/// @param byte_depth Bytes per sample (1-4).
/// @param num_channels 1 or 2. Both channels are counted as samples.
/// @returns The peaks found. If the range is empty, min is INT32_MAX and max is INT32_MIN.
Peak scanPeak(const char* data, int32_t start, int32_t end, int32_t byte_depth, int32_t num_channels);

/// @brief Reduces a 32-bit peak to the 8-bit form stored in SampleCluster::minValue / maxValue.
/// @param value Full-scale 32-bit sample value.
/// @returns The top 8 bits, rounded toward zero.
int8_t coarsePeak(int32_t value);

/// @brief Finds where the audio ends within the last Cluster that holds any.
///
/// Rounds up, so audio that ends exactly on a Cluster boundary gives a full Cluster rather than 0.
///
/// @param audio_bytes Length of the audio data in bytes.
/// @param audio_start Offset of the audio data within the file.
/// @param cluster_size Cluster::size. Must be a power of two.
/// @returns The byte after the last audio byte, relative to the start of that Cluster: 1 to @p cluster_size.
int32_t lastClusterEnd(uint64_t audio_bytes, uint32_t audio_start, int32_t cluster_size);

/// @brief Finds a Cluster's byte offset within its file, without overflowing for files over 2 GB.
/// @param index Index of the Cluster within the file.
/// @param size_log2 Cluster::size_magnitude.
/// @returns The offset of the Cluster's first byte.
int64_t clusterStart(int32_t index, int32_t size_log2);

/// @brief Finds where the first whole audio frame starts within a Cluster.
///
/// Frames are laid out from @p audio_start, so with non-power-of-two frame sizes (e.g. 24-bit) a Cluster usually begins
/// part-way through a frame.
///
/// @param cluster_start The Cluster's byte offset within the file, from clusterStart().
/// @param audio_start Offset of the audio data within the file.
/// @param frame_size Bytes per frame (num_channels * byte_depth).
/// @returns Offset of the first whole frame, relative to the Cluster's start. For a Cluster that still holds the file
///          header, this is where the audio data begins.
int32_t firstFrameOffset(int64_t cluster_start, uint32_t audio_start, int32_t frame_size);

/// @brief Works out whether a scroll moved the view by a whole number of columns.
///
/// AudioClip converts its scroll from ticks to samples with integer division. So scrolling by one column can land up to
/// one sample per column moved away from an exact multiple of @p zoom. That much slack is accepted, as long as it stays
/// under half a column.
///
/// @param delta New scroll minus old scroll, in samples.
/// @param zoom Samples per column. Must be positive.
/// @param width Number of columns. Moves further than this leave nothing to reuse.
/// @returns The number of columns moved (positive is rightwards), or std::nullopt if it wasn't a whole-column move of 1
///          to @p width columns.
std::optional<int32_t> columnShift(int64_t delta, int64_t zoom, int32_t width);

} // namespace deluge::gui::waveform
