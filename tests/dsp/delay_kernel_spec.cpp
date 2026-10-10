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

// The Delay kernel: one frame kernel under Mono that reads the tape, runs the feedback, mixes and writes the tape,
// frame by frame. Rendered in place, it must equal the frame-order reference (the delay line's order,
// reference_delay_processor.h) bit for bit on every recorded workload (delay_workloads.h): each block's output, the
// block's wrap and secondary steps read from the kernel's State, both tapes byte for byte, their heads, and the
// feedback chain's memories. Between blocks it switches modes as the settings do, keeps the impulse response's history
// through a reactivation, crosses ping-pong only when the engine renders in stereo, and plays a new delay's first
// blocks as the dry signal while the secondary fills.

#include "delay_workloads.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/delay/delay_kernel.h"
#include "dsp-next/fixed/delay/delay_tape.h"
#include "dsp-next/fixed/delay/feedback_shaper.h"
#include "reference_delay_processor.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>

using deluge::dsp::Mono;
using deluge::dsp::StereoSample;
using deluge::dsp::fixed::Delay;
using deluge::dsp::fixed::DelayBlock;
using deluge::dsp::fixed::DelayBlockResult;
using deluge::dsp::fixed::DelayTape;
using deluge::dsp::fixed::FeedbackShaper;

static_assert(deluge::dsp::detail::plain_value<Delay::Settings>);
static_assert(deluge::dsp::detail::plain_value<Delay::Coefficients>);
static_assert(deluge::dsp::detail::plain_value<Delay::State>);
static_assert(deluge::dsp::detail::plain_value<Delay::Block>);
static_assert(deluge::dsp::detail::state_in_place<Delay>);
static_assert(deluge::dsp::processor_kernel<Delay>);
static_assert(deluge::dsp::moded_kernel<Delay>);

namespace {

/// The frame-order reference: the delay line's order.
using FrameOrder = delay_reference::FrameOrderDelayProcessor;

/// The block's result, read from the kernel's State and cleared there, as the owner reads it.
DelayBlockResult takeResult(Delay::State& s) {
	const DelayBlockResult result{.wrapped = s.wrapped, .secondary_written = s.secondary_written};
	s.wrapped = false;
	s.secondary_written = 0;
	return result;
}

/// `Mono<Delay>` with `DelayProcessor`'s `process` and `resetHighPass`, so the workloads drive it as the owner will:
/// configured from the block's settings and tapes, rendered in place, its result read from the State and cleared.
class KernelDelay {
public:
	DelayBlockResult process(std::span<StereoSample> block, DelayTape* primary, DelayTape* secondary,
	                         const DelayBlock& settings, std::span<StereoSample> /*scratch*/) {
		delay_.configure({.rate = static_cast<uint32_t>(settings.rate),
		                  .feedback = settings.feedback,
		                  .analog = settings.analog,
		                  .analog_saturation = settings.analog_saturation,
		                  .ping_pong = settings.ping_pong,
		                  .render_in_stereo = settings.render_in_stereo,
		                  .primary = primary,
		                  .secondary = secondary});
		modes_.push_back(delay_.coefficients().mode);
		delay_.renderBlock(block, block);
		return delay_.on<&takeResult>();
	}

	void resetHighPass() { delay_.on<&Delay::reset_high_pass>(); }

	[[nodiscard]] const Mono<Delay>& mono() const { return delay_; }
	/// @return The mode each block rendered at, in order.
	[[nodiscard]] const std::vector<Delay::Mode>& modes() const { return modes_; }

private:
	Mono<Delay> delay_;
	std::vector<Delay::Mode> modes_;
};

/// @return One channel's feedback memories: its impulse response history and oldest index, then its high-pass memory.
std::vector<int64_t> words(const Delay::FeedbackChain::State& s) {
	const auto& response = deluge::dsp::get<0>(s).response;
	std::vector<int64_t> out(std::begin(response.history), std::end(response.history));
	out.push_back(response.oldest);
	out.push_back(deluge::dsp::get<1>(s).y);
	return out;
}

/// @return The kernel's feedback memories, left then right.
std::vector<int64_t> words(const KernelDelay& kernel) {
	std::vector<int64_t> out = words(kernel.mono().state().left);
	const std::vector<int64_t> right = words(kernel.mono().state().right);
	out.insert(out.end(), right.begin(), right.end());
	return out;
}

/// @return The frame-order reference's feedback memories, left then right.
std::vector<int64_t> words(const FrameOrder& reference) {
	std::vector<int64_t> out = words(reference.feedback().left());
	const std::vector<int64_t> right = words(reference.feedback().right());
	out.insert(out.end(), right.begin(), right.end());
	return out;
}

/// @return "<name> block <b>: <what>" for each block where @p workload parts between the kernel and the frame-order
/// reference: the output, the result, the lifecycle, a tape or a head, or (checked before each block) the feedback
/// memories. @p before runs on both before each block, after the memories are compared.
std::vector<std::string> partings(const delay_workloads::Workload& workload,
                                  const std::function<void(size_t, KernelDelay&, FrameOrder&)>& before = {}) {
	std::vector<std::string> out;
	const auto parting = delay_workloads::first_parting<KernelDelay, FrameOrder>(
	    workload, [&](size_t block, KernelDelay& kernel, FrameOrder& reference) {
		    if (words(kernel) != words(reference)) {
			    out.push_back(std::string{workload.name} + " block " + std::to_string(block) + ": feedback memories");
		    }
		    if (before) {
			    before(block, kernel, reference);
		    }
	    });
	if (parting) {
		out.push_back(std::string{workload.name} + " block " + std::to_string(parting->block) + ": " + parting->what);
	}
	return out;
}

/// The rate of a 2048-sample tape, native; and of a 1024-sample one.
inline constexpr uint32_t kRate2048 = 1u << 27;
inline constexpr uint32_t kRate1024 = 1u << 28;

/// A 2048-sample tape whose settings change every block: the shaper every block, the mix every two. Native for 8
/// blocks, resampling at 1.5x for 8, then at 2x while a 1024-sample secondary grows natively, which swaps in to play
/// native again. Every one of the eight modes is rendered, and every switch between the shapers, the mixes and the tape
/// forms.
delay_workloads::Workload modeSwitches() {
	return {.id = delay_workloads::WorkloadId::NATIVE_STRAIGHT_DIGITAL,
	        .name = "mode_switches",
	        .tapes = {.start = delay_workloads::TapeSetup::Start::PRIMARY, .rate = kRate2048},
	        .blocks = 40,
	        .step = [](size_t b) {
		        const uint32_t rate = b < 8 ? kRate2048 : b < 16 ? kRate2048 / 2 * 3 : kRate1024;
		        return delay_workloads::BlockStep{.frames = delay_workloads::kMaxFrames,
		                                          .rate = rate,
		                                          .settings = {.rate = static_cast<int32_t>(rate),
		                                                       .feedback = 0x38000000,
		                                                       .analog = b % 2 == 1,
		                                                       .analog_saturation = 5,
		                                                       .ping_pong = (b / 2) % 2 == 1,
		                                                       .render_in_stereo = true},
		                                          .input = b < 34};
	        }};
}

/// A 2048-sample tape at 1.5x for 24 blocks, analog, ping-pong set to @p ping_pong and the engine rendering in stereo
/// as @p stereo says.
delay_workloads::Workload pingPong(bool ping_pong, bool stereo) {
	return {.id = delay_workloads::WorkloadId::NATIVE_STRAIGHT_DIGITAL,
	        .name = "ping_pong",
	        .tapes = {.start = delay_workloads::TapeSetup::Start::PRIMARY, .rate = kRate2048},
	        .blocks = 24,
	        .step = [=](size_t b) {
		        constexpr uint32_t kRate = kRate2048 / 2 * 3;
		        return delay_workloads::BlockStep{.frames = delay_workloads::kMaxFrames,
		                                          .rate = kRate,
		                                          .settings = {.rate = static_cast<int32_t>(kRate),
		                                                       .feedback = 0x38000000,
		                                                       .analog = true,
		                                                       .analog_saturation = 8,
		                                                       .ping_pong = ping_pong,
		                                                       .render_in_stereo = stereo},
		                                          .input = b < 16};
	        }};
}

/// @return Every block's output digest of @p workload through the kernel.
std::vector<uint64_t> outputs(const delay_workloads::Workload& workload) {
	return delay_workloads::record<KernelDelay>(workload).outputs;
}

/// The delay's settings, without tapes.
Delay::Settings settings(bool analog, bool ping_pong, bool stereo) {
	return {.rate = kRate2048,
	        .feedback = 0x38000000,
	        .analog = analog,
	        .analog_saturation = 8,
	        .ping_pong = ping_pong,
	        .render_in_stereo = stereo,
	        .primary = nullptr,
	        .secondary = nullptr};
}

} // namespace

// clang-format off
describe delay_kernel("Delay kernel", ${
	it("renders every recorded workload in place as the frame-order reference does: outputs, results from its State, "
	   "both tapes byte for byte, their heads and the feedback memories", _ {
		std::vector<std::string> parted;
		for (const delay_workloads::Workload& workload : delay_workloads::all_workloads()) {
			const std::vector<std::string> p = partings(workload);
			parted.insert(parted.end(), p.begin(), p.end());
		}
		expect(parted).to_equal(std::vector<std::string>{});
	});

	it("renders every workload as the frame order's record, block for block", _ {
		for (const delay_workloads::Workload& workload : delay_workloads::all_workloads()) {
			expect(delay_workloads::record<KernelDelay>(workload))
			    .to_equal(delay_workloads::kRecordedFrameOrder.at(static_cast<size_t>(workload.id)));
		}
	});

	it("switches shaper, mix and tape form between blocks as the reference does, rendering all eight modes", _ {
		const delay_workloads::Workload workload = modeSwitches();
		expect(partings(workload)).to_equal(std::vector<std::string>{});

		delay_workloads::WorkloadRun<KernelDelay> run{workload};
		std::array<StereoSample, delay_workloads::kMaxFrames> block{};
		for (size_t b = 0; b < workload.blocks; ++b) {
			std::ignore = run.block(b, block);
		}
		std::set<std::tuple<Delay::Shaper, Delay::Mix, Delay::Tape>> seen;
		size_t switches = 0;
		const std::vector<Delay::Mode>& modes = run.processor().modes();
		for (size_t b = 0; b < modes.size(); ++b) {
			seen.insert({modes[b].shaper, modes[b].mix, modes[b].tape});
			if (b > 0 && modes[b].tape != modes[b - 1].tape) {
				++switches;
			}
		}
		expect(seen.size()).to_equal(size_t{8});
		// Native to resampled at block 8, and back once the native secondary swaps in.
		expect(switches).to_equal(size_t{2});
	});

	it("steps the feedback chain at the mode its own coefficients assemble, the shaper's", _ {
		for (const bool analog : {false, true}) {
			const Delay::Coefficients c = Delay::coefficients(settings(analog, false, true));
			expect(c.mode.shaper).to_equal(analog ? Delay::Shaper::ANALOG : Delay::Shaper::DIGITAL);
			expect(deluge::dsp::get<0>(c.feedback.mode))
			    .to_equal(analog ? FeedbackShaper::Mode::ANALOG : FeedbackShaper::Mode::DIGITAL);
			expect(deluge::dsp::get<0>(c.feedback.mode))
			    .to_equal(deluge::dsp::get<0>(Delay::feedback_mode(c.mode.shaper)));
		}
	});

	it("keeps the impulse response's histories through a reactivation, its high-pass reset, as the reference does",
	   _ {
		// An analog workload, reactivated at blocks 10 and 25 on both.
		const delay_workloads::Workload workload =
		    delay_workloads::delay_workload(delay_workloads::WorkloadId::NATIVE_STRAIGHT_ANALOG);
		// Per reactivation and channel: the history words and oldest index before and after, then the high-pass memory
		// before and after.
		std::vector<std::vector<int64_t>> kept;
		std::vector<int64_t> high_pass;
		const std::vector<std::string> parted =
		    partings(workload, [&](size_t block, KernelDelay& kernel, FrameOrder& reference) {
			    if (block != 10 && block != 25) {
				    return;
			    }
			    const std::vector<int64_t> before = words(kernel);
			    kernel.resetHighPass();
			    reference.resetHighPass();
			    const std::vector<int64_t> after = words(kernel);
			    // Each channel: 50 history words, the oldest index, then the high-pass memory.
			    constexpr std::ptrdiff_t kChannel = 52;
			    for (std::ptrdiff_t first = 0; first < 2 * kChannel; first += kChannel) {
				    kept.emplace_back(before.begin() + first, before.begin() + first + kChannel - 1);
				    kept.emplace_back(after.begin() + first, after.begin() + first + kChannel - 1);
				    high_pass.push_back(before.at(static_cast<size_t>(first + kChannel - 1)));
				    high_pass.push_back(after.at(static_cast<size_t>(first + kChannel - 1)));
			    }
		    });
		expect(parted).to_equal(std::vector<std::string>{});
		expect(kept.size()).to_equal(size_t{8});
		expect(high_pass.size()).to_equal(size_t{8});
		for (size_t i = 0; i < kept.size(); i += 2) {
			expect(kept.at(i + 1)).to_equal(kept.at(i));
			expect(high_pass.at(i)).not_().to_equal(int64_t{0});
			expect(high_pass.at(i + 1)).to_equal(int64_t{0});
		}
	});

	it("crosses ping-pong only when the engine renders in stereo, in all four combinations of the two flags", _ {
		for (const bool ping_pong : {false, true}) {
			for (const bool stereo : {false, true}) {
				const Delay::Mix mix = Delay::coefficients(settings(false, ping_pong, stereo)).mode.mix;
				expect(mix).to_equal(ping_pong && stereo ? Delay::Mix::CROSS : Delay::Mix::STRAIGHT);
				expect(partings(pingPong(ping_pong, stereo))).to_equal(std::vector<std::string>{});
			}
		}
		const std::vector<uint64_t> straight = outputs(pingPong(false, true));
		expect(outputs(pingPong(false, false))).to_equal(straight);
		expect(outputs(pingPong(true, false))).to_equal(straight);
		expect(outputs(pingPong(true, true))).not_().to_equal(straight);
	});

	it("plays a new delay's first blocks as the dry signal, reading silence while the secondary fills", _ {
		delay_workloads::Buffer secondary;
		expect(secondary.allocate(kRate2048, 0)).to_be_true();
		const std::vector<uint8_t> fresh = delay_workloads::tape_bytes(secondary.tape);
		DelayTape inactive;
		Delay::Settings s = settings(true, true, true);
		s.primary = &inactive;
		s.secondary = &secondary.tape;
		Mono<Delay> delay{s};
		delay.on<&Delay::reset_high_pass>();
		expect(delay.coefficients().reading).to_be_false();
		expect(delay.coefficients().growing).to_be_true();

		uint32_t k = 0;
		for (size_t b = 0; b < 3; ++b) {
			std::array<StereoSample, delay_workloads::kMaxFrames> block{};
			for (StereoSample& sample : block) {
				sample = delay_workloads::input(k++);
			}
			const std::array<StereoSample, delay_workloads::kMaxFrames> dry = block;
			delay.renderBlock(block, block);
			expect(delay_workloads::same_frames(block, dry)).to_be_true();
			const DelayBlockResult result = delay.on<&takeResult>();
			expect(result.secondary_written).to_equal(static_cast<int32_t>(delay_workloads::kMaxFrames));
			expect(result.wrapped).to_be_false();
		}
		expect(delay_workloads::tape_bytes(secondary.tape)).not_().to_equal(fresh);
		expect(delay_workloads::tape_bytes(inactive)).to_equal(std::vector<uint8_t>{});
		expect(partings(delay_workloads::delay_workload(delay_workloads::WorkloadId::FIRST_BLOCKS)))
		    .to_equal(std::vector<std::string>{});
	});
});
// clang-format on

CPPSPEC_MAIN(delay_kernel);
