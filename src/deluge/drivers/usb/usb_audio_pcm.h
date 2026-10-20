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

#include <argon.hpp>
#include <array>
#include <cstddef>
#include <cstdint>

// Converting between the Deluge's samples and USB audio's. A USB audio frame is 4 channels of 24-bit samples, each
// packed into 3 little-endian bytes: two stereo pairs, one after the other. On the Deluge side, each pair is its own
// buffer of interleaved stereo q31 samples (StereoSamples), the 24 bits being the top three bytes of each.
//
// NEON does the byte shuffling: vld4.8 splits each sample's four bytes into their own vectors, the two pairs are zipped
// together a pair of samples at a time, then vst3.8 stores just the top three bytes of each. Unpacking is the same
// backwards, with zeros for the low bytes. Being 8-bit accesses, these never fault on alignment, even in uncached
// memory
namespace deluge::usb_audio::pcm {

constexpr size_t kChannels = 4;
constexpr size_t kBytesPerSample = 3;
constexpr size_t kBytesPerFrame = kChannels * kBytesPerSample;
constexpr size_t kFramesPerVector = 4; // 4 stereo frames is 32 bytes - one vld4.8 into doubleword registers

// The three byte planes are written out one by one below, rather than looping over arrays of vectors - GCC keeps those
// in memory, spilling each vector to the stack and back, which made this several times slower

/// Interleaves two sources' bytes a stereo pair (16 bits) at a time: A.L0 A.R0 B.L0 B.R0 A.L1 ...
[[gnu::always_inline]] inline uint8x16_t interleavePairs(ArgonHalf<uint8_t> a, ArgonHalf<uint8_t> b) {
	auto const pairs = a.As<uint16_t>().ZipWith(b.As<uint16_t>());
	return Argon<uint16_t>{pairs[0], pairs[1]}.As<uint8_t>().vec();
}

/// The reverse: splits bytes in channel order back into the two sources' pairs
[[gnu::always_inline]] inline std::array<ArgonHalf<uint8_t>, 2> splitPairs(Argon<uint8_t> bytes) {
	auto const pairs = bytes.As<uint16_t>();
	auto const sources = pairs.GetLow().UnzipWith(pairs.GetHigh());
	return {sources[0].As<uint8_t>(), sources[1].As<uint8_t>()};
}

/// Packs numFrames frames: first's L/R become channels 1/2, second's 3/4. Each points to interleaved stereo q31 samples
[[gnu::always_inline]] inline void pack(uint8_t* __restrict__ out, int32_t const* __restrict__ first,
                                        int32_t const* __restrict__ second, size_t numFrames) {
	auto const* a = reinterpret_cast<uint8_t const*>(first);
	auto const* b = reinterpret_cast<uint8_t const*>(second);

	for (; numFrames >= kFramesPerVector; numFrames -= kFramesPerVector) {
		// [i] holds byte i of each of the 8 samples: L0 R0 L1 R1 ...
		auto const bytesA = ArgonHalf<uint8_t>::LoadInterleaved<4>(a);
		auto const bytesB = ArgonHalf<uint8_t>::LoadInterleaved<4>(b);

		// Byte 0 is below the 24 bits and gets dropped. The rest go out in channel order
		uint8x16x3_t const packed{{
		    interleavePairs(bytesA[1], bytesB[1]),
		    interleavePairs(bytesA[2], bytesB[2]),
		    interleavePairs(bytesA[3], bytesB[3]),
		}};
		argon::store_interleaved<kBytesPerSample, uint8_t, uint8x16_t>(out, packed);

		a += kFramesPerVector * 8;
		b += kFramesPerVector * 8;
		out += kFramesPerVector * kBytesPerFrame;
	}

	// The last few frames
	auto const* tailA = reinterpret_cast<int32_t const*>(a);
	auto const* tailB = reinterpret_cast<int32_t const*>(b);
	for (; numFrames; numFrames--) {
		int32_t const samples[kChannels] = {tailA[0], tailA[1], tailB[0], tailB[1]};
		for (int32_t sample : samples) {
			out[0] = static_cast<uint8_t>(sample >> 8);
			out[1] = static_cast<uint8_t>(sample >> 16);
			out[2] = static_cast<uint8_t>(sample >> 24);
			out += kBytesPerSample;
		}
		tailA += 2;
		tailB += 2;
	}
}

/// Unpacks numFrames frames: channels 1/2 into first, 3/4 into second, each as interleaved stereo q31 samples
[[gnu::always_inline]] inline void unpack(int32_t* __restrict__ first, int32_t* __restrict__ second,
                                          uint8_t const* __restrict__ in, size_t numFrames) {
	auto* a = reinterpret_cast<uint8_t*>(first);
	auto* b = reinterpret_cast<uint8_t*>(second);

	for (; numFrames >= kFramesPerVector; numFrames -= kFramesPerVector) {
		// [i] holds byte i (of 3) of each of the 16 samples, in channel order
		auto const packed = Argon<uint8_t>::LoadInterleaved<kBytesPerSample>(in);

		// Split them back into the two stereo sources, under a zero low byte
		auto const byte1 = splitPairs(packed[0]);
		auto const byte2 = splitPairs(packed[1]);
		auto const byte3 = splitPairs(packed[2]);
		uint8x8_t const zero = vdup_n_u8(0);
		argon::store_interleaved<4, uint8_t, uint8x8_t>(
		    a, uint8x8x4_t{{zero, byte1[0].vec(), byte2[0].vec(), byte3[0].vec()}});
		argon::store_interleaved<4, uint8_t, uint8x8_t>(
		    b, uint8x8x4_t{{zero, byte1[1].vec(), byte2[1].vec(), byte3[1].vec()}});

		in += kFramesPerVector * kBytesPerFrame;
		a += kFramesPerVector * 8;
		b += kFramesPerVector * 8;
	}

	auto* tailA = reinterpret_cast<int32_t*>(a);
	auto* tailB = reinterpret_cast<int32_t*>(b);
	for (; numFrames; numFrames--) {
		int32_t samples[kChannels];
		for (int32_t& sample : samples) {
			sample = static_cast<int32_t>((uint32_t{in[0]} << 8) | (uint32_t{in[1]} << 16) | (uint32_t{in[2]} << 24));
			in += kBytesPerSample;
		}
		tailA[0] = samples[0];
		tailA[1] = samples[1];
		tailB[0] = samples[2];
		tailB[1] = samples[3];
		tailA += 2;
		tailB += 2;
	}
}

} // namespace deluge::usb_audio::pcm
