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

/// @file
/// @brief The waveform *scan*: turning a `Sample`'s clusters into per-column min/max peaks, and the
///        two pure derivations a renderer needs from them.
///
/// Moved out of `WaveformRenderer` (`gui/waveform/waveform_renderer.cpp`) unchanged. The split is
/// the one `src/ui/grid/waveform.h` already names in its own header: *"the scan that turns a
/// `Sample`'s clusters into per-column peaks reads `Sample`, `SampleRecorder` and the card, and
/// stays outside this module"*. This is that outside. Nothing here draws, and nothing here includes
/// `gui/` or `hid/`, so `model/sample/waveform_scan.h` is reachable from `src/scenes` where
/// `gui/waveform/waveform_renderer.h` is not (`check_layer_rules.py`, `scenes-includes-outside`).
///
/// `WaveformRenderer`'s remaining members -- `renderFullScreen`, `renderAsSingleRow`, `renderOneCol`
/// and the two collapse-animation entry points -- are thin wrappers over these plus `drawColBar`,
/// and stay in `gui/` because they write into a `PadLEDs`-shaped `RGB[][]`.

#include "definitions_cxx.hpp"
#include "model/sample/waveform_render_data.h"

#include <cstdint>

class Sample;
class SampleRecorder;

namespace deluge::sample {

/// @brief Fill in @p data's per-column min/max peaks for columns `[xStart, xEnd)`.
///
/// `WaveformRenderer::findPeaksPerCol` (`gui/waveform/waveform_renderer.cpp:263` before the move).
/// Reuses whatever @p data already holds: an unchanged zoom and a whole-column scroll delta slide
/// the cache across (issue #4460) rather than re-reading, and a column already marked
/// `COL_STATUS_INVESTIGATED` is skipped outright.
///
/// @warning **This can read the card**, through `deluge::sample::SampleFrameReader`, and pumps
///          `AudioEngine::routineWithClusterLoading()` between columns. A caller off the storage
///          owner must expect a partial result rather than a stall -- that is what the return value
///          reports.
///
/// @param sample         The sample to scan.
/// @param xScrollSamples Left edge, in samples.
/// @param xZoomSamples   Samples per column.
/// @param data           The per-view cache to fill; its `xScroll`/`xZoom`/`validLengthSamples` are
///                       updated to match this call.
/// @param recorder       The in-progress recorder writing this sample, or null for a finished file.
/// @param xStart         First column to consider.
/// @param xEnd           One past the last column to consider.
/// @return False if some cluster could not be loaded (often not all of them) -- e.g. because the
///         call is inside the card routine. The columns that did load are still valid.
bool find_peaks_per_col(Sample* sample, int64_t xScrollSamples, uint64_t xZoomSamples, WaveformRenderData* data,
                        SampleRecorder* recorder = nullptr, int32_t xStart = 0, int32_t xEnd = kDisplayWidth);

/// @brief One column's brightness for the single-row render, normalised against the sample's peak.
///
/// `WaveformRenderer::getColBrightnessForSingleRow` (`waveform_renderer.cpp:152` before the move).
/// @param xDisplay        Column index into @p data.
/// @param maxPeakFromZero `Sample::getMaxPeakFromZero()`.
/// @param data            The filled cache.
/// @return 0..256 inclusive -- the range `ui::grid::kWaveformMaxBrightness` names, and deliberately
///         one past what a `uint8_t` holds.
int32_t col_brightness_for_single_row(int32_t xDisplay, int32_t maxPeakFromZero, const WaveformRenderData* data);

/// @brief One column's vertical bar extent, in 8.24 fixed-point pad units.
///
/// `WaveformRenderer::getColBarPositions` (`waveform_renderer.cpp:718` before the move), including
/// its "ensure we're going to draw at least 1 pixel's width" widening -- which `ui::grid::PeakCol`'s
/// docstring already records as belonging to the scan rather than the render.
///
/// @param xDisplay         Column index into @p data.
/// @param data             The filled cache.
/// @param[out] min24       Bottom of the bar.
/// @param[out] max24       Top of the bar.
/// @param valueCentrePoint `Sample::getFoundValueCentrePoint()`.
/// @param valueSpan        `Sample::getValueSpan()`.
void col_bar_positions(int32_t xDisplay, const WaveformRenderData* data, int32_t* min24, int32_t* max24,
                       int32_t valueCentrePoint, int32_t valueSpan);

/// @brief Background "waveform overview" pre-scan: investigate one whole cluster's min/max.
///
/// `WaveformRenderer::investigateWholeCluster` (`waveform_renderer.cpp:569` before the move).
/// Caches the result in the Sample's overview cache entry (int8 + investigated), so the zoomed-out
/// single-row render path finds it already done and never has to load clusters synchronously while
/// scrolling.
/// @param sample       The sample whose cluster to investigate.
/// @param clusterIndex Index of the cluster to investigate.
/// @return True if the cluster is now investigated (or there was nothing to do); false if its data
///         couldn't be loaded.
bool investigate_whole_cluster(Sample* sample, int32_t clusterIndex);

/// @brief Advance @p sample's background overview pre-scan by up to @p maxClusters clusters.
///
/// `WaveformRenderer::advanceOverviewScan` (`waveform_renderer.cpp:665` before the move).
/// @param sample      The sample to advance.
/// @param maxClusters Budget of previously-uninvestigated clusters for this call.
/// @return True while more clusters remain to scan, false once the whole sample is covered.
bool advance_overview_scan(Sample* sample, int32_t maxClusters);

} // namespace deluge::sample
