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

#pragma once

#include "deluge_storage_sample_peaks.h"
#include "model/sample/sample.h"
#include "storage/cluster/cluster.h"
#include "test/peaks_test_support.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <utility>

namespace peak_seed {

/// @brief Fold a fixture sample's peaks into its index: each 256-frame block whose frames all lie in clusters below
///        `upto` holds the fold of `peak_of(c)` over the clusters `c` its frames lie in. A block already complete keeps
///        its place in the pyramid (only a kept level-0 value is filled).
/// @param s       The sample, its index made.
/// @param peak_of Cluster index → {min, max} at 8-bit scale.
/// @param upto    The first cluster not folded.
template <typename PeakOf>
void fold_peaks(Sample& s, PeakOf peak_of, int32_t upto = INT32_MAX) {
	DelugePeakIndex* peaks = s.peaks();
	const uint64_t stride = uint64_t{s.byteDepth} * s.numChannels;
	const auto log2 = static_cast<uint32_t>(Cluster::size_magnitude);
	for (uint64_t first = 0; first < s.lengthInSamples; first += 256) {
		const uint64_t end = std::min<uint64_t>(first + 256, s.lengthInSamples);
		const uint64_t c0 = (s.audioDataStartPosBytes + first * stride) >> log2;
		const uint64_t c1 = (s.audioDataStartPosBytes + end * stride - 1) >> log2;
		if (c1 >= static_cast<uint64_t>(upto)) {
			break;
		}
		DelugePeak p{.min = INT16_MAX, .max = INT16_MIN};
		for (uint64_t c = c0; c <= c1; c++) {
			const auto [lo, hi] = peak_of(static_cast<int32_t>(c));
			p = {.min = std::min<int16_t>(p.min, static_cast<int16_t>(lo * 256)),
			     .max = std::max<int16_t>(p.max, static_cast<int16_t>(hi * 256))};
		}
		deluge_peaks_fold_block(peaks, first >> 8, p);
	}
}

/// @brief A fixture sample's peaks, which have no frames behind them: a fresh index of the sample's length with every
///        level-0 value kept (so a column on block boundaries reads no frame), the blocks of clusters below `upto`
///        folded as `fold_peaks` folds them, and the range widened to the full 8-bit scale the fixtures were drawn at.
/// @param s       The sample: its length, byte depth, channels and audio start set.
/// @param peak_of Cluster index → {min, max} at 8-bit scale.
/// @param upto    The first cluster not folded: the rest stay unscanned.
template <typename PeakOf>
void seed_peaks(Sample& s, PeakOf peak_of, int32_t upto = INT32_MAX) {
	s.makePeaks(s.lengthInSamples, false, s.lengthInSamples);
	deluge_peaks_ensure_level0(s.peaks(), 0, s.lengthInSamples);
	fold_peaks(s, peak_of, upto);
	deluge_peaks_widen_range(s.peaks(), DelugePeak{.min = -128 * 256, .max = 127 * 256});
}

/// @brief A fixture sample's peaks, every block the same: for a sample too long to seed block by block.
/// @param s    The sample: its length set.
/// @param peak The peak every block holds, at 8-bit scale.
inline void seed_uniform_peaks(Sample& s, std::pair<int8_t, int8_t> peak) {
	s.makePeaks(s.lengthInSamples, false, s.lengthInSamples);
	deluge_peaks_fill_uniform(s.peaks(), DelugePeak{.min = static_cast<int16_t>(peak.first * 256),
	                                                .max = static_cast<int16_t>(peak.second * 256)});
	deluge_peaks_widen_range(s.peaks(), DelugePeak{.min = -128 * 256, .max = 127 * 256});
}

} // namespace peak_seed
