// tests/unit/audio_stream/convert_cluster_spec.cpp
//
// Own file (not a second `describe` inside convert_spec.cpp): the harness's create_test_sourcelist
// dispatches ctest -> driver by matching a function named after the *file's* stem, so a second
// CPPSPEC_SPEC in convert_spec.cpp would compile but never actually run under ctest. A second
// *_spec.cpp file is auto-picked-up by the existing glob in CMakeLists.txt with no build-file changes.
#include "storage/audio/stream/convert.h"

#include "cppspec.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

using namespace deluge::audio::stream;

// clang-format off
describe convert_cluster("convert_cluster_data", $ {
	it("NATIVE leaves data untouched and does not write the backup out-param", _ {
		std::array<std::byte, 16> data{};
		for (size_t i = 0; i < data.size(); ++i) {
			data[i] = static_cast<std::byte>(i);
		}
		std::array<std::byte, 16> original = data;
		std::array<std::byte, 3> backup{std::byte{0xEE}, std::byte{0xEE}, std::byte{0xEE}};

		convert_cluster_data(std::span<std::byte>(data), /*cluster_index=*/0, RawDataFormat::NATIVE,
		                      ConvertGeometry{.audio_data_start_pos_bytes = 5,
		                                      .audio_data_length_bytes = 10,
		                                      .first_cluster_index_with_no_audio_data = 1},
		                      /*cluster_size=*/16, /*cluster_size_magnitude=*/4,
		                      std::span<std::byte, 3>(backup), [] {});

		expect(data == original).to_equal(true);
		expect(std::to_integer<int>(backup[0])).to_equal(0xEE);
		expect(std::to_integer<int>(backup[1])).to_equal(0xEE);
		expect(std::to_integer<int>(backup[2])).to_equal(0xEE);
	});

	it("ENDIANNESS_WRONG_24 swaps byte 0/2 of each 3-byte group in the audio region "
	   "and backs up the original first 3 bytes", _ {
		constexpr size_t cluster_size = 32; // 2^5
		std::array<std::byte, cluster_size> data{};
		for (size_t i = 0; i < data.size(); ++i) {
			data[i] = static_cast<std::byte>(i);
		}
		std::array<std::byte, 3> backup{};

		convert_cluster_data(std::span<std::byte>(data), /*cluster_index=*/0, RawDataFormat::ENDIANNESS_WRONG_24,
		                      ConvertGeometry{.audio_data_start_pos_bytes = 2,
		                                      .audio_data_length_bytes = 1000,
		                                      .first_cluster_index_with_no_audio_data = 5},
		                      cluster_size, /*cluster_size_magnitude=*/5,
		                      std::span<std::byte, 3>(backup), [] {});

		// Backup captured before any swap: the cluster's absolute first 3 bytes.
		expect(std::to_integer<int>(backup[0])).to_equal(0);
		expect(std::to_integer<int>(backup[1])).to_equal(1);
		expect(std::to_integer<int>(backup[2])).to_equal(2);

		// Bytes before the audio start (offset 2) are untouched.
		expect(std::to_integer<int>(data[0])).to_equal(0);
		expect(std::to_integer<int>(data[1])).to_equal(1);

		// First processed 3-byte group at offset 2: [2,3,4] -> [4,3,2].
		expect(std::to_integer<int>(data[2])).to_equal(4);
		expect(std::to_integer<int>(data[3])).to_equal(3);
		expect(std::to_integer<int>(data[4])).to_equal(2);

		// A middle group at offset 14: [14,15,16] -> [16,15,14].
		expect(std::to_integer<int>(data[14])).to_equal(16);
		expect(std::to_integer<int>(data[15])).to_equal(15);
		expect(std::to_integer<int>(data[16])).to_equal(14);

		// Last processed group at offset 29: [29,30,31] -> [31,30,29].
		expect(std::to_integer<int>(data[29])).to_equal(31);
		expect(std::to_integer<int>(data[30])).to_equal(30);
		expect(std::to_integer<int>(data[31])).to_equal(29);
	});

	it("UNSIGNED_8 XORs 0x80 per byte across the word region", _ {
		constexpr size_t cluster_size = 16; // 2^4
		std::array<std::byte, cluster_size> data{};
		std::array<std::byte, 3> backup{};

		convert_cluster_data(std::span<std::byte>(data), /*cluster_index=*/0, RawDataFormat::UNSIGNED_8,
		                      ConvertGeometry{.audio_data_start_pos_bytes = 4,
		                                      .audio_data_length_bytes = 1000,
		                                      .first_cluster_index_with_no_audio_data = 5},
		                      cluster_size, /*cluster_size_magnitude=*/4,
		                      std::span<std::byte, 3>(backup), [] {});

		// Bytes before the (4-byte-aligned) start pos are untouched.
		expect(std::to_integer<int>(data[0])).to_equal(0x00);
		expect(std::to_integer<int>(data[1])).to_equal(0x00);
		expect(std::to_integer<int>(data[2])).to_equal(0x00);
		expect(std::to_integer<int>(data[3])).to_equal(0x00);

		// The word region [4..16) is XORed with 0x80 per byte.
		for (size_t i = 4; i < cluster_size; ++i) {
			expect(std::to_integer<int>(data[i])).to_equal(0x80);
		}
	});

	it("audio_data_start_pos_bytes == 0 is a no-op (early-out before any backup or conversion)", _ {
		std::array<std::byte, 16> data{};
		for (size_t i = 0; i < data.size(); ++i) {
			data[i] = static_cast<std::byte>(i);
		}
		std::array<std::byte, 16> original = data;
		std::array<std::byte, 3> backup{std::byte{0xEE}, std::byte{0xEE}, std::byte{0xEE}};

		convert_cluster_data(std::span<std::byte>(data), /*cluster_index=*/0, RawDataFormat::ENDIANNESS_WRONG_24,
		                      ConvertGeometry{.audio_data_start_pos_bytes = 0,
		                                      .audio_data_length_bytes = 10,
		                                      .first_cluster_index_with_no_audio_data = 1},
		                      /*cluster_size=*/16, /*cluster_size_magnitude=*/4,
		                      std::span<std::byte, 3>(backup), [] {});

		expect(data == original).to_equal(true);
		expect(std::to_integer<int>(backup[0])).to_equal(0xEE);
		expect(std::to_integer<int>(backup[1])).to_equal(0xEE);
		expect(std::to_integer<int>(backup[2])).to_equal(0xEE);
	});

	it("ENDIANNESS_WRONG_32 reverses all 4 bytes of every word across a region that straddles a future "
	   "16-byte SIMD chunk boundary (68 bytes = 4 full 16-byte chunks + a 4-byte/1-word scalar tail)", _ {
		constexpr size_t cluster_size = 128; // 2^7
		std::array<std::byte, cluster_size> data{};
		for (size_t i = 0; i < data.size(); ++i) {
			data[i] = static_cast<std::byte>(i);
		}
		std::array<std::byte, 3> backup{};

		// Region [4, 72): 68 bytes = 17 words, not a multiple of 16.
		convert_cluster_data(std::span<std::byte>(data), /*cluster_index=*/0, RawDataFormat::ENDIANNESS_WRONG_32,
		                      ConvertGeometry{.audio_data_start_pos_bytes = 4,
		                                      .audio_data_length_bytes = 68,
		                                      .first_cluster_index_with_no_audio_data = 1},
		                      cluster_size, /*cluster_size_magnitude=*/7,
		                      std::span<std::byte, 3>(backup), [] {});

		// Untouched before the region.
		expect(std::to_integer<int>(data[0])).to_equal(0);
		expect(std::to_integer<int>(data[3])).to_equal(3);

		// Every word in [4, 72) has all 4 bytes reversed: new[w+k] = old[w+3-k].
		for (size_t w = 4; w < 72; w += 4) {
			for (size_t k = 0; k < 4; ++k) {
				expect(std::to_integer<int>(data[w + k])).to_equal(static_cast<int>(w + 3 - k));
			}
		}

		// Untouched after the region.
		expect(std::to_integer<int>(data[72])).to_equal(72);
		expect(std::to_integer<int>(data[127])).to_equal(127);
	});

	it("ENDIANNESS_WRONG_16 swaps bytes within each 16-bit halfword across a region that straddles a "
	   "future 16-byte SIMD chunk boundary (52 bytes = 3 full 16-byte chunks + a 4-byte/1-word scalar tail)", _ {
		constexpr size_t cluster_size = 128; // 2^7
		std::array<std::byte, cluster_size> data{};
		for (size_t i = 0; i < data.size(); ++i) {
			data[i] = static_cast<std::byte>(i);
		}
		std::array<std::byte, 3> backup{};

		// Region [8, 60): 52 bytes = 13 words, not a multiple of 16.
		convert_cluster_data(std::span<std::byte>(data), /*cluster_index=*/0, RawDataFormat::ENDIANNESS_WRONG_16,
		                      ConvertGeometry{.audio_data_start_pos_bytes = 8,
		                                      .audio_data_length_bytes = 52,
		                                      .first_cluster_index_with_no_audio_data = 1},
		                      cluster_size, /*cluster_size_magnitude=*/7,
		                      std::span<std::byte, 3>(backup), [] {});

		// Untouched before the region.
		expect(std::to_integer<int>(data[7])).to_equal(7);

		// Every word in [8, 60) has each halfword byte-swapped: new[w]=old[w+1], new[w+1]=old[w],
		// new[w+2]=old[w+3], new[w+3]=old[w+2].
		for (size_t w = 8; w < 60; w += 4) {
			expect(std::to_integer<int>(data[w])).to_equal(static_cast<int>(w + 1));
			expect(std::to_integer<int>(data[w + 1])).to_equal(static_cast<int>(w));
			expect(std::to_integer<int>(data[w + 2])).to_equal(static_cast<int>(w + 3));
			expect(std::to_integer<int>(data[w + 3])).to_equal(static_cast<int>(w + 2));
		}

		// Untouched after the region.
		expect(std::to_integer<int>(data[60])).to_equal(60);
	});

	it("UNSIGNED_8 XORs 0x80 per byte across a region that straddles a future 16-byte SIMD chunk boundary "
	   "(76 bytes = 4 full 16-byte chunks + a 12-byte scalar tail)", _ {
		constexpr size_t cluster_size = 128; // 2^7
		std::array<std::byte, cluster_size> data{};
		std::array<std::byte, 3> backup{};

		// Region [4, 80): 76 bytes, not a multiple of 16.
		convert_cluster_data(std::span<std::byte>(data), /*cluster_index=*/0, RawDataFormat::UNSIGNED_8,
		                      ConvertGeometry{.audio_data_start_pos_bytes = 4,
		                                      .audio_data_length_bytes = 76,
		                                      .first_cluster_index_with_no_audio_data = 1},
		                      cluster_size, /*cluster_size_magnitude=*/7,
		                      std::span<std::byte, 3>(backup), [] {});

		// Untouched before the region.
		expect(std::to_integer<int>(data[0])).to_equal(0x00);
		expect(std::to_integer<int>(data[3])).to_equal(0x00);

		// Every byte in [4, 80) is XORed with 0x80 (data started zeroed).
		for (size_t i = 4; i < 80; ++i) {
			expect(std::to_integer<int>(data[i])).to_equal(0x80);
		}

		// Untouched after the region.
		expect(std::to_integer<int>(data[80])).to_equal(0x00);
		expect(std::to_integer<int>(data[127])).to_equal(0x00);
	});

	it("ENDIANNESS_WRONG_24 swaps byte 0/2 of each 3-byte group across a region that crosses a future "
	   "48-byte SIMD chunk boundary (54 bytes = 18 groups = 1 full 48-byte/16-group chunk + a "
	   "6-byte/2-group scalar tail)", _ {
		constexpr size_t cluster_size = 128; // 2^7
		std::array<std::byte, cluster_size> data{};
		for (size_t i = 0; i < data.size(); ++i) {
			data[i] = static_cast<std::byte>(i);
		}
		std::array<std::byte, 3> backup{};

		// Region [2, 56): 54 bytes = 18 whole 3-byte groups, not a multiple of 48.
		convert_cluster_data(std::span<std::byte>(data), /*cluster_index=*/0, RawDataFormat::ENDIANNESS_WRONG_24,
		                      ConvertGeometry{.audio_data_start_pos_bytes = 2,
		                                      .audio_data_length_bytes = 54,
		                                      .first_cluster_index_with_no_audio_data = 1},
		                      cluster_size, /*cluster_size_magnitude=*/7,
		                      std::span<std::byte, 3>(backup), [] {});

		expect(std::to_integer<int>(backup[0])).to_equal(0);
		expect(std::to_integer<int>(backup[1])).to_equal(1);
		expect(std::to_integer<int>(backup[2])).to_equal(2);

		// Untouched before the region.
		expect(std::to_integer<int>(data[0])).to_equal(0);
		expect(std::to_integer<int>(data[1])).to_equal(1);

		// Every 3-byte group in [2, 56) has byte 0 and byte 2 swapped: [g, g+1, g+2] -> [g+2, g+1, g].
		for (size_t g = 2; g < 56; g += 3) {
			expect(std::to_integer<int>(data[g])).to_equal(static_cast<int>(g + 2));
			expect(std::to_integer<int>(data[g + 1])).to_equal(static_cast<int>(g + 1));
			expect(std::to_integer<int>(data[g + 2])).to_equal(static_cast<int>(g));
		}

		// Untouched after the region.
		expect(std::to_integer<int>(data[56])).to_equal(56);
	});

	it("FLOAT converts each word to Q31 via q31_from_float across a region that straddles a future "
	   "16-byte SIMD chunk boundary (20 bytes = 5 words = 1 full 16-byte/4-word chunk + a "
	   "4-byte/1-word scalar tail); pins saturation and NaN/+inf behavior", _ {
		constexpr size_t cluster_size = 64; // 2^6
		std::array<std::byte, cluster_size> data{};
		std::array<std::byte, 3> backup{};

		// Region [4, 24): 5 words. word0=0.5f, word1=1.0f (saturates), word2=-0.5f, word3=NaN (last word
		// of the vectorized prefix), word4=+inf (the scalar tail word).
		store_word_unaligned(&data[4], std::bit_cast<int32_t>(0.5f));
		store_word_unaligned(&data[8], std::bit_cast<int32_t>(1.0f));
		store_word_unaligned(&data[12], std::bit_cast<int32_t>(-0.5f));
		store_word_unaligned(&data[16], std::bit_cast<int32_t>(std::numeric_limits<float>::quiet_NaN()));
		store_word_unaligned(&data[20], std::bit_cast<int32_t>(std::numeric_limits<float>::infinity()));

		convert_cluster_data(std::span<std::byte>(data), /*cluster_index=*/0, RawDataFormat::FLOAT,
		                      ConvertGeometry{.audio_data_start_pos_bytes = 4,
		                                      .audio_data_length_bytes = 20,
		                                      .first_cluster_index_with_no_audio_data = 1},
		                      cluster_size, /*cluster_size_magnitude=*/6,
		                      std::span<std::byte, 3>(backup), [] {});

		// 0.5f -> 0x40000000.
		expect(load_word_unaligned(&data[4])).to_equal(0x40000000);
		// 1.0f saturates to Q31 max.
		expect(load_word_unaligned(&data[8])).to_equal(0x7FFFFFFF);
		// -0.5f -> 0xC0000000.
		expect(load_word_unaligned(&data[12])).to_equal(static_cast<int32_t>(0xC0000000));
		// NaN (quiet, sign bit clear on this host) saturates to Q31 max, same as a positive out-of-range
		// value -- pins the scalar q31_from_float's actual NaN handling for the SIMD path to match.
		expect(load_word_unaligned(&data[16])).to_equal(0x7FFFFFFF);
		// +inf saturates to Q31 max (the scalar-tail word).
		expect(load_word_unaligned(&data[20])).to_equal(0x7FFFFFFF);
	});

	it("invokes yield at least once for a buffer large enough to cross the ~1024-byte cadence", _ {
		constexpr size_t cluster_size = 4096; // 2^12
		auto data = std::make_unique<std::array<std::byte, cluster_size>>();
		std::array<std::byte, 3> backup{};
		int yield_count = 0;

		convert_cluster_data(std::span<std::byte>(*data), /*cluster_index=*/0, RawDataFormat::ENDIANNESS_WRONG_24,
		                      ConvertGeometry{.audio_data_start_pos_bytes = 3,
		                                      .audio_data_length_bytes = 100000,
		                                      .first_cluster_index_with_no_audio_data = 5},
		                      cluster_size, /*cluster_size_magnitude=*/12,
		                      std::span<std::byte, 3>(backup), [&] { ++yield_count; });

		expect(yield_count >= 1).to_equal(true);
	});
});

CPPSPEC_SPEC(convert_cluster)
