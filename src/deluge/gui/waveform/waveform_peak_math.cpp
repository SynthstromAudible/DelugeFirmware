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

#include "gui/waveform/waveform_peak_math.h"
#include "gui/waveform/waveform_render_data.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace deluge::gui::waveform {

Peak scanPeak(const char* data, int32_t start, int32_t end, int32_t byte_depth, int32_t num_channels) {
	int32_t num_samples = (end - start) / byte_depth;
	int32_t stride = byte_depth;

	// Don't read endless samples: if there are lots, skip some
	int32_t skip = ((num_samples - 1) >> kMaxScanSamplesLog2) + 1;
	if (skip > 1) {
		// An odd stride alternates between the two channels, so both get read
		if (num_channels == 2 && (skip & 1) == 0) {
			skip++;
		}
		stride *= skip;
	}

	// Offset so each 32-bit read lands on the sample's most significant bytes
	int32_t offset = byte_depth - 4;

	Peak peak{std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min()};
	for (int32_t pos = start + offset; pos < end + offset; pos += stride) {
		int32_t value = *reinterpret_cast<const int32_t*>(&data[pos]);
		peak.min = std::min(peak.min, value);
		peak.max = std::max(peak.max, value);
	}
	return peak;
}

int8_t coarsePeak(int32_t value) {
	auto coarse = static_cast<int8_t>(value >> 24);
	if (value < 0) {
		coarse++; // Round toward zero
	}
	return coarse;
}

int32_t lastClusterEnd(uint64_t audio_bytes, uint32_t audio_start, int32_t cluster_size) {
	uint64_t end = audio_bytes + audio_start;
	return static_cast<int32_t>(((end - 1) & static_cast<uint64_t>(cluster_size - 1)) + 1);
}

int64_t clusterStart(int32_t index, int32_t size_log2) {
	return static_cast<int64_t>(index) << size_log2;
}

int32_t firstFrameOffset(int64_t cluster_start, uint32_t audio_start, int32_t frame_size) {
	if (cluster_start <= static_cast<int64_t>(audio_start)) {
		return static_cast<int32_t>(audio_start - cluster_start); // Still in the file header
	}
	auto into_frame = static_cast<int32_t>((cluster_start - audio_start) % frame_size);
	return (into_frame == 0) ? 0 : (frame_size - into_frame);
}

std::optional<int32_t> columnShift(int64_t delta, int64_t zoom, int32_t width) {
	if (zoom <= 0) {
		return std::nullopt;
	}

	// Round to the nearest whole column
	int64_t shift = delta / zoom;
	int64_t slack = delta - (shift * zoom);
	if (slack * 2 > zoom) {
		shift++;
		slack -= zoom;
	}
	else if (slack * 2 < -zoom) {
		shift--;
		slack += zoom;
	}

	if (shift == 0 || std::abs(shift) > width) {
		return std::nullopt;
	}
	if (std::abs(slack) > std::abs(shift) || std::abs(slack) * 2 >= zoom) {
		return std::nullopt;
	}
	return static_cast<int32_t>(shift);
}

namespace {

/// Rounds towards negative infinity. @p b must be positive.
int64_t floorDiv(int64_t a, int64_t b) {
	int64_t quotient = a / b;
	return (a % b < 0) ? quotient - 1 : quotient;
}

/// Rounds to the nearest whole number. @p b must be positive.
int64_t roundDiv(int64_t a, int64_t b) {
	return floorDiv(a + (b / 2), b);
}

} // namespace

void remapColumns(WaveformRenderData& data, int64_t scroll, int64_t zoom) {
	const WaveformRenderData old = data;
	const int64_t old_zoom = old.xZoom;
	if (zoom <= 0 || old_zoom <= 0) {
		std::ranges::fill(data.colStatus, ColumnStatus::NOT_INVESTIGATED);
		return;
	}

	// Same slack as columnShift(): up to a sample per column, but always under half a column
	const int64_t slack = std::min<int64_t>(kDisplayWidth, (old_zoom - 1) / 2);

	for (int32_t col = 0; col < kDisplayWidth; col++) {
		// This column's span, relative to the old column 0
		int64_t start = scroll + (col * zoom) - old.xScroll;
		int64_t end = start + zoom;

		// Old columns [first, last) that it covers
		int64_t first = roundDiv(start, old_zoom);
		int64_t last = roundDiv(end, old_zoom);
		bool aligned =
		    first < last && std::abs(start - (first * old_zoom)) <= slack && std::abs(end - (last * old_zoom)) <= slack;
		if (!aligned) {
			first = floorDiv(start, old_zoom);
			last = floorDiv(end - 1, old_zoom) + 1;
		}

		bool exact = aligned && first >= 0 && last <= kDisplayWidth;
		Peak peak{std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min()};
		bool any = false;
		for (int64_t old_col = std::max<int64_t>(first, 0); old_col < std::min<int64_t>(last, kDisplayWidth);
		     old_col++) {
			ColumnStatus status = old.colStatus[old_col];
			if (status != ColumnStatus::INVESTIGATED) {
				exact = false;
				if (status != ColumnStatus::PLACEHOLDER) {
					continue;
				}
			}
			peak.min = std::min(peak.min, old.minPerCol[old_col]);
			peak.max = std::max(peak.max, old.maxPerCol[old_col]);
			any = true;
		}

		if (!any) {
			data.colStatus[col] = ColumnStatus::NOT_INVESTIGATED;
			continue;
		}
		data.colStatus[col] = exact ? ColumnStatus::INVESTIGATED : ColumnStatus::PLACEHOLDER;
		data.minPerCol[col] = peak.min;
		data.maxPerCol[col] = peak.max;
	}
}

} // namespace deluge::gui::waveform
