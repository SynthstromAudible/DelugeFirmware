/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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
#include "gui/colour/colour.h"
#include <array>
#include <cstdint>
#include <optional>

class Cluster;
class Sample;
class MultisampleRange;
class SampleRecorder;
struct WaveformRenderData;

struct MarkerColumn {
	int32_t pos; // Unscrolled
	int32_t colOnScreen;
};

// This class provides low-level rendering functions for the waveform only

class WaveformRenderer {
public:
	WaveformRenderer();

	bool renderFullScreen(Sample* sample, uint64_t xScroll, uint64_t xZoom,
	                      RGB thisImage[][kDisplayWidth + kSideBarWidth], WaveformRenderData* data,
	                      SampleRecorder* recorder = nullptr, std::optional<RGB> rgb = std::nullopt,
	                      bool reversed = false, int32_t xEnd = kDisplayWidth);
	/// @brief Renders a Sample's waveform as one row of brightness, as song row view shows it.
	///
	/// Doesn't wait on the SD card unless @p recorder is set: Clusters not in memory are enqueued, and their columns
	/// draw black until they've loaded. Everything that is available is always drawn.
	///
	/// @returns false if some columns are still waiting on the card, so the caller should render the row again later.
	bool renderAsSingleRow(Sample* sample, int64_t xScroll, uint64_t xZoom, RGB* thisImage, WaveformRenderData* data,
	                       SampleRecorder* recorder, RGB rgb, bool reversed, int32_t xStart, int32_t xEnd);
	void renderOneCol(Sample* sample, int32_t xDisplay, RGB thisImage[][kDisplayWidth + kSideBarWidth],
	                  WaveformRenderData* data, bool reversed = false, std::optional<RGB> rgb = std::nullopt);
	void renderOneColForCollapseAnimation(int32_t xDisplay, int32_t xDisplayOutput, int32_t maxPeakFromZero,
	                                      int32_t progress, RGB thisImage[][kDisplayWidth + kSideBarWidth],
	                                      WaveformRenderData* data, std::optional<RGB> rgb, bool reversed,
	                                      int32_t valueCentrePoint, int32_t valueSpan);
	void renderOneColForCollapseAnimationZoomedOut(int32_t xDisplayWaveformLeftEdge, int32_t xDisplayWaveformRightEdge,
	                                               int32_t xDisplayOutput, int32_t maxPeakFromZero, int32_t progress,
	                                               RGB thisImage[][kDisplayWidth + kSideBarWidth],
	                                               WaveformRenderData* data, std::optional<RGB> rgb, bool reversed,
	                                               int32_t valueCentrePoint, int32_t valueSpan);
	/// @brief Fills in @p data's per-column min/max peaks for any columns in [@p xStart, @p xEnd) not already cached.
	///
	/// Cached columns are kept across calls while the zoom and waveform length stay the same. A whole-column scroll
	/// slides them across, and only the newly exposed columns are worked out.
	///
	/// @param sample The Sample to read.
	/// @param xScroll Sample position of the left edge of column 0.
	/// @param xZoom Samples per column.
	/// @param data Per-column cache to fill in. Set its xScroll to -1 to force everything to be worked out again.
	/// @param recorder The SampleRecorder still writing @p sample, if any. Its captured length is used.
	/// @param xStart First column to fill in.
	/// @param xEnd Column after the last one to fill in.
	/// @param clusterLoadInstruction What to do about Clusters not in memory. CLUSTER_LOAD_IMMEDIATELY reads them from
	///        the card now. CLUSTER_ENQUEUE queues them for loading and leaves their columns un-investigated. Don't use
	///        CLUSTER_ENQUEUE while @p recorder is set: the reason it holds on @p sample would break
	///        SampleRecorder::abort().
	/// @returns false if any column couldn't be worked out yet (Cluster enqueued, or couldn't load it), so the caller
	///          should try again later.
	bool findPeaksPerCol(Sample* sample, int64_t xScroll, uint64_t xZoom, WaveformRenderData* data,
	                     SampleRecorder* recorder = nullptr, int32_t xStart = 0, int32_t xEnd = kDisplayWidth,
	                     int32_t clusterLoadInstruction = CLUSTER_LOAD_IMMEDIATELY);

	/// @brief Lets go of enqueued Clusters that have finished loading, or that never will because they're unloadable.
	///
	/// Cheap. findPeaksPerCol() calls it, and AudioFileManager::slowRoutine() does too, so Clusters are let go of even
	/// once nothing is rendering waveforms.
	void releaseFinishedClusterLoads();

	/// @brief Lets go of every enqueued Cluster and its Sample, loaded or not.
	///
	/// Must be called before deleting Samples without checking their reasons, as
	/// AudioFileManager::deleteAnyTempRecordedSamplesFromMemory() does.
	void releaseAllClusterLoads();

	int8_t collapseAnimationToWhichRow;

private:
	/// @brief A Cluster that findPeaksPerCol() enqueued and is waiting on.
	///
	/// We hold one reason on the Cluster, which keeps it in the loading queue, and one on its Sample, so the Sample
	/// can't be deleted while we still point at it.
	struct PendingClusterLoad {
		Cluster* cluster; ///< The enqueued Cluster.
		Sample* sample;   ///< The Sample it belongs to.
	};

	/// @brief Most Clusters we'll wait on at once.
	///
	/// Keeps a long scroll from flooding the loading queue ahead of playback. Columns that don't fit are retried on a
	/// later render.
	static constexpr int32_t kMaxPendingClusterLoads = 16;
	std::array<PendingClusterLoad, kMaxPendingClusterLoads> pendingClusterLoads{}; ///< Unordered; first n are live.
	int32_t numPendingClusterLoads = 0; ///< Number of live entries in pendingClusterLoads.

	/// @brief Takes over the caller's reason on an enqueued Cluster until it has loaded.
	/// @param sample The Sample @p cluster belongs to.
	/// @param cluster An enqueued, not yet loaded Cluster that the caller holds one reason on.
	/// @returns false if there was no room. The caller's reason has then been removed, so @p cluster may no longer
	///          exist.
	bool holdUntilLoaded(Sample* sample, Cluster* cluster);

	/// @brief Removes the reasons held by one pending entry and drops that entry.
	/// @param i Index into pendingClusterLoads. The last entry is moved into its place.
	void releasePendingClusterLoad(int32_t i);

	int32_t getColBrightnessForSingleRow(int32_t xDisplay, int32_t maxPeakFromZero, WaveformRenderData* data);
	void getColBarPositions(int32_t xDisplay, WaveformRenderData* data, int32_t* min24, int32_t* max24,
	                        int32_t valueCentrePoint, int32_t valueSpan);
	void drawColBar(int32_t xDisplay, int32_t min24, int32_t max24, RGB thisImage[][kDisplayWidth + kSideBarWidth],
	                int32_t brightness = 128, std::optional<RGB> rgb = std::nullopt);
	void renderOneColForCollapseAnimationInterpolation(int32_t xDisplayOutput, int32_t min24, int32_t max24,
	                                                   int32_t singleSquareBrightness, int32_t progress,
	                                                   RGB thisImage[][kDisplayWidth + kSideBarWidth],
	                                                   std::optional<RGB> rgb);
};

extern WaveformRenderer waveformRenderer;
