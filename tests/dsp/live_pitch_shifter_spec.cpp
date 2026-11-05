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
// (pitch_shift_workloads.h): it must make the records the legacy classes made (pitch_shift_workload_records.h).
// PITCH_SHIFT_PRINT_RECORDS=1 prints its records instead.

#include "alloc_seams.h"
#include "pitch_shift_workload_records.h"
#include "pitch_shift_workloads.h"

#include "synth/sources/input/audio_input_port.h"
#include "synth/sources/input/live/live_input.h"
#include "synth/sources/input/live/live_pitch_shifter.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

namespace {

namespace pw = pitch_shift_workloads;
using deluge::synth::InputChannel;

/// The host's input port as the shifter reads it: the block's clock and ring, and the one shared input.
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
	[[nodiscard]] deluge::synth::MonoLiveInput* mono_input(InputChannel /*channel*/, bool /*may_create*/) override {
		return mono_.get();
	}
	[[nodiscard]] deluge::synth::StereoLiveInput* stereo_input(bool /*may_create*/) override { return stereo_.get(); }
	void report_hops(int32_t /*hops_ended*/) override {}

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
		                              noise);
	}

private:
	WorkloadPort port_;
	std::array<deluge::synth::PitchShifter, 2> shifters_{};
};

} // namespace

// clang-format off
describe live_pitch_shifter("deluge::synth::LivePitchShifter", ${
	it("renders each workload as the legacy classes did", _ {
		const char* print = std::getenv("PITCH_SHIFT_PRINT_RECORDS");
		const bool printing = print != nullptr && std::strcmp(print, "1") == 0;
		for (const pw::Workload& w : pw::all_workloads()) {
			const pw::WorkloadRecord record = pw::record<ShifterSystem>(w);
			if (printing) {
				std::cout << "    // " << w.name << "\n    " << record << ",\n";
			}
			else {
				expect(record).to_equal(pw::kRecordedReference.at(static_cast<std::size_t>(w.id)));
			}
		}
	});
});
// clang-format on

CPPSPEC_MAIN(live_pitch_shifter);
