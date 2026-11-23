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

#include "dsp/core/fixed_point.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace deluge::dsp::fixed::timestretch {

/// @file
/// @brief A stretcher's crossfade out of a sample cache: the tail of cached audio its older head fades out of, copied
/// into a buffer the engine lends, and mixed under the newer head (C1 to C4 of docs/dev/time_stretcher_contracts.md).

/// @brief Where a stretcher leaves a cache: the read position's frame, its place in its cache chunk, and what is
/// written of the cache past it.
struct CacheWindow {
	const std::byte* frame_origin; ///< The unmasked 32-bit read origin of the position's 3-byte cache frame (C2).
	int32_t chunk_offset;          ///< The position's byte offset in its cache chunk.
	int32_t chunk_size;            ///< Bytes.
	int32_t bytes_written_ahead;   ///< The written end less the position, in bytes.
};

/// The cache's bytes per sample.
inline constexpr int32_t kCacheByteDepth = 3;
/// The tail's frames: a whole number under the buffer's 256.
inline constexpr int32_t kCacheTailCapacity = 256;

/// @brief The frames of cache a tail can take at @p window, at most @p frames_to_hop and 255 (C1): none if the
/// written end is within one cache frame of the position, or the chunk ends within one; else as many whole frames as
/// lie before both, the chunk's end rounded up by a frame less a byte.
[[nodiscard]] constexpr int32_t cache_tail_frames(const CacheWindow& window, int32_t frames_to_hop, int32_t channels) {
	const int32_t frame_bytes = kCacheByteDepth * channels;
	const int32_t wanted = std::min(frames_to_hop, kCacheTailCapacity - 1);
	if (window.bytes_written_ahead <= frame_bytes) {
		return 0;
	}
	const int32_t to_chunk_end = window.chunk_size - window.chunk_offset + (frame_bytes - 1);
	if (to_chunk_end <= frame_bytes) {
		return 0;
	}
	const auto readable = static_cast<int32_t>(static_cast<uint32_t>(std::min(to_chunk_end, window.bytes_written_ahead))
	                                           / static_cast<uint32_t>(frame_bytes));
	return std::min(wanted, readable);
}

/// @brief A cache tail held in a lent buffer of kCacheTailCapacity frames: copied from the cache, then mixed in
/// segment by segment from a read position that wraps at the buffer's end.
class CacheTail {
public:
	/// Each cache word is copied as a word, its sample in the top three bytes, so it crosses into Q31 once.
	/// @brief Copies @p frames frames of @p channels from the cache frame whose read origin is @p cache_frame_origin
	/// (C2): each word the unmasked 32-bit read whose top three bytes are the sample, so its low byte, the byte before
	/// the sample, reaches the mix (C2's quirk). The read position starts at the tail's start (C3).
	CacheTail(std::span<Q<31>> storage, const std::byte* cache_frame_origin, int32_t frames, int32_t channels)
	    : storage_(storage), frames_(frames), channels_(channels) {
		for (int32_t i = 0; i < frames * channels; i++) {
			int32_t word = 0;
			std::memcpy(&word, cache_frame_origin + static_cast<std::ptrdiff_t>(i) * kCacheByteDepth, sizeof word);
			storage_[i] = Q<31>::from_raw(word);
		}
	}

	/// @brief Adopts a tail of @p frames already in @p storage, read on from @p position.
	CacheTail(std::span<Q<31>> storage, int32_t frames, int32_t channels, int32_t position)
	    : storage_(storage), frames_(frames), channels_(channels), position_(position) {}

	/// @brief Mixes the tail into @p out at @p level, stepping it by @p level_step before each frame (C4). Each
	/// product's high word is floored, where the heads' reads round (C4's quirk). A stereo tail mixed into a mono
	/// output is halved per channel and summed; the legacy's loop ends after @p frames times the tail's channels of
	/// output words, so a stereo tail mixed into mono mixes twice @p frames frames. A tail of no frames mixes nothing.
	void mix_into(Q<31>* out, int32_t frames, int32_t out_channels, Q<31> level, Q<31> level_step) {
		if (frames_ == 0) {
			return;
		}
		Q<31>* const end = out + static_cast<std::ptrdiff_t>(frames) * channels_;
		while (out != end) {
			Q<31> left = storage_[static_cast<std::size_t>(position_) * channels_];
			const Q<31> right = channels_ == 2 ? storage_[static_cast<std::size_t>(position_) * 2 + 1] : left;
			position_ = (position_ + 1) & (kCacheTailCapacity - 1);
			if (channels_ == 2 && out_channels == 1) {
				left = add_wrap(halve_floor(left), halve_floor(right));
			}
			level = add_wrap(level, level_step);
			*out = mixed_in_floor(*out, left, level);
			++out;
			if (out_channels == 2) {
				*out = mixed_in_floor(*out, right, level);
				++out;
			}
		}
	}

	[[nodiscard]] int32_t frames() const { return frames_; }

private:
	/// @p mix plus @p sample times @p level, the high word floored: the mix's Q30 view accumulating a Q31 product.
	[[nodiscard]] static Q<31> mixed_in_floor(Q<31> mix, Q<31> sample, Q<31> level) {
		return reinterpret_bits<Q<31>>(mla(reinterpret_bits<Q<30>>(mix), sample, level));
	}

	std::span<Q<31>> storage_;
	int32_t frames_;
	int32_t channels_;
	int32_t position_ = 0;
};

} // namespace deluge::dsp::fixed::timestretch
