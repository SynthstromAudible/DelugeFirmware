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

#include "model/sample/take_peaks.h"

#include "deluge_storage_sample_peaks.h"

#include <algorithm>

namespace deluge::sample {

void TakePeaks::feed(DelugePeakIndex* index, std::span<const uint8_t> bytes, uint32_t audio_start, uint8_t channels) {
	const uint32_t stride = kByteDepth * channels;
	if (stride == 0 || stride > carry_.size()) {
		return;
	}
	// The header.
	if (file_byte_ < audio_start) {
		const auto skip = static_cast<size_t>(std::min<uint64_t>(bytes.size(), audio_start - file_byte_));
		file_byte_ += skip;
		bytes = bytes.subspan(skip);
	}
	file_byte_ += bytes.size();
	// Room for every frame these bytes can complete (thread mode: the card side writes the take).
	deluge_peaks_reserve(index, frames() + (carry_len_ + bytes.size()) / kByteDepth + 1);
	// A frame the last call's bytes began.
	if (carry_len_ > 0) {
		const auto take = std::min<size_t>(stride - carry_len_, bytes.size());
		std::copy_n(bytes.begin(), take, carry_.begin() + carry_len_);
		carry_len_ += take;
		bytes = bytes.subspan(take);
		if (carry_len_ < stride) {
			return;
		}
		frame(index, carry_.data(), channels);
		carry_len_ = 0;
	}
	while (bytes.size() >= stride) {
		frame(index, bytes.data(), channels);
		bytes = bytes.subspan(stride);
	}
	std::ranges::copy(bytes, carry_.begin());
	carry_len_ = bytes.size();
}

void TakePeaks::frame(DelugePeakIndex* index, const uint8_t* bytes, uint8_t channels) {
	for (uint8_t c = 0; c < channels; c++) {
		// A 24-bit sample's top 16 bits: its second and third bytes.
		const uint8_t* s = bytes + c * kByteDepth;
		const auto v = static_cast<int16_t>(static_cast<uint16_t>(s[1] | (s[2] << 8)));
		min_ = std::min(min_, v);
		max_ = std::max(max_, v);
	}
	if (++block_frames_ == kBlockFrames) {
		deluge_peaks_fold_block(index, block_, DelugePeak{.min = min_, .max = max_});
		block_++;
		block_frames_ = 0;
		min_ = INT16_MAX;
		max_ = INT16_MIN;
	}
}

void TakePeaks::finish(DelugePeakIndex* index) {
	// The count stays: `frames()` is still the take's length.
	if (block_frames_ > 0) {
		deluge_peaks_fold_block(index, block_, DelugePeak{.min = min_, .max = max_});
	}
}

} // namespace deluge::sample
