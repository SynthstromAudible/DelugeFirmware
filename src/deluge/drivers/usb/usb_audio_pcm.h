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

#include <algorithm>
#include <argon.hpp>
#include <array>
#include <cstddef>
#include <cstdint>

// Converting between the Deluge's samples and multitrack USB audio's (see r_usb_paudio_config.h), which are 16-bit and
// little-endian.
//
// To the host, a frame is 8 channels: four stereo pairs, each from its own buffer of interleaved stereo q31 samples
// (StereoSamples). A pair narrowed to 16-bit is 32 bits - one lane - so a 4-lane interleaving store (vst4.32) puts
// four sources' pairs side by side, in channel order: one instruction for every 4 frames.
//
// From the host, a frame is 4 channels: two stereo pairs, each going to its own buffer of interleaved stereo q31. A
// 2-lane deinterleaving load (vld2.32) splits the pairs apart, already interleaved L/R as the Deluge's are, and then
// each sample only needs widening.
namespace deluge::usb_audio::pcm {

constexpr size_t kToHostSources = 4;
constexpr size_t kToHostBytesPerFrame = kToHostSources * 2 * sizeof(int16_t);
constexpr size_t kFromHostDestinations = 2;
constexpr size_t kFromHostBytesPerFrame = kFromHostDestinations * 2 * sizeof(int16_t);
constexpr size_t kFramesPerVector = 4; // A q register of interleaved stereo q31 holds 2 frames; we take two at a time

/// How far right to shift a source's q31 samples to get 16-bit ones. The main output is at full scale already. The
/// track buses are at the level tracks mix at, which is 5 doublings below - the same gain SampleRecorder applies when
/// recording the song's mix
constexpr int kMainShift = 16;
constexpr int kTrackBusShift = 16 - 5;

/// One sample, as the vector code does it: rounded, and saturated to 16 bits
template <int shift>
[[gnu::always_inline]] inline int16_t narrowSample(int32_t sample) {
	int64_t const rounded = (static_cast<int64_t>(sample) + (int64_t{1} << (shift - 1))) >> shift;
	return static_cast<int16_t>(std::clamp<int64_t>(rounded, INT16_MIN, INT16_MAX));
}

/// 4 frames of a stereo q31 source narrowed to 16-bit, each frame's pair in one 32-bit lane
template <int shift>
[[gnu::always_inline]] inline Argon<uint32_t> narrowFrames(int32_t const* source) {
	auto const first = Argon<int32_t>::Load(source).ShiftRightRoundSaturateNarrow<shift>();
	auto const second = Argon<int32_t>::Load(source + 4).ShiftRightRoundSaturateNarrow<shift>();
	return first.CombineWith(second).template As<uint32_t>();
}

/// Packs numFrames 8-channel frames: main's L/R become channels 1/2, buses[0] 3/4, buses[1] 5/6 and buses[2] 7/8. Each
/// source is interleaved stereo q31; a null bus is silent. out needs only 4-byte alignment, and may be uncached
/// (Normal, non-cacheable) memory
[[gnu::always_inline]] inline void packToHost(uint32_t* __restrict__ out, int32_t const* __restrict__ main,
                                              std::array<int32_t const*, kToHostSources - 1> buses, size_t numFrames) {
	// A silent bus reads the same 4 frames of zeros over and over, rather than branching in the loop. Named variables
	// rather than arrays, which GCC keeps on the stack
	alignas(16) static constexpr int32_t kSilence[kFramesPerVector * 2] = {};
	int32_t const* bus0 = (buses[0] != nullptr) ? buses[0] : kSilence;
	int32_t const* bus1 = (buses[1] != nullptr) ? buses[1] : kSilence;
	int32_t const* bus2 = (buses[2] != nullptr) ? buses[2] : kSilence;
	size_t const step0 = (buses[0] != nullptr) ? kFramesPerVector * 2 : 0;
	size_t const step1 = (buses[1] != nullptr) ? kFramesPerVector * 2 : 0;
	size_t const step2 = (buses[2] != nullptr) ? kFramesPerVector * 2 : 0;

	for (; numFrames >= kFramesPerVector; numFrames -= kFramesPerVector) {
		argon::store_interleaved<kToHostSources>(
		    out, std::array<Argon<uint32_t>, kToHostSources>{
		             narrowFrames<kMainShift>(main), narrowFrames<kTrackBusShift>(bus0),
		             narrowFrames<kTrackBusShift>(bus1), narrowFrames<kTrackBusShift>(bus2)});
		out += kFramesPerVector * kToHostSources;
		main += kFramesPerVector * 2;
		bus0 += step0;
		bus1 += step1;
		bus2 += step2;
	}

	// The last few frames
	auto* outSamples = reinterpret_cast<int16_t*>(out);
	std::array<int32_t const*, kToHostSources - 1> const source{bus0, bus1, bus2};
	std::array<size_t, kToHostSources - 1> const step{step0, step1, step2};
	for (size_t frame = 0; frame < numFrames; frame++) {
		*outSamples++ = narrowSample<kMainShift>(main[frame * 2]);
		*outSamples++ = narrowSample<kMainShift>(main[frame * 2 + 1]);
		for (size_t i = 0; i < source.size(); i++) {
			size_t const offset = step[i] ? frame * 2 : 0;
			*outSamples++ = narrowSample<kTrackBusShift>(source[i][offset]);
			*outSamples++ = narrowSample<kTrackBusShift>(source[i][offset + 1]);
		}
	}
}

/// 4 frames' worth of one stereo pair from the host, widened to q31 and stored interleaved
[[gnu::always_inline]] inline void widenFrames(int32_t* out, Argon<int16_t> samples) {
	samples.GetLow().ShiftLeftLong<16>().StoreTo(out);
	samples.GetHigh().ShiftLeftLong<16>().StoreTo(out + 4);
}

[[gnu::always_inline]] inline int32_t widenSample(int16_t sample) {
	return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint16_t>(sample)) << 16);
}

/// Unpacks numFrames 4-channel frames from the host: channels 1/2 into first, 3/4 into second, each interleaved stereo
/// q31. in may be uncached (Normal, non-cacheable) memory, and needs only 4-byte alignment
[[gnu::always_inline]] inline void unpackFromHost(int32_t* __restrict__ first, int32_t* __restrict__ second,
                                                  int16_t const* __restrict__ in, size_t numFrames) {
	for (; numFrames >= kFramesPerVector; numFrames -= kFramesPerVector) {
		// Each frame's pairs are a 32-bit lane apiece, so deinterleaving by lane separates them
		auto const pairs =
		    Argon<uint32_t>::LoadInterleaved<kFromHostDestinations>(reinterpret_cast<uint32_t const*>(in));
		widenFrames(first, pairs[0].As<int16_t>());
		widenFrames(second, pairs[1].As<int16_t>());
		in += kFramesPerVector * kFromHostDestinations * 2;
		first += kFramesPerVector * 2;
		second += kFramesPerVector * 2;
	}
	for (size_t frame = 0; frame < numFrames; frame++) {
		first[frame * 2] = widenSample(in[frame * 4]);
		first[frame * 2 + 1] = widenSample(in[frame * 4 + 1]);
		second[frame * 2] = widenSample(in[frame * 4 + 2]);
		second[frame * 2 + 1] = widenSample(in[frame * 4 + 3]);
	}
}

} // namespace deluge::usb_audio::pcm
