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
/// @brief A sample's waveform columns, read from its peak index, and the background scan that fills the index.
///
/// Both read `Sample` and its frames through the frame reader, so they stay out of the neutral
/// `src/ui/views/waveform/`, whose types they answer in. The host's waveforms port and the session, arranger and audio
/// clip views all read through `waveform_columns`; `WaveformRenderer` draws the answers into a `PadLEDs`-shaped
/// `RGB[][]`.

#include "definitions_cxx.hpp"
#include "ui/views/waveform/peaks.h"

#include <cstdint>
#include <optional>

class Sample;

namespace deluge::sample {

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

/// @brief The background peak scan: fold the first block of @p sample's peak index that is not complete, reading its
///        frames through the frame reader (which requests the fill of a cluster that is not resident).
///
/// A sample a recorder still holds is skipped: its recorder fills its index. Bails without reading while the card or
/// the audio routine is busy, as the overview scan does.
/// @param sample The sample to advance.
/// @return True while blocks remain, false once its index is complete (or it has none).
bool advance_peak_scan(Sample& sample);

} // namespace deluge::sample
