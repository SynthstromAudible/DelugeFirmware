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

} // namespace deluge::gui::waveform
