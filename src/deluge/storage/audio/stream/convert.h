#pragma once
#include "storage/audio/audio_file_format.h" // RawDataFormat
#include <algorithm>
#include <argon.hpp>
#include <argon/helpers/size.hpp> // argon::helpers::vectorizeable_size (not pulled in by argon.hpp itself)
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility> // std::unreachable

namespace deluge::audio::stream {
/// @brief Convert a single 4-byte word from `format` to native representation.
///
/// Depends only on `format` — no cluster or geometry state. ENDIANNESS_WRONG_24 and NATIVE return the
/// word unchanged; the 24-bit 3-byte swap is instead done group-wise in convert_cluster_data.
/// @param word   The word to convert, as read from the raw byte stream.
/// @param format The stream's on-disk format.
/// @return The word converted to native representation.
int32_t convert_word(int32_t word, RawDataFormat format);

/// @brief Load a 4-byte word from a possibly-unaligned byte address, UB-free.
///
/// Goes through a local `int32_t` via `memcpy`, which the compiler folds into a single unaligned
/// load/store at -O2 (zero cost) — unlike `*reinterpret_cast<int32_t*>(p)`, which is both an unaligned
/// access and a strict-aliasing violation on a char/std::byte buffer.
/// @param p Byte address to load from; need not be 4-byte aligned.
/// @return The loaded word.
inline int32_t load_word_unaligned(const std::byte* p) {
	int32_t w;
	std::memcpy(&w, p, sizeof(w));
	return w;
}
/// @brief Store a 4-byte word to a possibly-unaligned byte address, UB-free.
///
/// @see load_word_unaligned for why this goes through `memcpy` rather than a cast.
/// @param p Byte address to store to; need not be 4-byte aligned.
/// @param w Word to store.
inline void store_word_unaligned(std::byte* p, int32_t w) {
	std::memcpy(p, &w, sizeof(w));
}
/// @brief Convert one word at a possibly-unaligned byte address in place.
///
/// UB-free replacement for `*(int32_t*)p = convert_word(*(int32_t*)p, format)`.
/// @param p      Byte address of the word to convert in place; need not be 4-byte aligned.
/// @param format The stream's on-disk format.
inline void convert_word_in_place(std::byte* p, RawDataFormat format) {
	store_word_unaligned(p, convert_word(load_word_unaligned(p), format));
}

/// @brief Audio-data geometry that convert_cluster_data needs from the owning Sample.
///
/// Gathered and passed in by value by the caller; the cluster itself owns none of this.
struct ConvertGeometry {
	uint32_t audio_data_start_pos_bytes;            ///< Byte offset of the sample's audio data in the file.
	uint64_t audio_data_length_bytes;               ///< Length of the sample's audio data, in bytes.
	int32_t first_cluster_index_with_no_audio_data; ///< = sample->getFirstClusterIndexWithNoAudioData()
};

/// @brief Vectorized ENDIANNESS_WRONG_24 byteswap over the 48-byte-aligned (16 lanes x 3-byte groups)
///        prefix of [begin, end).
///
/// De-interleaves 16 consecutive 3-byte groups into 3 channel vectors via vld3 (LoadInterleaved<3>),
/// then stores them back with channel 0 and channel 2 swapped via vst3 (store_interleaved) — the
/// vectorized form of the scalar byte0<->byte2 swap in convert_24bit_range. The caller's `begin` is
/// already 3-byte-group-aligned (see convert_cluster_data), so the 48-byte SIMD grid lines up with the
/// scalar grid with no prologue needed.
/// @tparam Yield Cooperative-scheduling callable; see convert_cluster_data's @note for what it's for.
/// @param begin Start of the range; must already be 3-byte-group-aligned.
/// @param end   End of the range (exclusive).
/// @param yield Called roughly every 1024 bytes, matching convert_24bit_range's cadence.
/// @return The (48-byte-aligned) point where the caller's scalar tail should pick up.
template <class Yield>
char* convert_24bit_range_simd(char* begin, char const* end, Yield yield) {
	constexpr size_t lanes = Argon<uint8_t>::lanes; // 16
	constexpr size_t group_bytes = lanes * 3;       // 48

	size_t const num_groups = (static_cast<size_t>(end - begin) / 3) / lanes;
	char* const vec_end = begin + num_groups * group_bytes;

	size_t bytes_since_yield = 0;
	for (; begin < vec_end; begin += group_bytes) {
		auto* p = reinterpret_cast<uint8_t*>(begin);
		auto [c0, c1, c2] = Argon<uint8_t>::LoadInterleaved<3>(p);
		argon::store_interleaved(p, c2, c1, c0);

		bytes_since_yield += group_bytes;
		if (bytes_since_yield >= 1024) {
			yield();
			bytes_since_yield = 0;
		}
	}
	return begin;
}

/// @brief Swap byte 0 and byte 2 of every 3-byte group in [begin, end) (the ENDIANNESS_WRONG_24 byteswap).
///
/// Runs vectorized over the 48-byte-aligned prefix first (convert_24bit_range_simd); this loop then only
/// covers the (< 48-byte) scalar tail.
/// @tparam Yield Cooperative-scheduling callable; see convert_cluster_data's @note for what it's for.
/// @param begin Start of the range.
/// @param end   End of the range (exclusive).
/// @param yield Called roughly every 1024 bytes; skipped after the final chunk.
template <class Yield>
void convert_24bit_range(char* begin, char const* end, Yield yield) {
	begin = convert_24bit_range_simd(begin, end, yield);

	while (true) {
		// Every this many bytes, we'll pause and do an audio routine.
		char const* chunk_end = std::min<char const*>(begin + 1024, end);

		while (begin < chunk_end) {
			std::swap(begin[0], begin[2]); // ENDIANNESS_WRONG_24: swap byte 0 and byte 2 of this 3-byte group
			begin += 3;
		}

		if (begin >= end) {
			break;
		}

		yield();
	}
}

/// @brief Vectorized transform over the 16-byte-aligned prefix of [begin, end), for formats with a SIMD
///        path (UNSIGNED_8, ENDIANNESS_WRONG_32, ENDIANNESS_WRONG_16, and FLOAT on real NEON only — see
///        the `#if` below).
///
/// Yields roughly every 1024 bytes (64 lanes), matching convert_word_range's cadence.
/// @tparam Yield Cooperative-scheduling callable; see convert_cluster_data's @note for what it's for.
/// @param begin  Start of the range.
/// @param end    End of the range (exclusive).
/// @param format The stream's on-disk format.
/// @param yield  Called roughly every 1024 bytes.
/// @return The (16-byte-aligned) point where the caller's scalar tail should pick up; for formats with
///         no SIMD path, `begin` is returned unchanged so the caller's scalar loop covers the whole range.
template <class Yield>
std::byte* convert_range_simd(std::byte* begin, std::byte* end, RawDataFormat format, Yield yield) {
	// Integer formats always take the SIMD path; FLOAT only on real NEON (the #if below).
	bool const has_int_simd_path = format == RawDataFormat::UNSIGNED_8 || format == RawDataFormat::ENDIANNESS_WRONG_32
	                               || format == RawDataFormat::ENDIANNESS_WRONG_16;
#if defined(__ARM_NEON) || defined(__ARM_FEATURE_MVE)
	bool const has_simd_path = has_int_simd_path || format == RawDataFormat::FLOAT;
#else
	bool const has_simd_path = has_int_simd_path;
#endif
	if (!has_simd_path) {
		return begin;
	}

	auto* p = reinterpret_cast<uint8_t*>(begin);
	uint8_t* const vec_end = p + argon::helpers::vectorizeable_size<uint8_t>(static_cast<size_t>(end - begin));
	Argon<uint8_t> const xor_key{uint8_t{0x80}};

	size_t bytes_since_yield = 0;
	for (; p < vec_end; p += Argon<uint8_t>::lanes) {
		switch (format) {
		case RawDataFormat::UNSIGNED_8:
			(Argon<uint8_t>::Load(p) ^ xor_key).StoreTo(p);
			break;
		case RawDataFormat::ENDIANNESS_WRONG_32:
			Argon<uint8_t>::Load(p).Reverse32bit().StoreTo(p);
			break;
		case RawDataFormat::ENDIANNESS_WRONG_16:
			Argon<uint8_t>::Load(p).Reverse16bit().StoreTo(p);
			break;
#if defined(__ARM_NEON) || defined(__ARM_FEATURE_MVE)
		case RawDataFormat::FLOAT: {
			auto converted = Argon<float>::Load(reinterpret_cast<const float*>(p)).ConvertTo<int32_t, 31>();
			converted.StoreTo(reinterpret_cast<int32_t*>(p));
			break;
		}
#endif
		default:
			std::unreachable();
		}

		bytes_since_yield += Argon<uint8_t>::lanes;
		if (bytes_since_yield >= 1024) {
			yield();
			bytes_since_yield = 0;
		}
	}
	return reinterpret_cast<std::byte*>(p);
}

/// @brief Convert every 4-byte word in [begin, end) in place (the non-24-bit word loop).
///
/// Converts via convert_word_in_place, yielding on a 1024-byte address-aligned cadence. Formats with a
/// SIMD path (convert_range_simd) run vectorized over their 16-byte-aligned prefix first; this loop then
/// only covers the (< 16-byte) scalar tail for those, and the whole range for everything else.
/// @tparam Yield Cooperative-scheduling callable; see convert_cluster_data's @note for what it's for.
/// @param begin  Start of the range.
/// @param end    End of the range (exclusive).
/// @param format The stream's on-disk format.
/// @param yield  Called roughly every 1024 bytes (whenever `begin`'s address crosses a 1024-byte boundary).
template <class Yield>
void convert_word_range(std::byte* begin, std::byte* end, RawDataFormat format, Yield yield) {
	begin = convert_range_simd(begin, end, format, yield);

	// Yield roughly every 1024 bytes (i.e. whenever `begin`'s address crosses a 1024-byte boundary).
	constexpr uintptr_t yield_address_mask = 0b1111111100;

	for (; begin < end; begin += 4) {
		if ((reinterpret_cast<uintptr_t>(begin) & yield_address_mask) == 0) {
			yield();
		}

		convert_word_in_place(begin, format);
	}
}

/// @brief Convert `data[0..cluster_size)` in place from `format` to native representation.
///
/// The pure core backing StreamedChunk::convert_data_if_necessary: given this cluster's index and the
/// sample's audio-data geometry, converts only the audio-bearing portion of the cluster. On
/// `format != RawDataFormat::NATIVE`, backs up the pre-conversion first 3 bytes of `data` into
/// `unconverted_head_out` (mirrors StreamedChunk::first_three_bytes_pre_data_conversion, used to undo the
/// 24-bit swap on a scan reversal) before doing any conversion.
///
/// @note `yield` is a cooperative-scheduling shim: called (as `yield()`, no args) roughly every 1024
///       bytes to pump the caller's audio routine during a long in-place conversion, without this
///       function knowing anything about AudioEngine. It's only needed because the conversion currently
///       runs cooperatively on the audio context; once audio is preemptively scheduled it becomes
///       unnecessary and removable (see TODO.md). Pass a no-op callable (e.g. `[]{}`) if no yielding is
///       desired.
/// @tparam Yield Cooperative-scheduling callable; see the @note above.
/// @param data                   The cluster's raw bytes, converted in place.
/// @param cluster_index          This cluster's index within the sample.
/// @param format                 The stream's on-disk format; a no-op when RawDataFormat::NATIVE.
/// @param geometry               The sample's audio-data geometry (see ConvertGeometry).
/// @param cluster_size           The cluster size, in bytes.
/// @param cluster_size_magnitude log2(cluster_size) — cluster_size expressed as a shift amount.
/// @param unconverted_head_out   Receives the pre-conversion first 3 bytes of `data`.
/// @param yield                  Cooperative-scheduling callback; see the @note above.
template <class Yield>
void convert_cluster_data(std::span<std::byte> data, int32_t cluster_index, RawDataFormat format,
                          ConvertGeometry geometry, size_t cluster_size, size_t cluster_size_magnitude,
                          std::span<std::byte, 3> unconverted_head_out, Yield yield) {
	// We haven't yet figured out where the audio data starts.
	if (geometry.audio_data_start_pos_bytes == 0) {
		return;
	}
	if (format == RawDataFormat::NATIVE) {
		return;
	}

	// Back up the pre-conversion first 3 bytes (mirrors StreamedChunk::first_three_bytes_pre_data_conversion,
	// used to undo the 24-bit swap on a scan reversal) before doing any conversion.
	std::ranges::copy(data.first<3>(), unconverted_head_out.begin());

	int32_t const audio_start_pos = geometry.audio_data_start_pos_bytes;
	int32_t const first_audio_cluster = audio_start_pos >> cluster_size_magnitude;

	if (cluster_index < first_audio_cluster) { // Hmm, there must have been a case where this happens...
		return;
	}

	bool const is_last_audio_cluster = cluster_index == geometry.first_cluster_index_with_no_audio_data - 1;

	// The offset within this cluster where the sample's audio data ends, when it ends inside this cluster
	// (only meaningful when is_last_audio_cluster).
	auto const audio_region_end_offset = [&]() -> uint32_t {
		uint32_t const audio_data_end_pos = geometry.audio_data_start_pos_bytes + geometry.audio_data_length_bytes;
		return audio_data_end_pos & (cluster_size - 1);
	};

	// Special case for 24-bit with its uneven number of bytes.
	if (format == RawDataFormat::ENDIANNESS_WRONG_24) {
		size_t begin_offset;
		if (cluster_index == first_audio_cluster) {
			begin_offset = audio_start_pos & (cluster_size - 1);
		}
		else {
			uint32_t const bytes_before_cluster_start =
			    cluster_index * cluster_size - geometry.audio_data_start_pos_bytes;
			int32_t bytes_into_prev_group = bytes_before_cluster_start % 3;
			if (bytes_into_prev_group == 0) {
				bytes_into_prev_group = 3;
			}
			begin_offset = 3 - bytes_into_prev_group;
		}
		size_t const end_offset = is_last_audio_cluster ? audio_region_end_offset() : cluster_size - 2;

		convert_24bit_range(reinterpret_cast<char*>(data.data() + begin_offset),
		                    reinterpret_cast<char*>(data.data() + end_offset), yield);
	}

	// Or, all other bit depths.
	else {
		size_t const begin_offset =
		    cluster_index == first_audio_cluster ? (audio_start_pos & (cluster_size - 1)) : (audio_start_pos & 0b11);
		size_t const end_offset = is_last_audio_cluster ? audio_region_end_offset() : cluster_size - 3;

		convert_word_range(data.data() + begin_offset, data.data() + end_offset, format, yield);
	}
}
} // namespace deluge::audio::stream
