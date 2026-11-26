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

#include <array>
#include <cstdint>
#include <span>

/// A sample's waveform peak index (`deluge_storage_sample_peaks.h`).
struct DelugePeakIndex;

namespace deluge::sample {

/// @brief A take's peaks, folded into its sample's index as the recorder writes its clusters: whole 256-frame blocks
///        in file order, the header skipped, a frame split across two clusters carried to the next.
///
/// The recorder writes 24-bit little-endian frames of one or two channels. Card side (thread mode): each feed reserves
/// the index's room for the frames it folds.
class TakePeaks {
public:
	/// @brief Fold the next bytes of the file.
	/// @param index       The take's index; null folds nothing but still counts the bytes.
	/// @param bytes       The file's next bytes, from where the last call stopped (the first call: byte 0).
	/// @param audio_start The byte the audio starts at.
	/// @param channels    Channels per frame, 1 or 2.
	void feed(DelugePeakIndex* index, std::span<const uint8_t> bytes, uint32_t audio_start, uint8_t channels);

	/// @brief Fold the last, part-filled block. Call once the take's index is final.
	/// @param index The take's index; null is ignored.
	void finish(DelugePeakIndex* index);

	/// @return The frames folded or counted so far.
	[[nodiscard]] uint64_t frames() const { return block_ * kBlockFrames + block_frames_; }

private:
	static constexpr uint32_t kBlockFrames = 256;
	static constexpr uint32_t kByteDepth = 3;

	void frame(DelugePeakIndex* index, const uint8_t* bytes, uint8_t channels);

	uint64_t file_byte_{0};
	std::array<uint8_t, 2 * kByteDepth> carry_{};
	uint32_t carry_len_{0};
	uint64_t block_{0};
	uint32_t block_frames_{0};
	int16_t min_{INT16_MAX};
	int16_t max_{INT16_MIN};
};

} // namespace deluge::sample
