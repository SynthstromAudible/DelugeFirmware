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

// DelayProcessor: one block of the delay over the tapes it is handed. Exact reference values, recorded from the legacy
// Delay::process, pin 20 blocks of 100 samples per case: each block's output (a 64-bit FNV-1a digest of every word),
// whether the block wrapped, and the two tapes' sizes after it, so the secondary tape's growth and swap are pinned
// too. The spec's Owner is a line-for-line mirror of the lifecycle Delay keeps around the processor: activation (the
// first tape is a secondary, read as silence until it is swapped in), the rate's settling, a secondary tape grown
// when the rate leaves the primary's range, the swap once it is full, and the abandon count's wraps. Fresh storage
// holds the recorder's allocator fill, so the samples a fresh tape has not cleared are pinned too.
//
// The recorded workloads (delay_workloads.h) run every mode combination, rate sweeps, secondaries grown through their
// swaps, read-head wraps, a new delay's first blocks, both ends of the rate range and delays shorter than a block
// through the same lifecycle. The processor must render each as the frame-order reference (reference_delay_processor.h,
// the delay line's order) does; what the frame order and the staged order render from each is pinned.

#include "delay_workloads.h"
#include "dsp-next/fixed/delay/delay_processor.h"
#include "reference_delay_processor.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using deluge::dsp::StereoSample;
using deluge::dsp::fixed::DelayBlock;
using deluge::dsp::fixed::DelayBlockResult;
using deluge::dsp::fixed::DelayProcessor;
using deluge::dsp::fixed::DelayTape;

namespace {

inline constexpr size_t kBlocks = 20;
inline constexpr size_t kBlockSize = 100;
/// The block whose opening samples are spelled out: the first that reads the primary tape.
inline constexpr size_t kWindowBlock = 6;
/// The delay's rate in every case's opening blocks: a 512-sample tape.
inline constexpr int32_t kNativeRate = 1 << 29;

/// The fill a fresh allocation holds: word k of the storage.
inline int32_t pattern(size_t word) {
	return static_cast<int32_t>(0x5A5A0000u + static_cast<uint32_t>(word));
}

/// The k-th input sample: two unrelated pseudo-random channels, 28 bits of magnitude, for the first 800 samples.
inline StereoSample input(int32_t k) {
	if (k >= 800) {
		return {.l = 0, .r = 0};
	}
	const uint32_t a = static_cast<uint32_t>(k + 1) * 2654435761u;
	const uint32_t b = static_cast<uint32_t>(k + 7) * 2246822519u;
	return {.l = static_cast<int32_t>(a) >> 3, .r = static_cast<int32_t>(b) >> 3};
}

/// A 64-bit FNV-1a over 32-bit words.
inline uint64_t digest(const std::vector<int32_t>& words) {
	uint64_t h = 14695981039346656037ull;
	for (int32_t w : words) {
		h = (h ^ static_cast<uint32_t>(w)) * 1099511628211ull;
	}
	return h;
}

/// One block's settings.
struct Settings {
	int32_t rate = kNativeRate;
	int32_t feedback = 0x30000000;
	bool analog = false;
	int32_t saturation = 8;
	bool ping_pong = false;
	bool stereo = true;
};

struct Case {
	std::array<Settings, kBlocks> block{};
};

/// Native rate, digital clipping, straight feedback.
inline Case digital() {
	return {};
}

/// Native rate, the analog impulse response then tanh at @p saturation.
inline Case analog(int32_t saturation) {
	Case c{};
	for (Settings& s : c.block) {
		s.feedback = 0x38000000;
		s.analog = true;
		s.saturation = saturation;
	}
	return c;
}

/// Ping-pong, the engine rendering in stereo or not.
inline Case pingPong(bool stereo) {
	Case c{};
	for (Settings& s : c.block) {
		s.ping_pong = true;
		s.stereo = stereo;
	}
	return c;
}

/// The rate rises to 1.5x (the primary resamples), then to 2.5x (a smaller secondary tape, swapped in), then falls
/// below half (a larger secondary tape, still being written at the end).
inline Case rateChange() {
	Case c{};
	for (size_t b = 0; b < kBlocks; ++b) {
		c.block[b].rate = b < 8 ? kNativeRate : b < 12 ? 805306368 : b < 16 ? 1342177280 : 1 << 27;
	}
	return c;
}

/// A 256-sample tape whose rate falls to a quarter: the secondary tape is grown at half the new rate (2048 samples),
/// so its read head steps twice per output sample until it is swapped in.
inline Case doubleSpeedSecondary() {
	Case c{};
	for (size_t b = 0; b < kBlocks; ++b) {
		c.block[b].rate = b < 4 ? 1 << 30 : 1 << 28;
	}
	return c;
}

/// What 20 blocks did: each block's output digest and whether it wrapped, the tapes' sizes after it (0 when
/// inactive), and the opening 8 samples (interleaved) of block kWindowBlock.
struct Record {
	std::vector<uint64_t> digests;
	std::vector<int32_t> wrapped;
	std::vector<int32_t> primary_size;
	std::vector<int32_t> secondary_size;
	std::vector<int32_t> window;

	bool operator==(const Record&) const = default;
};

std::ostream& operator<<(std::ostream& out, const Record& r) {
	auto list = [&](const char* name, const auto& values) {
		out << ' ' << name;
		for (const auto v : values) {
			out << ' ' << v;
		}
	};
	out << std::hex << "{digests";
	for (const uint64_t d : r.digests) {
		out << " 0x" << d;
	}
	out << std::dec << ',';
	list("wrapped", r.wrapped);
	list(", primary_size", r.primary_size);
	list(", secondary_size", r.secondary_size);
	list(", window", r.window);
	return out << '}';
}

/// Native rate, digital clipping, straight feedback.
const Record kDigital{
    .digests = {0x3699dd083fe6160bull, 0x0c7a52609b4a3c1cull, 0x7ac286bcd0601e3bull, 0x2f26d69d4a448dc2ull,
                0xb8d77da63e641723ull, 0x9628030da502db50ull, 0x4fb782fa3d27d2c7ull, 0xf60bde04a5ccf71dull,
                0x450a68dfa62f573full, 0x6fb7d688c349c7d0ull, 0x14ade03d2f3afc35ull, 0x8656d581c3c43bdbull,
                0xa34ebf198af13b0cull, 0x532b717c8c4c54e1ull, 0x4e511d3169515e72ull, 0xca07e2e205d946d9ull,
                0x5acc26d0b3921ce6ull, 0xd647f72bb6b2a8ddull, 0x9bf1aad818b0449full, 0x354e301e3126f06eull},
    .wrapped = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0},
    .primary_size = {0, 0, 0, 0, 0, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512},
    .secondary_size = {512, 512, 512, 512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    .window = {237400466, -369128011, -121391697, 122321056, -77652223, -325660197, 100380100, 165779771, 144073171,
               -282210574, -214811799, 209220301, -36750750, -238779129, 6971038, 252642670}};

/// The analog impulse response then tanh at a sound's drive (8).
const Record kAnalog8{
    .digests = {0x3699dd083fe6160bull, 0x0c7a52609b4a3c1cull, 0x7ac286bcd0601e3bull, 0x2f26d69d4a448dc2ull,
                0xb8d77da63e641723ull, 0x9628030da502db50ull, 0x2eec036bdc7f2b51ull, 0xae18fc4c798adf6cull,
                0x78aaff7bb64975b0ull, 0xae5a3cc63078fcd5ull, 0x57968cebb6c03265ull, 0x0bf205924a76fc22ull,
                0x6e76ee022f1dac30ull, 0x181eec8b07946a8dull, 0x872eaf9990cdcc61ull, 0xd652150026401416ull,
                0x3b985ba641213225ull, 0xc92a3c4bc95db864ull, 0x400afed860e2ff4cull, 0x6b46c6cd6c2902a8ull},
    .wrapped = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0},
    .primary_size = {0, 0, 0, 0, 0, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512},
    .secondary_size = {512, 512, 512, 512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    .window = {235376430, -247185354, 30448642, 33191130, -175190415, -222974235, 156334437, 57284088, -49347434,
               -198149327, -253383429, 82823344, 79850382, -171504059, -123473617, 109646236}};

/// The analog impulse response then tanh at a host chain's drive (5).
const Record kAnalog5{
    .digests = {0x3699dd083fe6160bull, 0x0c7a52609b4a3c1cull, 0x7ac286bcd0601e3bull, 0x2f26d69d4a448dc2ull,
                0xb8d77da63e641723ull, 0x9628030da502db50ull, 0x6dfb5c89c66906a9ull, 0x81ecee2b8171e02bull,
                0x54bb65dda231b60cull, 0xa220e113f92603bdull, 0x5c490a24d4492176ull, 0x33510c232d7bcb7cull,
                0xa9fb4eff209f1233ull, 0x616518c160872ba7ull, 0x48f86766eea735d3ull, 0xfc1cb7c4cb365ee0ull,
                0xdc412c49be2fa79bull, 0x75d7cf2ab9ed2766ull, 0x788321edf0ab261dull, 0x06db37b38e38056dull},
    .wrapped = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0},
    .primary_size = {0, 0, 0, 0, 0, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512},
    .secondary_size = {512, 512, 512, 512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    .window = {235376402, -247185358, 30448614, 33190998, -175190607, -222974495, 156333030, 57277907, -49358068,
               -198149628, -253383467, 82823195, 79856833, -171494701, -123355048, 109664405}};

/// Ping-pong, the engine rendering in stereo: the channels cross.
const Record kPingPongStereo{
    .digests = {0x3699dd083fe6160bull, 0x0c7a52609b4a3c1cull, 0x7ac286bcd0601e3bull, 0x2f26d69d4a448dc2ull,
                0xb8d77da63e641723ull, 0x9628030da502db50ull, 0x5f690aee2320e3d8ull, 0xa6401f8b0a8581f5ull,
                0x5352911348d936d9ull, 0x24789d86079651b6ull, 0x08b424bd88f79912ull, 0xcf489f952c314496ull,
                0x99512312b6c29df4ull, 0x84cb9742694fd235ull, 0xffc359156c867b9full, 0x111c3d6c603f3293ull,
                0x28984f3fcbc499aaull, 0xc4b9da65b46c4c6dull, 0x7a610ff6fc1712b3ull, 0xed18846f4253286dull},
    .wrapped = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0},
    .primary_size = {0, 0, 0, 0, 0, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512},
    .secondary_size = {512, 512, 512, 512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    .window = {235378193, -307198561, 30311751, 2089517, -174754691, -225507202, 157049779, 83753118, -48016663,
               -143871349, -253083105, 165361236, 78721365, -263519240, -126345076, 247012181}};

/// Ping-pong, the engine rendering in mono: straight feedback, the same as kDigital.
const Record kPingPongMono{
    .digests = {0x3699dd083fe6160bull, 0x0c7a52609b4a3c1cull, 0x7ac286bcd0601e3bull, 0x2f26d69d4a448dc2ull,
                0xb8d77da63e641723ull, 0x9628030da502db50ull, 0x4fb782fa3d27d2c7ull, 0xf60bde04a5ccf71dull,
                0x450a68dfa62f573full, 0x6fb7d688c349c7d0ull, 0x14ade03d2f3afc35ull, 0x8656d581c3c43bdbull,
                0xa34ebf198af13b0cull, 0x532b717c8c4c54e1ull, 0x4e511d3169515e72ull, 0xca07e2e205d946d9ull,
                0x5acc26d0b3921ce6ull, 0xd647f72bb6b2a8ddull, 0x9bf1aad818b0449full, 0x354e301e3126f06eull},
    .wrapped = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0},
    .primary_size = {0, 0, 0, 0, 0, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512, 512},
    .secondary_size = {512, 512, 512, 512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    .window = {237400466, -369128011, -121391697, 122321056, -77652223, -325660197, 100380100, 165779771, 144073171,
               -282210574, -214811799, 209220301, -36750750, -238779129, 6971038, 252642670}};

/// The rate rising to 1.5x, then 2.5x (a 204-sample secondary tape, swapped in after block 14), then below
/// half (a 4096-sample secondary tape, still growing at the end).
const Record kRateChange{
    .digests = {0x3699dd083fe6160bull, 0x0c7a52609b4a3c1cull, 0x7ac286bcd0601e3bull, 0x2f26d69d4a448dc2ull,
                0xb8d77da63e641723ull, 0x9628030da502db50ull, 0x4fb782fa3d27d2c7ull, 0xf60bde04a5ccf71dull,
                0xdee69ce5701ac1efull, 0x2c580e0c74db7821ull, 0xe2e13b57448f253cull, 0x61a72420f441c269ull,
                0xac3cf2cb19071b1full, 0xe6379158e1a40061ull, 0x11c0168595550187ull, 0x130922d8e5c9cc8full,
                0xe14636d87369d475ull, 0x89e5ab89e24492f6ull, 0x9487915ca0dc5703ull, 0x1460020f9ab77e4eull},
    .wrapped = {0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
    .primary_size = {0, 0, 0, 0, 0, 512, 512, 512, 512, 512, 512, 512, 512, 512, 204, 204, 204, 204, 204, 204},
    .secondary_size = {512, 512, 512, 512, 512, 0, 0, 0, 0, 0, 0, 0, 204, 204, 0, 0, 4096, 4096, 4096, 4096},
    .window = {237400466, -369128011, -121391697, 122321056, -77652223, -325660197, 100380100, 165779771, 144073171,
               -282210574, -214811799, 209220301, -36750750, -238779129, 6971038, 252642670}};

/// doubleSpeedSecondary, per block: the secondary tape's read-head steps (100 on the native 256-sample tape that starts
/// the delay, 200 on the 2048-sample tape grown at half the rate, 100 on the 1024-sample tape that follows its swap),
/// the samples left until the swap after the block, and the tapes' sizes after it.
const std::vector<int32_t> kWritten{100, 100, 100, 0,   200, 200, 200, 200, 200, 200,
                                    200, 200, 200, 200, 200, 100, 100, 100, 100, 100};
const std::vector<int32_t> kSizeLeft{156, 56,  -44, -44, 1853, 1653, 1453, 1253, 1053, 853,
                                     653, 453, 253, 53,  -147, 929,  829,  729,  629,  529};
const std::vector<int32_t> kPrimarySize{0,   0,   256, 256, 256,  256,  256,  256,  256,  256,
                                        256, 256, 256, 256, 2048, 2048, 2048, 2048, 2048, 2048};
const std::vector<int32_t> kSecondarySize{256,  256,  0,    0,    2048, 2048, 2048, 2048, 2048, 2048,
                                          2048, 2048, 2048, 2048, 0,    1024, 1024, 1024, 1024, 1024};

/// A tape and the heap storage it plays, as the delay's buffer allocates it: the ideal size for its rate plus
/// kWriteLag, holding the allocator's fill.
struct Buffer {
	std::vector<StereoSample> storage;
	DelayTape tape;

	/// @return Whether a buffer was made: not when the ideal size is @p fail_if_this_size.
	bool init(uint32_t rate, uint32_t fail_if_this_size = 0) {
		const auto [size, clamped] = DelayTape::ideal_size_for_rate(rate);
		if (static_cast<uint32_t>(size) == fail_if_this_size) {
			return false;
		}
		const size_t samples = static_cast<size_t>(size) + static_cast<size_t>(DelayTape::kWriteLag);
		storage.resize(samples);
		for (size_t i = 0; i < samples; ++i) {
			storage[i] = {.l = pattern(2 * i), .r = pattern((2 * i) + 1)};
		}
		tape = DelayTape{std::span{storage}, static_cast<size_t>(size), rate};
		if (clamped) {
			tape.make_native_rate_precise();
		}
		return true;
	}
};

/// The lifecycle Delay keeps around the processor, mirrored line for line.
class Owner {
public:
	/// @brief Activates the delay at @p rate: the first tape is a secondary, swapped in once written through.
	explicit Owner(int32_t rate) {
		std::ignore = secondary_.init(rate);
		size_left_ = static_cast<int32_t>(secondary_.tape.size());
		processor_.reset_high_pass();
	}

	DelayBlockResult block(std::span<StereoSample> buffer, const Settings& s) {
		DelayTape& primary = primary_.tape;
		DelayTape& secondary = secondary_.tape;

		if (s.rate != last_rate_) {
			last_rate_ = s.rate;
			count_ = 0;
		}
		else {
			count_ += buffer.size();
		}
		if (!secondary.is_active()) {
			if (primary.resampling() || s.rate != static_cast<int32_t>(primary.native_rate())) {
				if (count_ >= (44100 >> 3)) {
					initializeSecondary(s.rate, true);
				}
				else if (static_cast<uint32_t>(s.rate) >= (primary.native_rate() << 1)) {
					initializeSecondary(s.rate, false);
				}
				else if (static_cast<uint32_t>(s.rate) < primary.native_rate() >> 1) {
					initializeSecondary(s.rate >> 1, false);
				}
			}
		}

		DelayTape* const growing = secondary.is_active() ? &secondary : nullptr;
		const DelayBlockResult result = processor_.process(buffer, &primary, growing,
		                                                   {.rate = s.rate,
		                                                    .feedback = s.feedback,
		                                                    .analog = s.analog,
		                                                    .analog_saturation = s.saturation,
		                                                    .ping_pong = s.ping_pong,
		                                                    .render_in_stereo = s.stereo});
		if (growing != nullptr) {
			size_left_ -= result.secondary_written;
			if (size_left_ < 0) {
				primary_ = std::move(secondary_);
				secondary_ = {};
			}
		}
		return result;
	}

	[[nodiscard]] int32_t primarySize() const { return size(primary_.tape); }
	[[nodiscard]] int32_t secondarySize() const { return size(secondary_.tape); }
	[[nodiscard]] int32_t sizeLeft() const { return size_left_; }

private:
	static int32_t size(const DelayTape& tape) { return tape.is_active() ? static_cast<int32_t>(tape.size()) : 0; }

	void initializeSecondary(int32_t rate, bool relative) {
		if (!secondary_.init(rate, primary_.tape.size())) {
			return;
		}
		if (relative) {
			primary_.tape.make_native_rate_precise_relative_to(secondary_.tape);
		}
		else {
			primary_.tape.make_native_rate_precise();
			secondary_.tape.make_native_rate_precise();
		}
		size_left_ = static_cast<int32_t>(secondary_.tape.size()) + 5;
	}

	Buffer primary_;
	Buffer secondary_;
	DelayProcessor processor_;
	size_t count_ = 0;
	int32_t last_rate_ = 0;
	int32_t size_left_ = 0;
};

std::vector<int32_t> words(std::span<const StereoSample> block) {
	std::vector<int32_t> out;
	for (const StereoSample s : block) {
		out.push_back(s.l);
		out.push_back(s.r);
	}
	return out;
}

Record run(const Case& c) {
	Owner owner{c.block[0].rate};
	Record record;
	int32_t k = 0;
	for (size_t b = 0; b < kBlocks; ++b) {
		std::array<StereoSample, kBlockSize> buffer{};
		for (StereoSample& s : buffer) {
			s = input(k++);
		}
		const DelayBlockResult result = owner.block(buffer, c.block[b]);
		const std::vector<int32_t> out = words(buffer);
		record.digests.push_back(digest(out));
		record.wrapped.push_back(result.wrapped ? 1 : 0);
		record.primary_size.push_back(owner.primarySize());
		record.secondary_size.push_back(owner.secondarySize());
		if (b == kWindowBlock) {
			record.window.assign(out.begin(), out.begin() + 16);
		}
	}
	return record;
}

/// One block of a run on a primary tape alone: its settings, and whether the delay is switched off and on again, as
/// its owner does, before it.
struct Step {
	bool analog = true;
	int32_t saturation = 8;
	bool reactivate = false;
};

/// Each block's output digest over @p steps, played by one processor on a native-rate primary tape: a reactivation
/// calls reset_high_pass and plays on a new tape, as the owner does when a delay starts again.
std::vector<uint64_t> runSteps(const std::vector<Step>& steps) {
	DelayProcessor processor;
	processor.reset_high_pass();
	Buffer primary;
	std::ignore = primary.init(kNativeRate);
	std::vector<uint64_t> digests;
	int32_t k = 0;
	for (const Step& step : steps) {
		if (step.reactivate) {
			processor.reset_high_pass();
			primary = {};
			std::ignore = primary.init(kNativeRate);
		}
		std::array<StereoSample, kBlockSize> block{};
		for (StereoSample& s : block) {
			s = input(k++);
		}
		std::ignore = processor.process(block, &primary.tape, nullptr,
		                                {.rate = kNativeRate,
		                                 .feedback = 0x38000000,
		                                 .analog = step.analog,
		                                 .analog_saturation = step.saturation,
		                                 .ping_pong = false,
		                                 .render_in_stereo = true});
		digests.push_back(digest(words(block)));
	}
	return digests;
}

/// Eight analog blocks at a sound's drive, the delay switched off and on again before the fifth.
std::vector<Step> reactivation() {
	std::vector<Step> steps(8);
	steps[4].reactivate = true;
	return steps;
}

/// Nine blocks at a host chain's drive: three analog, three digital, three analog.
std::vector<Step> modeSwitch() {
	std::vector<Step> steps(9, Step{.saturation = 5});
	for (size_t b = 3; b < 6; ++b) {
		steps[b].analog = false;
	}
	return steps;
}

/// reactivation, per block.
const std::vector<uint64_t> kReactivation{0x8aa7220a7c697fabull, 0x5376b9bd707e1438ull, 0x512938d1fa2845fdull,
                                          0xc2d71cd2941a5d36ull, 0x22afd840ab053535ull, 0xf3062b8c58040d8eull,
                                          0x54c66d0892025357ull, 0x48016699cf038d20ull};

/// modeSwitch, per block.
const std::vector<uint64_t> kModeSwitch{0xa311cc4b9a7f8d1bull, 0x752a675ed800cd40ull, 0x1ca45bb0240210b9ull,
                                        0x3355483403b6f12cull, 0xd3f49f7106e57afbull, 0xe166c9a5f581d580ull,
                                        0x18362807d470e747ull, 0x6771a96b601fe5f4ull, 0x22cd52b8d0b30a1cull};

} // namespace

// clang-format off
describe delay_processor("DelayProcessor", ${
	it("reads silence and writes nothing without tapes, leaving the block as it was", _ {
		DelayProcessor processor;
		std::array<StereoSample, kBlockSize> block{};
		for (size_t i = 0; i < block.size(); ++i) {
			block[i] = input(static_cast<int32_t>(i));
		}
		const std::vector<int32_t> before = words(block);
		const DelayBlockResult result = processor.process(block, nullptr, nullptr, {.rate = kNativeRate,
		    .feedback = 0x30000000, .analog = false, .analog_saturation = 8, .ping_pong = false,
		    .render_in_stereo = true});
		expect(result.wrapped).to_be_false();
		expect(result.secondary_written).to_equal(0);
		expect(words(block)).to_equal(before);
	});

	it("counts a native secondary tape's steps, one per sample, and reports its wrap", _ {
		Buffer secondary;
		std::ignore = secondary.init(kNativeRate);
		DelayProcessor processor;
		const DelayBlock settings{.rate = kNativeRate, .feedback = 0x30000000, .analog = false,
		    .analog_saturation = 8, .ping_pong = false, .render_in_stereo = true};
		std::vector<int32_t> written;
		std::vector<int32_t> wrapped;
		for (int32_t b = 0; b < 6; ++b) {
			std::array<StereoSample, kBlockSize> block{};
			const DelayBlockResult result = processor.process(block, nullptr, &secondary.tape, settings);
			written.push_back(result.secondary_written);
			wrapped.push_back(result.wrapped ? 1 : 0);
		}
		expect(written).to_equal(std::vector<int32_t>{100, 100, 100, 100, 100, 100});
		// The head starts kWriteLag in, at the end of a 532-sample ring, so it wraps 512 steps on, in the sixth block.
		expect(wrapped).to_equal(std::vector<int32_t>{0, 0, 0, 0, 0, 1});
	});

	it("counts a resampled secondary tape's steps, two per sample at twice its native rate, and swaps it in once "
	   "they pass its size", _ {
		const Case c = doubleSpeedSecondary();
		Owner owner{c.block[0].rate};
		std::vector<int32_t> written;
		std::vector<int32_t> left;
		std::vector<int32_t> primary;
		std::vector<int32_t> secondary;
		int32_t k = 0;
		for (size_t b = 0; b < kBlocks; ++b) {
			std::array<StereoSample, kBlockSize> buffer{};
			for (StereoSample& s : buffer) {
				s = input(k++);
			}
			written.push_back(owner.block(buffer, c.block[b]).secondary_written);
			left.push_back(owner.sizeLeft());
			primary.push_back(owner.primarySize());
			secondary.push_back(owner.secondarySize());
		}
		expect(written).to_equal(kWritten);
		expect(left).to_equal(kSizeLeft);
		expect(primary).to_equal(kPrimarySize);
		expect(secondary).to_equal(kSecondarySize);
	});

	it("feeds back straight and clips digitally", _ { expect(run(digital())).to_equal(kDigital); });

	it("shapes analog feedback through the impulse response and tanh at a sound's drive", _ {
		expect(run(analog(8))).to_equal(kAnalog8);
	});

	it("shapes analog feedback through the impulse response and tanh at a host chain's drive", _ {
		expect(run(analog(5))).to_equal(kAnalog5);
	});

	it("crosses ping-pong between the channels when the engine renders in stereo", _ {
		expect(run(pingPong(true))).to_equal(kPingPongStereo);
	});

	it("feeds ping-pong back straight when the engine renders in mono", _ {
		expect(run(pingPong(false))).to_equal(kPingPongMono);
		expect(run(pingPong(false)).digests).to_equal(run(digital()).digests);
		expect(run(pingPong(true)).digests).not_().to_equal(run(digital()).digests);
	});

	it("keeps playing through a rate change: resampling, a secondary tape grown and swapped in, the wraps the "
	   "secondary's while it grows", _ {
		expect(run(rateChange())).to_equal(kRateChange);
	});

	it("starts again after a reactivation with the impulse response's history from before it, its high-pass reset", _ {
		expect(runSteps(reactivation())).to_equal(kReactivation);
	});

	it("resumes the impulse response's history in analog after digital blocks left it untouched", _ {
		expect(runSteps(modeSwitch())).to_equal(kModeSwitch);
	});

	it("renders every recorded workload as the frame-order reference does: outputs, results, both tapes byte for byte "
	   "and their heads", _ {
		std::vector<std::string> parted;
		for (const delay_workloads::Workload& workload : delay_workloads::all_workloads()) {
			const auto parting =
			    delay_workloads::first_parting<DelayProcessor, delay_reference::FrameOrderDelayProcessor>(workload);
			if (parting) {
				parted.push_back(std::string{workload.name} + " block " + std::to_string(parting->block) + ": "
				                 + parting->what);
			}
		}
		expect(parted).to_equal(std::vector<std::string>{});
		expect(delay_workloads::all_workloads().size()).to_equal(delay_workloads::kWorkloadCount);
	});

	it("renders every workload in the delay line's frame order as recorded: each block's output, every block's "
	   "result, tape sizes and heads, and both tapes at the end", _ {
		for (const delay_workloads::Workload& workload : delay_workloads::all_workloads()) {
			const delay_workloads::WorkloadRecord& recorded =
			    delay_workloads::kRecordedFrameOrder.at(static_cast<size_t>(workload.id));
			expect(recorded.outputs.size()).to_equal(workload.blocks);
			expect(delay_workloads::record<DelayProcessor>(workload)).to_equal(recorded);
			expect(delay_workloads::record<delay_reference::FrameOrderDelayProcessor>(workload)).to_equal(recorded);
		}
	});

	it("leaves the staged order as recorded, block for block", _ {
		for (const delay_workloads::Workload& workload : delay_workloads::all_workloads()) {
			const delay_workloads::WorkloadRecord& recorded =
			    delay_workloads::kRecorded.at(static_cast<size_t>(workload.id));
			expect(recorded.outputs.size()).to_equal(workload.blocks);
			expect(delay_workloads::record<delay_reference::ReferenceDelayProcessor>(workload)).to_equal(recorded);
		}
	});
});
// clang-format on

CPPSPEC_MAIN(delay_processor);
