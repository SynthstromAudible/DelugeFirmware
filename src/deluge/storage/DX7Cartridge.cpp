/**
 *
 * Copyright (c) 2024 Katherine Whitlock
 * Copyright (c) 2014-2017 Pascal Gauthier.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 *
 */

#include "DX7Cartridge.h"
#include "util/misc.h"

#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>

std::byte sysexChecksum(std::span<std::byte> sysex) {
	int32_t sum = std::ranges::fold_left(sysex, 0, [](int32_t sum, std::byte b) {
		return sum - std::to_integer<uint8_t>(b); // fold op
	});
	return std::byte(sum & 0x7F);
}

void exportSysexPgm(std::span<std::byte> dest, std::span<std::byte> src) {
	std::array header = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
	auto [in_it, out_it] = std::ranges::copy(src, dest.begin());

	// copy 1 unpacked voices
	out_it = std::copy_n(src.begin(), 155, out_it);

	// make checksum for dump
	std::array footer = {sysexChecksum(src.first(155)), 0xF7_b};
	std::ranges::copy(footer, out_it);
}

/**
 * Pack a program into a 32 packed sysex
 */
void DX7Cartridge::packProgram(uint8_t* src, int idx, char* name, char* opSwitch) {
	auto* bulk = reinterpret_cast<uint8_t*>(&voiceData[6 + (idx * 128)]);

	for (int op = 0; op < 6; op++) {
		// eg rate and level, brk pt, depth, scaling
		memcpy(bulk + op * 17, src + op * 21, 11);
		int pp = op * 17;
		int up = op * 21;

		// left curves
		bulk[pp + 11] = (src[up + 11] & 0x03) | ((src[up + 12] & 0x03) << 2);
		bulk[pp + 12] = (src[up + 13] & 0x07) | ((src[up + 20] & 0x0f) << 3);

		// kvs_ams
		bulk[pp + 13] = (src[up + 14] & 0x03) | ((src[up + 15] & 0x07) << 2);

		// output lvl
		if (opSwitch[op] == '0') {
			bulk[pp + 14] = 0;
		}
		else {
			bulk[pp + 14] = src[up + 16];
		}

		// fcoarse_mode
		bulk[pp + 15] = (src[up + 17] & 0x01) | ((src[up + 18] & 0x1f) << 1);

		// fine freq
		bulk[pp + 16] = src[up + 19];
	}
	memcpy(bulk + 102, src + 126, 9); // pitch env, algo
	bulk[111] = (src[135] & 0x07) | ((src[136] & 0x01) << 3);
	memcpy(bulk + 112, src + 137, 4); // lfo
	bulk[116] = (src[141] & 0x01) | (((src[142] & 0x07) << 1) | ((src[143] & 0x07) << 4));
	bulk[117] = src[144];

	int eos = 0;

	for (int i = 0; i < 10; i++) {
		char c = (char)name[i];
		if (c == 0) {
			eos = 1;
		}
		if (eos) {
			bulk[118 + i] = ' ';
			continue;
		}
		c = c < 32 ? ' ' : c;
		c = c > 127 ? ' ' : c;
		bulk[118 + i] = c;
	}
}

/**
 * This function normalize data that comes from corrupted sysex.
 * It used to avoid engine crashing upon extreme values
 */
uint8_t normparm(uint8_t value, uint8_t max, int id) {
	if (value <= max && value >= 0) {
		return value;
	}

	// if this is beyond the max, we expect a 0-255 range, normalize this
	// to the expected return value; and this value as a random data.

	return ((float)value) / 255 * max;
}

/**
 * @brief unpacks a byte into separate bitfield components, least significant bits first
 *
 * @tparam args the length of each bitfield, LSB first
 * @param b the byte to unpack
 * @return an array of bitfield components as separate bytes
 */
template <size_t... args, typename T>
constexpr std::array<T, sizeof...(args)> unpackBits(T b) {
	static_assert((args + ...) <= CHAR_BIT);
	std::array<T, sizeof...(args)> output;

	size_t i = 0;
	for (size_t num_bits : {args...}) {
		auto bitmask = static_cast<T>((1 << num_bits) - 1);
		output[i++] = b & bitmask;
		b >>= num_bits;
	}
	return output;
}

template <typename T>
constexpr T clearTopNBits(T b, size_t n) {
	return b & ~util::top_n_bits<T>(n);
}

template <typename T>
constexpr T clearTopBit(T b) {
	return clearTopNBits(b, 1);
}

// For details on the DX7 SYSEX specification, please see contrib/sysex-format.txt

void DX7Cartridge::unpackProgram(std::span<std::uint8_t> unpackPgm, size_t idx) {
	if (!isCartridge()) {
		std::memcpy(unpackPgm.data(), &voiceData[6], 155);
		return;
	}

	auto* bulk = reinterpret_cast<uint8_t*>(&voiceData[6 + (idx * 128)]);

	for (size_t op = 0; op < 6; ++op) {
		std::uint8_t* bulk_op = &bulk[op * 17];
		std::uint8_t* unpack_op = &unpackPgm[op * 21];

		// eg rate and level, brk pt, depth, scaling
		for (size_t i = 0; i < 10; ++i) {
			unpack_op[i] = normparm(clearTopBit(bulk_op[i]), 99, i); // mask BIT7 (don't care per sysex spec)
		}

		auto [left_curve, right_curve] = unpackBits<2, 2>(clearTopBit(bulk_op[11]));
		unpack_op[11] = left_curve;
		unpack_op[12] = right_curve;

		auto [rs, detune] = unpackBits<3, 4>(clearTopBit(bulk_op[12]));
		unpack_op[13] = rs;

		auto [ams, kvs] = unpackBits<2, 3>(clearTopBit(bulk_op[13])); // bits 5-7 don't care per sysex spec
		unpack_op[14] = ams;
		unpack_op[15] = kvs;
		unpack_op[16] = clearTopBit(bulk_op[14]); // output level

		auto [mode, freq_coarse] = unpackBits<1, 5>(clearTopBit(bulk_op[15])); // bits 6-7 don't care per sysex spec
		unpack_op[17] = mode;
		unpack_op[18] = freq_coarse;
		unpack_op[19] = clearTopBit(bulk_op[16]); // fine freq
		unpack_op[20] = detune;
	}

	for (int i = 0; i < 8; i++) {
		unpackPgm[126 + i] = normparm(clearTopBit(bulk[102 + i]), 99, 126 + i); // mask BIT7 (don't care per sysex spec)
	}

	// algorithm // bits 5-7 are don't care per sysex spec
	unpackPgm[134] = normparm(clearTopNBits(bulk[110], 3), 31, 134);

	auto [fb, oks] = unpackBits<3, 1>(clearTopBit(bulk[111])); // bits 4-7 are don't care per spec
	unpackPgm[135] = fb;
	unpackPgm[136] = oks;
	unpackPgm[137] = clearTopBit(bulk[112]); // lfs
	unpackPgm[138] = clearTopBit(bulk[113]); // lfd
	unpackPgm[139] = clearTopBit(bulk[114]); // lpmd
	unpackPgm[140] = clearTopBit(bulk[115]); // lamd

	auto [lks, lfw, lpms] = unpackBits<1, 4, 2>(clearTopBit(bulk[116]));
	unpackPgm[141] = lks;
	unpackPgm[142] = lfw;
	unpackPgm[143] = lpms;
	unpackPgm[144] = clearTopBit(bulk[117]);
	for (int name_idx = 0; name_idx < 10; name_idx++) {
		unpackPgm[145 + name_idx] = clearTopBit(bulk[118 + name_idx]);
	} // name_idx
	  //    memcpy(unpackPgm + 144, bulk + 117, 11);  // transpose, name
}
