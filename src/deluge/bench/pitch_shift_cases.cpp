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

/// @file
/// @brief The pitch-shift list's cases: the sample reader's resampled loops, and the live pitch shifter.
/// @details The reader cases call `SampleLowLevelReader::readSamplesResampled` as a sample voice does, over a resident
/// region of 32-bit frames. The shifter cases render one voice's live pitch shifter over an input ring the codec has
/// filled, the shared input buffer ingesting each block as the first voice on its channel makes it.

#include "bench/cases.h"

#include "board_config.h" // PLACE_SDRAM_BSS, the codec ring's size
#include "definitions_cxx.hpp"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/fixed/noise/cong.h"
#include "model/sample/sample.h"
#include "model/sample/sample_low_level_reader.h"
#include "synth/sources/input/audio_input_port.h"
#include "synth/sources/input/live/live_input_buffer.h"
#include "synth/sources/input/live/live_pitch_shifter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <tuple>

namespace deluge::bench {

namespace {

// ── the sample reader ──────────────────────────────────────────────────────────────────────────────────────────

/// Bytes per channel-sample of the bench region: 32-bit frames, as the reader's 24- and 32-bit files play.
constexpr int32_t kReaderByteDepth = 4;
/// Frames of the bench region: a 128-sample block at the case's rate of 1.5 reads 192, padded for the jump's
/// look-ahead.
constexpr std::size_t kReaderFrames = 512;
/// The reader cases' rate, 8.24: one and a half source samples per output sample, so blocks mix one- and two-sample
/// jumps.
constexpr int32_t kReaderRate = (3 << 24) / 2;

/// The bench region's stereo frames, a full-scale saw, read by every reader case.
alignas(16) std::array<int32_t, kReaderFrames * 2> reader_region{};

/// A sample reader over the bench region: resident, with its play position at the region's start each block.
class BenchReader : public SampleLowLevelReader {
public:
	/// Marks the region resident and puts the play position at its start.
	void rewind() {
		region_.payload_base = reinterpret_cast<char*>(reader_region.data());
		currentPlayPos = reinterpret_cast<char*>(reader_region.data());
		oscPos = 0;
	}
};

/// The rig shared by the reader cases, made on first use: the reader and the sample it plays.
struct ReaderRig {
	BenchReader reader{};
	Sample sample{};
	std::array<dsp::Q<31>, kMaxBlock * 2> out{};
	bool ready = false;
};

ReaderRig& reader_rig() {
	static ReaderRig rig;
	return rig;
}

// ── the live pitch shifter ─────────────────────────────────────────────────────────────────────────────────────

/// The codec ring the shifter cases read: SSI_RX_BUFFER_NUM_SAMPLES interleaved frames, filled once at setup.
alignas(16) std::array<int32_t, SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS> input_ring{};

/// Bytes of the shared input buffer, the stereo one with its raw ring's second half after it, as the host's pool
/// allocates it: about 66 KB.
constexpr std::size_t kInputBufferBytes = sizeof(synth::LiveInputBuffer) + kInputRawBufferSize * sizeof(int32_t);
/// The shared input buffer's storage, in SDRAM, in words so the buffer placed in it is aligned.
PLACE_SDRAM_BSS std::array<uint64_t, (kInputBufferBytes + 7) / 8> input_buffer_storage;

/// A 32-bit hash, the murmur3 finaliser: the percussive signal's noise and burst starts.
constexpr uint32_t hash32(uint32_t x) {
	x ^= x >> 16;
	x *= 0x85eb'ca6bU;
	x ^= x >> 13;
	x *= 0xc2b2'ae35U;
	return x ^ (x >> 16);
}

/// The harness's percussive test signal on one channel at frame @p n (src/bsp/rust/src/audio/host.rs).
constexpr int32_t percussive_channel(uint32_t n, const std::array<uint32_t, 5>& periods, uint32_t seed) {
	const uint32_t period = periods[(n / 4096) % 5];
	const int32_t saw = static_cast<int32_t>(((n % 4096) % period) * (UINT32_MAX / period)) >> 1;
	const uint32_t window = n / 6000;
	const uint32_t start = window * 6000 + hash32(window ^ seed) % 3000;
	int32_t burst = 0;
	if (n >= start && n - start < 256 * 12) {
		burst = (static_cast<int32_t>(hash32(n ^ seed)) >> 2) >> ((n - start) / 256);
	}
	return static_cast<int32_t>(static_cast<uint32_t>(saw) + static_cast<uint32_t>(burst));
}

/// The input port as the app's adapter gives it: the frame clock, the codec ring at the block's frames, stereo input
/// present, and the one shared buffer.
class BenchInputPort final : public synth::AudioInputPort {
public:
	[[nodiscard]] uint32_t sample_time() const override { return time_; }
	[[nodiscard]] bool stereo_input_available() const override { return true; }
	[[nodiscard]] const int32_t* ring_read_pos() const override {
		return &input_ring[(time_ % SSI_RX_BUFFER_NUM_SAMPLES) * NUM_MONO_INPUT_CHANNELS];
	}
	[[nodiscard]] const int32_t* ring_end() const override { return input_ring.data() + input_ring.size(); }
	[[nodiscard]] synth::LiveInputBuffer* buffer_for_channel(synth::InputChannel /*channel*/,
	                                                         bool /*may_create*/) override {
		return buffer_;
	}
	void report_hops(int32_t /*hops_ended*/) override {}

	void use(synth::LiveInputBuffer* buffer) {
		buffer_ = buffer;
		time_ = 0;
	}
	void advance(std::size_t n) { time_ += static_cast<uint32_t>(n); }

private:
	synth::LiveInputBuffer* buffer_ = nullptr;
	uint32_t time_ = 0;
};

/// The rig shared by the shifter cases: the port, the buffer, the shifter, the voice's noise and its output.
struct ShifterRig {
	BenchInputPort port;
	synth::LiveInputBuffer* buffer = nullptr;
	std::optional<synth::LivePitchShifter> shifter;
	dsp::fixed::Cong noise{0x1234'5678};
	std::array<dsp::Q<31>, kMaxBlock * 2> out{};
};

ShifterRig& shifter_rig() {
	static ShifterRig rig;
	return rig;
}

/// Blocks a shifter case renders untimed at setup, so its hops are in their steady pattern: a second of input.
constexpr std::size_t kShifterWarmUpFrames = 44'100;

/// The voice's source amplitude, Q31, and its per-sample step, as an unchanging note gives them.
constexpr int32_t kShifterAmplitude = 0x3000'0000;

} // namespace

void SampleReadCase::setup(std::size_t /*n*/) {
	ReaderRig& rig = reader_rig();
	if (!rig.ready) {
		for (std::size_t i = 0; i < reader_region.size(); ++i) {
			reader_region[i] = static_cast<int32_t>(static_cast<uint32_t>(i) * 0x0100'0000U);
		}
		rig.sample.byteDepth = kReaderByteDepth;
		rig.sample.numChannels = 2;
		rig.sample.workOutBitMask();
		rig.ready = true;
	}
	rig.reader.rewind();
}

void SampleReadCase::block(std::size_t n) {
	ReaderRig& rig = reader_rig();
	rig.reader.rewind();
	const int32_t channels = config_.stereo ? 2 : 1;
	dsp::Q<31>* out = rig.out.data();
	int32_t amplitude = kShifterAmplitude;
	bool started = true;
	rig.reader.readSamplesResampled(
	    &out, static_cast<int32_t>(n), &rig.sample, channels * kReaderByteDepth, channels, channels, kReaderRate,
	    &amplitude, 0, config_.linear ? 2 : kInterpolationMaxNumSamples, false, nullptr, &started, nullptr, false, 0);
}

void PitchShiftCase::setup(std::size_t /*n*/) {
	ShifterRig& rig = shifter_rig();
	for (uint32_t f = 0; f < SSI_RX_BUFFER_NUM_SAMPLES; ++f) {
		const bool perc = config_.percussive;
		input_ring[f * NUM_MONO_INPUT_CHANNELS] =
		    perc ? percussive_channel(f, {97, 131, 173, 211, 149}, 0) : static_cast<int32_t>(f * 0x0200'0000U);
		input_ring[f * NUM_MONO_INPUT_CHANNELS + 1] =
		    perc ? percussive_channel(f, {149, 97, 211, 131, 173}, 0x5bd1'e995U)
		         : static_cast<int32_t>(f * 0x0100'0000U);
	}
	if (rig.buffer != nullptr) {
		rig.buffer->~LiveInputBuffer();
	}
	input_buffer_storage.fill(0);
	rig.buffer = new (input_buffer_storage.data()) synth::LiveInputBuffer(); // NOLINT(cppcoreguidelines-owning-memory)
	rig.port.use(rig.buffer);
	const auto channel = config_.stereo ? synth::InputChannel::STEREO : synth::InputChannel::LEFT;
	rig.shifter.emplace(channel, config_.phase_increment);
	rig.noise.seed(0x1234'5678);
	for (std::size_t done = 0; done < kShifterWarmUpFrames; done += kMaxBlock) {
		block(kMaxBlock);
	}
}

void PitchShiftCase::block(std::size_t n) {
	ShifterRig& rig = shifter_rig();
	std::ignore = rig.shifter->render(rig.out.data(), static_cast<int32_t>(n), config_.phase_increment,
	                                  kShifterAmplitude, 0, kInterpolationMaxNumSamples, rig.port, rig.noise);
	rig.port.advance(n);
}

} // namespace deluge::bench
