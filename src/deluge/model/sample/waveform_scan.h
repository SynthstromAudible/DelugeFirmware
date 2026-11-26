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
/// @brief The waveform scan: the legacy column read, which turns a `Sample`'s clusters into each column's peak, and
///        the legacy views' adapter, which keeps the columns between frames in a
///        `deluge::app::ports::waveforms::PeakCache`.
///
/// The scan reads `Sample`, `SampleRecorder` and the card, so it stays out of the neutral `src/ui/views/waveform/`,
/// whose types it answers in. The host's waveforms port reads through `read_columns`, and the session, arranger and
/// audio clip views through `find_peaks_per_col`. `WaveformRenderer` draws the answers into a `PadLEDs`-shaped
/// `RGB[][]`.

#include "app/ports/waveforms/peak_cache.h"
#include "definitions_cxx.hpp"

#include <cstdint>
#include <optional>

class Sample;
class SampleRecorder;

namespace deluge::sample {

/// @brief Read the columns @p wanted of @p sample at @p at: each column's peak, PAST_END where it lies outside the
///        frames the sample holds now (for a take still being written, until the take has captured all of it), or
///        PENDING where a cluster under it could not be read now. Then update the sample's running range: from
///        @p recorder's while a take is being written, else from the peaks just read. A column a cache keeps was taken
///        into the range when it was read, and the range only widens.
/// @param sample   The sample to read.
/// @param recorder The recorder writing @p sample, or null for a finished file.
/// @param at       The window. A zoom below 1 answers every column PAST_END.
/// @param wanted   The columns to read; the others are answered PENDING and are not read.
/// @return The answers.
/// @warning This can read the card, through `deluge::sample::SampleFrameReader`. A caller off the storage owner gets
///          PENDING columns rather than a stall.
[[nodiscard]] deluge::ui::views::waveform::PeakRead read_columns(Sample& sample, SampleRecorder* recorder,
                                                                 deluge::ui::views::waveform::Window at,
                                                                 deluge::app::ports::waveforms::ColumnMask wanted);

/// @brief The sample's 16 columns at @p at, read from its peak index: each column's exact peak over every frame it
///        covers. PAST_END outside the frames the sample holds (a take: the frames captured, and a column not wholly
///        captured yet); PENDING where a block under it is not scanned yet, or the frames at its ends are not resident.
///
/// A pure function of the sample's data: it keeps no state of its own. Frames read at a column's ends are folded back
/// into the index, so the next read finds them kept.
/// @param sample   The sample.
/// @param captured The frames a recorder has captured, or nullopt for a finished file.
/// @param at       The window. A zoom below 1 answers every column PAST_END.
/// @return The answers, peaks at 32-bit scale.
[[nodiscard]] deluge::ui::views::waveform::PeakRead waveform_columns(Sample& sample, std::optional<uint64_t> captured,
                                                                     deluge::ui::views::waveform::Window at);

/// @brief The legacy views' scan: @p cache brought to @p at for [@p first, @p last), reading through
///        @ref read_columns what it lacks.
/// @param sample   The sample to read.
/// @param at       The window.
/// @param cache    The view's columns, kept between frames.
/// @param recorder The recorder writing @p sample, or null for a finished file.
/// @param first    The first column to read.
/// @param last     One past the last.
/// @return The cache's columns; a column in the range that could not be read now is PENDING.
deluge::ui::views::waveform::PeakRead find_peaks_per_col(Sample& sample, deluge::ui::views::waveform::Window at,
                                                         deluge::app::ports::waveforms::PeakCache& cache,
                                                         SampleRecorder* recorder = nullptr, int32_t first = 0,
                                                         int32_t last = deluge::ui::views::waveform::kColumns);

/// @brief Background "waveform overview" pre-scan: investigate one whole cluster's min/max.
///
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
/// @param sample      The sample to advance.
/// @param maxClusters Budget of previously-uninvestigated clusters for this call.
/// @return True while more clusters remain to scan, false once the whole sample is covered.
bool advance_overview_scan(Sample* sample, int32_t maxClusters);

/// @brief The background peak scan: fold the first block of @p sample's peak index that is not complete, reading its
///        frames through the frame reader (which requests the fill of a cluster that is not resident).
///
/// A sample a recorder still holds is skipped: its recorder fills its index. Bails without reading while the card or
/// the audio routine is busy, as the overview scan does.
/// @param sample The sample to advance.
/// @return True while blocks remain, false once its index is complete (or it has none).
bool advance_peak_scan(Sample& sample);

} // namespace deluge::sample
