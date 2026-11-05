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
/// @brief The live pitch shifter's workloads: input signals, an input ring as the codec fills it, the workload list,
/// and the driver that renders one into a record, as the voice's input source drives its shifter.
/// @details A record is three hashes: of every output word, of each block's hop count, and of each block's
/// removability. The oracle (reference_live_pitch_shifter.h), the legacy classes while they exist, and the dsp-next
/// component each render every workload through an adapter with the same small protocol (see @ref ReferenceSystem),
/// and must make the same records.

#pragma once

#include "board_config.h"
#include "definitions_cxx.hpp"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/fixed/noise/cong.h"
#include "reference_live_pitch_shifter.h"
#include "synth/sources/input/input_channel.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <ostream>
#include <span>
#include <vector>

namespace pitch_shift_workloads {

using deluge::synth::InputChannel;
namespace ref = deluge::dsp::test::reference_pitch_shift;

/// @brief The signal a workload feeds the input.
enum class Signal : uint8_t {
	SAW,              ///< The harness's signal 1: full-scale saws, 128 frames left, 256 right.
	PERCUSSIVE,       ///< The harness's signal 2: noise bursts over a stepped-period saw (audio/host.rs).
	SINE,             ///< A smooth periodic wave: a parabolic sine, 100 frames left, 151 right.
	SILENCE_THEN_SAW, ///< Silence for 9000 frames, then the saw: the history under one lap when shifting starts.
};

/// @brief A 32-bit integer hash, the murmur3 finaliser: signal 2's noise and burst starts, as the host computes them.
[[nodiscard]] constexpr uint32_t hash32(uint32_t x) {
	x ^= x >> 16;
	x *= 0x85eb'ca6bU;
	x ^= x >> 13;
	x *= 0xc2b2'ae35U;
	return x ^ (x >> 16);
}

/// @brief Signal 2's sample on one channel at frame @p n: the host's `percussive_channel`, bit for bit.
[[nodiscard]] constexpr int32_t percussive_channel(uint32_t n, const std::array<uint32_t, 5>& periods, uint32_t seed) {
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

/// @brief A parabolic sine of @p period frames at frame @p n, Q31: 4x(1 − |x|) over the phase as a Q31 x.
[[nodiscard]] constexpr int32_t parabolic_sine(uint32_t n, uint32_t period) {
	const auto x = static_cast<int32_t>((n % period) * (UINT32_MAX / period));
	const int64_t magnitude = x < 0 ? -static_cast<int64_t>(x) : x;
	return static_cast<int32_t>((static_cast<int64_t>(x) * (INT32_MAX - magnitude)) >> 29);
}

/// @brief One interleaved input frame of @p signal at frame @p n.
[[nodiscard]] constexpr std::array<int32_t, 2> input_frame(Signal signal, uint32_t n) {
	switch (signal) {
	case Signal::PERCUSSIVE:
		return {percussive_channel(n, {97, 131, 173, 211, 149}, 0),
		        percussive_channel(n, {149, 97, 211, 131, 173}, 0x5bd1'e995U)};
	case Signal::SINE:
		return {parabolic_sine(n, 100), parabolic_sine(n, 151)};
	case Signal::SILENCE_THEN_SAW:
		if (n < 9000) {
			return {0, 0};
		}
		[[fallthrough]];
	case Signal::SAW:
	default:
		return {static_cast<int32_t>(n * 0x0200'0000U), static_cast<int32_t>(n * 0x0100'0000U)};
	}
}

/// @brief The codec's input ring: SSI_RX_BUFFER_NUM_SAMPLES interleaved frames, written a block at a time.
class InputRing {
public:
	static constexpr std::size_t kFrames = SSI_RX_BUFFER_NUM_SAMPLES;

	/// @brief Writes @p n frames of @p signal from frame @p first at the ring's cursor.
	/// @return The block's first frame, where the shifter starts reading it.
	const int32_t* write(Signal signal, uint32_t first, std::size_t n) {
		const int32_t* block = &words_[cursor_ * NUM_MONO_INPUT_CHANNELS];
		for (std::size_t i = 0; i < n; ++i) {
			const auto frame = input_frame(signal, first + static_cast<uint32_t>(i));
			words_[cursor_ * NUM_MONO_INPUT_CHANNELS] = frame[0];
			words_[cursor_ * NUM_MONO_INPUT_CHANNELS + 1] = frame[1];
			cursor_ = (cursor_ + 1) % kFrames;
		}
		return block;
	}
	[[nodiscard]] const int32_t* end() const { return words_.data() + words_.size(); }

private:
	std::array<int32_t, kFrames * NUM_MONO_INPUT_CHANNELS> words_{};
	std::size_t cursor_ = 0;
};

/// @brief A buffer of the input buffer's type in zeroed storage, the stereo one with its raw ring's second half after
/// it, as the host's pool allocates it.
template <typename Buffer>
class PooledBuffer {
public:
	explicit PooledBuffer(InputChannel channel) {
		const std::size_t size =
		    sizeof(Buffer) + (channel == InputChannel::STEREO ? kInputRawBufferSize * sizeof(int32_t) : 0);
		storage_.assign(size, std::byte{0});
		// Placed in the zeroed storage, destroyed by the destructor
		buffer_ = new (storage_.data())
		    Buffer(); // NOLINT(cppcoreguidelines-owning-memory,cppcoreguidelines-prefer-member-initializer)
	}
	~PooledBuffer() { buffer_->~Buffer(); }
	PooledBuffer(const PooledBuffer&) = delete;
	PooledBuffer& operator=(const PooledBuffer&) = delete;
	[[nodiscard]] Buffer* get() const { return buffer_; }

private:
	std::vector<std::byte> storage_;
	Buffer* buffer_ = nullptr;
};

/// @brief What one block hands a system: the clock, the ring, and the voice's render arguments.
struct BlockInput {
	uint32_t time;               ///< The frame clock at the block's start.
	const int32_t* read_pos;     ///< The block's first input frame.
	const int32_t* ring_end;     ///< One past the ring.
	int32_t phase_increment;     ///< 8.24.
	int32_t amplitude;           ///< Q31.
	int32_t amplitude_increment; ///< Q31 per sample.
	int32_t interpolation_size;  ///< 2 or kInterpolationMaxNumSamples.
};

/// @brief The oracle as a system: one shared input buffer and up to two shifters on it.
/// @details The protocol every system gives the driver: `create(slot, channel, phase)`, `destroy(slot)`,
/// `exists(slot)`, `removable(slot)`, and `render(slot, out, n, block, noise)` returning the hops.
class ReferenceSystem {
public:
	explicit ReferenceSystem(InputChannel channel) : buffer_(channel) {}
	void create(std::size_t slot, InputChannel channel, int32_t phase) {
		shifters_[slot] = std::make_unique<ref::ReferenceLivePitchShifter>(channel, phase);
	}
	void destroy(std::size_t slot) { shifters_[slot].reset(); }
	[[nodiscard]] bool exists(std::size_t slot) const { return shifters_[slot] != nullptr; }
	[[nodiscard]] bool removable(std::size_t slot) const { return shifters_[slot]->may_be_removed_without_click(); }
	[[nodiscard]] int32_t render(std::size_t slot, deluge::dsp::Q<31>* out, int32_t n, const BlockInput& b,
	                             deluge::dsp::fixed::Cong& noise) {
		const ref::ReferencePort port{.time = b.time, .read_pos = b.read_pos, .end = b.ring_end};
		return shifters_[slot]->render(out, n, b.phase_increment, b.amplitude, b.amplitude_increment,
		                               b.interpolation_size, port, buffer_.get(), noise);
	}

private:
	PooledBuffer<ref::ReferenceInputBuffer> buffer_;
	std::array<std::unique_ptr<ref::ReferenceLivePitchShifter>, 2> shifters_{};
};

/// @brief The workloads, in record order.
enum class WorkloadId : uint8_t {
	UP7_SAW_LEFT,
	UP7_SAW_RIGHT,
	UP7_SAW_STEREO,
	UP7_PERC_LEFT,
	UP7_PERC_STEREO,
	DOWN12_PERC_LEFT,
	DOWN12_PERC_STEREO,
	DOWN24_SAW_LEFT,
	DOWN5_SINE_LEFT,
	DOWN1_SINE_LEFT,
	UP_HALF_SINE_LEFT,
	UP1_SINE_LEFT,
	UP5_PERC_LEFT,
	UP12_SINE_STEREO,
	UP24_PERC_LEFT,
	UP24_SAW_STEREO,
	UP7_LINEAR_PERC_LEFT,
	DOWN12_LINEAR_SINE_STEREO,
	UP7_BLOCK1_PERC_LEFT,
	DOWN12_BLOCK7_PERC_LEFT,
	UP7_BLOCK32_SINE_STEREO,
	GLIDE_PERC_LEFT,
	GLIDE_SAW_STEREO,
	SHARED_UP7_PERC_LEFT,
	SHARED_DOWN5_PERC_STEREO,
	SILENCE_UP7_LEFT,
	SILENCE_DOWN12_LEFT,
	COUNT,
};
inline constexpr std::size_t kWorkloadCount = static_cast<std::size_t>(WorkloadId::COUNT);

/// @brief Phase increments, 8.24, for whole and half semitones: 2^24 · 2^(s/12), rounded.
inline constexpr int32_t kDown24 = 4194304;
inline constexpr int32_t kDown12 = 8388608;
inline constexpr int32_t kDown7 = 11197448;
inline constexpr int32_t kDown5 = 12568711;
inline constexpr int32_t kDown1 = 15835583;
inline constexpr int32_t kUnity = kMaxSampleValue;
inline constexpr int32_t kUpHalf = 17268826;
inline constexpr int32_t kUp1 = 17774841;
inline constexpr int32_t kUp5 = 22394897;
inline constexpr int32_t kUp7 = 25137421;
inline constexpr int32_t kUp12 = 33554432;
inline constexpr int32_t kUp24 = 67108864;

/// @brief One workload: a shifter (two, with @ref shared) rendered block by block, as a voice's input source drives it.
struct Workload {
	WorkloadId id;
	const char* name;
	InputChannel channel;
	Signal signal;
	int32_t interpolation_size; ///< 2 or kInterpolationMaxNumSamples.
	int32_t block_length;       ///< Frames per block, 1 … 128.
	int32_t blocks;
	std::vector<int32_t> phases; ///< One phase increment per block; the last holds for the rest.
	int32_t amplitude = 0x3000'0000;
	int32_t amplitude_increment = 0x1000;
	bool shared = false; ///< A second shifter on the same buffer, from block 8.

	[[nodiscard]] int32_t phase_at(int32_t block) const {
		return phases[std::min(static_cast<std::size_t>(block), phases.size() - 1)];
	}
};

/// @brief The glide: at +7, down to unity (where the shifter is removed), down to −7, back to unity, then a jump to
/// +24 (a fresh shifter), each ramp linear in the phase increment.
inline std::vector<int32_t> glide_phases() {
	std::vector<int32_t> p;
	const auto hold = [&](int32_t phase, int32_t blocks) { p.insert(p.end(), blocks, phase); };
	const auto ramp = [&](int32_t from, int32_t to, int32_t blocks) {
		for (int32_t i = 1; i <= blocks; ++i) {
			p.push_back(from + static_cast<int32_t>((static_cast<int64_t>(to - from) * i) / blocks));
		}
	};
	hold(kUp7, 100);
	ramp(kUp7, kUnity, 100);
	hold(kUnity, 100);
	ramp(kUnity, kDown7, 100);
	hold(kDown7, 200);
	hold(kUnity, 100);
	hold(kUp24, 100);
	return p;
}

/// @brief Every workload, in WorkloadId order.
inline const std::vector<Workload>& all_workloads() {
	using enum WorkloadId;
	constexpr auto kL = InputChannel::LEFT;
	constexpr auto kR = InputChannel::RIGHT;
	constexpr auto kS = InputChannel::STEREO;
	constexpr int32_t kSinc = kInterpolationMaxNumSamples;
	// NOLINTNEXTLINE(bugprone-dynamic-static-initializers): built once, on first use
	static const std::vector<Workload> w{
	    {UP7_SAW_LEFT, "UP7_SAW_LEFT", kL, Signal::SAW, kSinc, 128, 600, {kUp7}},
	    {UP7_SAW_RIGHT, "UP7_SAW_RIGHT", kR, Signal::SAW, kSinc, 128, 600, {kUp7}},
	    {UP7_SAW_STEREO, "UP7_SAW_STEREO", kS, Signal::SAW, kSinc, 128, 600, {kUp7}},
	    {UP7_PERC_LEFT, "UP7_PERC_LEFT", kL, Signal::PERCUSSIVE, kSinc, 128, 800, {kUp7}},
	    {UP7_PERC_STEREO, "UP7_PERC_STEREO", kS, Signal::PERCUSSIVE, kSinc, 128, 800, {kUp7}},
	    {DOWN12_PERC_LEFT, "DOWN12_PERC_LEFT", kL, Signal::PERCUSSIVE, kSinc, 128, 800, {kDown12}},
	    {DOWN12_PERC_STEREO, "DOWN12_PERC_STEREO", kS, Signal::PERCUSSIVE, kSinc, 128, 800, {kDown12}},
	    {DOWN24_SAW_LEFT, "DOWN24_SAW_LEFT", kL, Signal::SAW, kSinc, 128, 800, {kDown24}},
	    {DOWN5_SINE_LEFT, "DOWN5_SINE_LEFT", kL, Signal::SINE, kSinc, 128, 600, {kDown5}},
	    {DOWN1_SINE_LEFT, "DOWN1_SINE_LEFT", kL, Signal::SINE, kSinc, 128, 600, {kDown1}},
	    {UP_HALF_SINE_LEFT, "UP_HALF_SINE_LEFT", kL, Signal::SINE, kSinc, 128, 600, {kUpHalf}},
	    {UP1_SINE_LEFT, "UP1_SINE_LEFT", kL, Signal::SINE, kSinc, 128, 600, {kUp1}},
	    {UP5_PERC_LEFT, "UP5_PERC_LEFT", kL, Signal::PERCUSSIVE, kSinc, 128, 800, {kUp5}},
	    {UP12_SINE_STEREO, "UP12_SINE_STEREO", kS, Signal::SINE, kSinc, 128, 600, {kUp12}},
	    {UP24_PERC_LEFT, "UP24_PERC_LEFT", kL, Signal::PERCUSSIVE, kSinc, 128, 800, {kUp24}},
	    {UP24_SAW_STEREO, "UP24_SAW_STEREO", kS, Signal::SAW, kSinc, 128, 600, {kUp24}},
	    {UP7_LINEAR_PERC_LEFT, "UP7_LINEAR_PERC_LEFT", kL, Signal::PERCUSSIVE, 2, 128, 800, {kUp7}},
	    {DOWN12_LINEAR_SINE_STEREO, "DOWN12_LINEAR_SINE_STEREO", kS, Signal::SINE, 2, 128, 600, {kDown12}},
	    {UP7_BLOCK1_PERC_LEFT, "UP7_BLOCK1_PERC_LEFT", kL, Signal::PERCUSSIVE, kSinc, 1, 40000, {kUp7}},
	    {DOWN12_BLOCK7_PERC_LEFT, "DOWN12_BLOCK7_PERC_LEFT", kL, Signal::PERCUSSIVE, kSinc, 7, 8000, {kDown12}},
	    {UP7_BLOCK32_SINE_STEREO, "UP7_BLOCK32_SINE_STEREO", kS, Signal::SINE, kSinc, 32, 2400, {kUp7}},
	    {GLIDE_PERC_LEFT, "GLIDE_PERC_LEFT", kL, Signal::PERCUSSIVE, kSinc, 128, 800, glide_phases()},
	    {GLIDE_SAW_STEREO, "GLIDE_SAW_STEREO", kS, Signal::SAW, kSinc, 128, 800, glide_phases()},
	    {SHARED_UP7_PERC_LEFT,
	     "SHARED_UP7_PERC_LEFT",
	     kL,
	     Signal::PERCUSSIVE,
	     kSinc,
	     128,
	     800,
	     {kUp7},
	     0x3000'0000,
	     0x1000,
	     true},
	    {SHARED_DOWN5_PERC_STEREO,
	     "SHARED_DOWN5_PERC_STEREO",
	     kS,
	     Signal::PERCUSSIVE,
	     kSinc,
	     128,
	     800,
	     {kDown5},
	     0x2000'0000,
	     -0x800,
	     true},
	    {SILENCE_UP7_LEFT, "SILENCE_UP7_LEFT", kL, Signal::SILENCE_THEN_SAW, kSinc, 128, 400, {kUp7}},
	    {SILENCE_DOWN12_LEFT, "SILENCE_DOWN12_LEFT", kL, Signal::SILENCE_THEN_SAW, kSinc, 128, 400, {kDown12}},
	};
	return w;
}

/// @brief What a system made of a workload: hashes of its outputs, its hop counts and its removability.
struct WorkloadRecord {
	uint64_t outputs = 0;
	uint64_t hops = 0;
	uint64_t lifecycle = 0;
	bool operator==(const WorkloadRecord&) const = default;
};

inline std::ostream& operator<<(std::ostream& out, const WorkloadRecord& r) {
	return out << std::hex << "{.outputs = 0x" << r.outputs << "ULL, .hops = 0x" << r.hops << "ULL, .lifecycle = 0x"
	           << r.lifecycle << "ULL}" << std::dec;
}

/// @brief A 64-bit FNV-1a hash over 32-bit words.
class Fnv {
public:
	void add(uint32_t word) {
		for (int i = 0; i < 4; ++i) {
			hash_ = (hash_ ^ ((word >> (8 * i)) & 0xFFU)) * 0x100'0000'01b3ULL;
		}
	}
	[[nodiscard]] uint64_t value() const { return hash_; }

private:
	uint64_t hash_ = 0xcbf2'9ce4'8422'2325ULL;
};

/// @brief The noise stream's seed: one stream per workload, drawn by every shifter in order.
inline constexpr uint32_t kNoiseSeed = 0x1234'5678;

/// @brief A slot's state for the lifecycle hash: 2 absent, 1 removable without a click, 0 not.
template <typename System>
[[nodiscard]] uint32_t slot_state(const System& system, std::size_t slot) {
	if (!system.exists(slot)) {
		return 2;
	}
	return system.removable(slot) ? 1 : 0;
}

/// @brief One voice's block on @p slot, as `InputSource::render_input` runs it: the shifter created when the phase
/// leaves unity, removed at unity once it may be without a click, and rendered into a zeroed block when it exists.
template <typename System>
void voice_block(System& system, std::size_t slot, const BlockInput& input, const Workload& w,
                 std::vector<deluge::dsp::Q<31>>& out, deluge::dsp::fixed::Cong& noise, Fnv& outputs, Fnv& hops) {
	if (input.phase_increment != kUnity) {
		if (!system.exists(slot)) {
			system.create(slot, w.channel, input.phase_increment);
		}
	}
	else if (system.exists(slot) && system.removable(slot)) {
		system.destroy(slot);
	}
	if (system.exists(slot)) {
		std::fill(out.begin(), out.end(), deluge::dsp::Q<31>{});
		hops.add(static_cast<uint32_t>(system.render(slot, out.data(), w.block_length, input, noise)));
		for (const deluge::dsp::Q<31> word : out) {
			outputs.add(static_cast<uint32_t>(word.raw()));
		}
	}
}

/// @brief Renders @p w through @p System and hashes what it made.
/// @details Each block runs @ref voice_block for each shifter slot (the second from block 8, when shared), hashing
/// the output words and hop count of each render and each slot's @ref slot_state.
template <typename System>
[[nodiscard]] WorkloadRecord record(const Workload& w) {
	System system(w.channel);
	InputRing ring;
	Fnv outputs;
	Fnv hops;
	Fnv lifecycle;
	deluge::dsp::fixed::Cong noise(kNoiseSeed);
	const std::size_t channels = w.channel == InputChannel::STEREO ? 2 : 1;
	std::vector<deluge::dsp::Q<31>> out(static_cast<std::size_t>(w.block_length) * channels);
	uint32_t time = 0;
	for (int32_t b = 0; b < w.blocks; ++b) {
		const int32_t phase = w.phase_at(b);
		const int32_t* read_pos = ring.write(w.signal, time, static_cast<std::size_t>(w.block_length));
		const BlockInput input{.time = time,
		                       .read_pos = read_pos,
		                       .ring_end = ring.end(),
		                       .phase_increment = phase,
		                       .amplitude = w.amplitude,
		                       .amplitude_increment = w.amplitude_increment,
		                       .interpolation_size = w.interpolation_size};
		for (std::size_t slot = 0; slot < (w.shared ? 2U : 1U); ++slot) {
			if (slot == 0 || b >= 8) {
				voice_block(system, slot, input, w, out, noise, outputs, hops);
				lifecycle.add(slot_state(system, slot));
			}
		}
		time += static_cast<uint32_t>(w.block_length);
	}
	return {.outputs = outputs.value(), .hops = hops.value(), .lifecycle = lifecycle.value()};
}

} // namespace pitch_shift_workloads
