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

/// @brief log2 of the most samples scanClusterPeak() reads per call; longer spans are subsampled.
constexpr int32_t kSamplesToReadPerColMagnitude = 9;

/// @brief Minimum and maximum of a run of raw 32-bit-aligned sample values.
struct WaveformPeak {
	int32_t min; ///< Smallest value read.
	int32_t max; ///< Largest value read.
};

/// @brief Finds the subsampled min/max over part of a Cluster's raw audio data.
///
/// Reads at most about 2^kSamplesToReadPerColMagnitude samples, striding over the rest. For stereo the stride is
/// forced odd so both channels get sampled. Each read is a 32-bit load offset by `byteDepth - 4`, so it lands on a
/// sample's most significant bytes. For 8/16/24-bit data that indexes slightly before @p startByte, which is valid
/// because Clusters keep padding bytes before their data.
///
/// @param clusterData Cluster::data of the Cluster being read.
/// @param startByte First byte to read, relative to @p clusterData. Should be frame-aligned.
/// @param endByte Byte after the last one to read, relative to @p clusterData.
/// @param byteDepth Bytes per sample (1-4).
/// @param numChannels 1 or 2. Both channels are counted as samples.
/// @returns The peaks found. If the range is empty, min is INT32_MAX and max is INT32_MIN.
WaveformPeak scanClusterPeak(const char* clusterData, int32_t startByte, int32_t endByte, int32_t byteDepth,
                             int32_t numChannels);

/// @brief Reduces a 32-bit peak to the 8-bit form stored in SampleCluster::minValue / maxValue.
/// @param value Full-scale 32-bit sample value.
/// @returns The top 8 bits, rounded toward zero.
int8_t toCoarsePeak(int32_t value);

/// @brief Finds where the audio ends within the last Cluster that holds any.
///
/// Rounds up, so audio that ends exactly on a Cluster boundary gives a full Cluster rather than 0.
///
/// @param numValidBytes Length of the audio data in bytes.
/// @param audioDataStartPosBytes Offset of the audio data within the file.
/// @param clusterSize Cluster::size. Must be a power of two.
/// @returns The byte after the last audio byte, relative to the start of that Cluster: 1 to @p clusterSize.
int32_t lastAudioClusterEndByte(uint64_t numValidBytes, uint32_t audioDataStartPosBytes, int32_t clusterSize);

/// @brief Finds a Cluster's byte offset within its file, without overflowing for files over 2 GB.
/// @param clusterIndex Index of the Cluster within the file.
/// @param sizeMagnitude Cluster::size_magnitude.
/// @returns The offset of the Cluster's first byte.
int64_t clusterStartByte(int32_t clusterIndex, int32_t sizeMagnitude);

/// @brief Finds where the first whole audio frame starts within a Cluster.
///
/// Frames are laid out from @p audioDataStartPosBytes, so with non-power-of-two frame sizes (e.g. 24-bit) a Cluster
/// usually begins part-way through a frame.
///
/// @param clusterStartByteAbs The Cluster's byte offset within the file, from clusterStartByte().
/// @param audioDataStartPosBytes Offset of the audio data within the file.
/// @param frameSize Bytes per frame (numChannels * byteDepth).
/// @returns Offset of the first whole frame, relative to the Cluster's start. For a Cluster that still holds the file
///          header, this is where the audio data begins.
int32_t firstFrameStartWithinCluster(int64_t clusterStartByteAbs, uint32_t audioDataStartPosBytes, int32_t frameSize);

/// @brief Works out whether a scroll moved the view by a whole number of columns.
///
/// AudioClip converts its scroll from ticks to samples with integer division. So scrolling by one column can land
/// up to one sample per column moved away from an exact multiple of @p zoomSamples. That much slack is accepted, as
/// long as it stays under half a column.
///
/// @param deltaSamples New scroll minus old scroll, in samples.
/// @param zoomSamples Samples per column. Must be positive.
/// @param displayWidth Number of columns. Moves further than this leave nothing to reuse.
/// @returns The number of columns moved (positive is rightwards), or std::nullopt if it wasn't a whole-column move
///          of 1 to @p displayWidth columns.
std::optional<int32_t> wholeColumnScrollShift(int64_t deltaSamples, int64_t zoomSamples, int32_t displayWidth);
