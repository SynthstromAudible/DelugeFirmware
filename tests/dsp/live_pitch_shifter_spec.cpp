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

// The live pitch shifter (synth/sources/input/live/live_pitch_shifter.h) over every workload
// (pitch_shift_workloads.h): it must make its records (pitch_shift_component_records.h), which differ from the legacy
// classes' (pitch_shift_workload_records.h) only on the workloads its quirk fixes reach. PITCH_SHIFT_PRINT_RECORDS=1
// prints its records instead.

#include "alloc_seams.h"
#include "pitch_shift_component_records.h"
#include "pitch_shift_workload_records.h"
#include "pitch_shift_workloads.h"

#include "synth/sources/input/audio_input_port.h"
#include "synth/sources/input/live/live_input.h"
#include "synth/sources/input/live/live_inputs.h"
#include "synth/sources/input/live/live_pitch_shifter.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <tuple>
#include <vector>

namespace {

namespace pw = pitch_shift_workloads;
using deluge::synth::InputChannel;

/// The host's input port as the shifter reads it, the block's clock and ring; and the one history the shifters share,
/// as one oscillator's voices do.
class WorkloadPort final : public deluge::synth::AudioInputPort {
public:
	explicit WorkloadPort(InputChannel channel) {
		if (channel == InputChannel::STEREO) {
			stereo_ = std::make_unique<deluge::synth::StereoLiveInput>(channel);
		}
		else {
			mono_ = std::make_unique<deluge::synth::MonoLiveInput>(channel);
		}
	}
	void at(const pw::BlockInput& b) { block_ = b; }
	[[nodiscard]] uint32_t sample_time() const override { return block_.time; }
	[[nodiscard]] bool stereo_input_available() const override { return true; }
	[[nodiscard]] const int32_t* ring_read_pos() const override { return block_.read_pos; }
	[[nodiscard]] const int32_t* ring_end() const override { return block_.ring_end; }
	void report_hops(int32_t /*hops_ended*/) override {}
	/// @brief The history, as the oscillator's store hands it to its voices.
	[[nodiscard]] deluge::synth::LiveInputs inputs() const { return {.mono = mono_.get(), .stereo = stereo_.get()}; }

private:
	std::unique_ptr<deluge::synth::MonoLiveInput> mono_;
	std::unique_ptr<deluge::synth::StereoLiveInput> stereo_;
	pw::BlockInput block_{};
};

/// The pitch shifter as a workload system: the shared live input, and its shifters.
class ShifterSystem {
public:
	explicit ShifterSystem(InputChannel channel) : port_(channel) {}
	ShifterSystem(const ShifterSystem&) = delete;
	ShifterSystem& operator=(const ShifterSystem&) = delete;
	~ShifterSystem() {
		for (deluge::synth::PitchShifter& shifter : shifters_) {
			shifter.destroy();
		}
	}
	void create(std::size_t slot, InputChannel channel, int32_t phase) {
		if (!shifters_[slot].create(channel, phase)) {
			std::abort(); // The host heap has room for a shifter
		}
	}
	void destroy(std::size_t slot) { shifters_[slot].destroy(); }
	[[nodiscard]] bool exists(std::size_t slot) const { return shifters_[slot].exists(); }
	[[nodiscard]] bool removable(std::size_t slot) { return shifters_[slot].may_be_removed_without_click(); }
	[[nodiscard]] int32_t render(std::size_t slot, deluge::dsp::Q<31>* out, int32_t n, const pw::BlockInput& b,
	                             deluge::dsp::fixed::Cong& noise) {
		port_.at(b);
		return shifters_[slot].render(out, n, b.phase_increment, deluge::dsp::Q<31>::from_raw(b.amplitude),
		                              deluge::dsp::Q<31>::from_raw(b.amplitude_increment), b.interpolation_size, port_,
		                              port_.inputs(), noise);
	}

private:
	WorkloadPort port_;
	std::array<deluge::synth::PitchShifter, 2> shifters_{};
};

} // namespace

// clang-format off
describe live_pitch_shifter("deluge::synth::LivePitchShifter", ${
	it("renders each workload as recorded", _ {
		const char* print = std::getenv("PITCH_SHIFT_PRINT_RECORDS");
		const bool printing = print != nullptr && std::strcmp(print, "1") == 0;
		for (const pw::Workload& w : pw::all_workloads()) {
			const pw::WorkloadRecord record = pw::record<ShifterSystem>(w);
			if (printing) {
				std::cout << "    // " << w.name << "\n    " << record << ",\n";
			}
			else {
				expect(record).to_equal(pw::kShifterRecords.at(static_cast<std::size_t>(w.id)));
			}
		}
	});

	it("differs from the legacy classes only where a quirk's fix reaches", _ {
		// Q1 reaches a shifter made on a running input, Q4 an input restarted, Q14 every head that ran out within a block
		// and Q2 every pitched-up hop: what is left equal is the smallest shift up, whose hops never run out, and
		// pitching down at short blocks and over a silent start
		const std::set<pw::WorkloadId> untouched{pw::WorkloadId::UP_HALF_SINE_LEFT,
		                                         pw::WorkloadId::DOWN12_BLOCK7_PERC_LEFT,
		                                         pw::WorkloadId::SILENCE_DOWN12_LEFT};
		for (const pw::Workload& w : pw::all_workloads()) {
			const auto i = static_cast<std::size_t>(w.id);
			expect(pw::kShifterRecords.at(i) == pw::kRecordedReference.at(i)).to_equal(untouched.contains(w.id));
		}
	});

	it("plays the block it is made in, on an input already running", _ {
		// An input ingested for 20,000 frames, then a shifter made on it: until its first hop its head reads the block
		// directly, at the direct level (×8 of the rounded high word of the frame times the amplitude)
		pw::InputRing ring;
		ShifterSystem system(InputChannel::LEFT);
		const auto amplitude = deluge::dsp::Q<31>::from_raw(0x3000'0000);
		uint32_t time = 0;
		deluge::dsp::fixed::Cong noise(pw::kNoiseSeed);
		std::vector<deluge::dsp::Q<31>> out(32);
		const auto block = [&] {
			const int32_t* read_pos = ring.write(pw::Signal::PERCUSSIVE, time, 32);
			const pw::BlockInput input{.time = time, .read_pos = read_pos, .ring_end = ring.end(),
			                           .phase_increment = pw::kUp7, .amplitude = amplitude.raw(),
			                           .amplitude_increment = 0, .interpolation_size = kInterpolationMaxNumSamples};
			time += 32;
			return input;
		};
		// The first shifter runs the input for 20,000 frames; the second is made after them
		system.create(0, InputChannel::LEFT, pw::kUp7);
		for (int b = 0; b < 625; ++b) {
			std::ranges::fill(out, deluge::dsp::Q<31>{});
			std::ignore = system.render(0, out.data(), 32, block(), noise);
		}
		system.create(1, InputChannel::LEFT, pw::kUp7);
		const uint32_t first = time;
		std::ranges::fill(out, deluge::dsp::Q<31>{});
		const pw::BlockInput input = block();
		std::ignore = system.render(0, out.data(), 32, input, noise); // The first shifter ingests the block
		std::ranges::fill(out, deluge::dsp::Q<31>{});
		std::ignore = system.render(1, out.data(), 32, input, noise);
		bool plays_the_block = true;
		for (uint32_t i = 0; i < 32; ++i) {
			const auto frame = deluge::dsp::Q<31>::from_raw(pw::input_frame(pw::Signal::PERCUSSIVE, first + i)[0]);
			const auto want = scale_shl_wrap<3>(mul_r_as_wrap<deluge::dsp::Q<31>>(frame, amplitude));
			plays_the_block = plays_the_block && out[i].raw() == want.raw();
		}
		expect(plays_the_block).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(live_pitch_shifter);
