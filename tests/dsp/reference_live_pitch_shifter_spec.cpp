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

// The live pitch shifter's oracle (reference_live_pitch_shifter.h) over every workload (pitch_shift_workloads.h):
// the legacy classes make the records, the oracle makes the same, and the workloads reach every path.
// PITCH_SHIFT_PRINT_RECORDS=1 prints the legacy classes' records, for pitch_shift_workload_records.h.

#include "pitch_shift_workload_records.h"
#include "pitch_shift_workloads.h"
#include "reference_live_pitch_shifter.h"

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
namespace ref = deluge::dsp::test::reference_pitch_shift;
using deluge::synth::InputChannel;

/// The host's input port as the legacy classes read it: the block's clock and ring, and the one shared input.
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

/// The legacy classes as a workload system: the shared live input, and its shifters.
class LegacySystem {
public:
	explicit LegacySystem(InputChannel channel) : port_(channel) {}
	void create(std::size_t slot, InputChannel channel, int32_t phase) {
		shifters_[slot] = std::make_unique<deluge::synth::LivePitchShifter>(channel, phase);
	}
	void destroy(std::size_t slot) { shifters_[slot].reset(); }
	[[nodiscard]] bool exists(std::size_t slot) const { return shifters_[slot] != nullptr; }
	[[nodiscard]] bool removable(std::size_t slot) const { return shifters_[slot]->may_be_removed_without_click(); }
	[[nodiscard]] int32_t render(std::size_t slot, deluge::dsp::Q<31>* out, int32_t n, const pw::BlockInput& b,
	                             deluge::dsp::fixed::Cong& noise) {
		port_.at(b);
		return shifters_[slot]->render(out, n, b.phase_increment, b.amplitude, b.amplitude_increment,
		                               b.interpolation_size, port_, noise);
	}

private:
	WorkloadPort port_;
	std::array<std::unique_ptr<deluge::synth::LivePitchShifter>, 2> shifters_{};
};

[[nodiscard]] const pw::WorkloadRecord& recorded(const pw::Workload& w) {
	return pw::kRecordedReference.at(static_cast<std::size_t>(w.id));
}

} // namespace

// clang-format off
describe reference_live_pitch_shifter("the live pitch shifter's oracle", ${
	it("lists the workloads in WorkloadId order", _ {
		expect(pw::all_workloads().size()).to_equal(pw::kWorkloadCount);
		for (std::size_t i = 0; i < pw::all_workloads().size(); ++i) {
			expect(static_cast<std::size_t>(pw::all_workloads()[i].id)).to_equal(i);
		}
	});

	it("the legacy classes render each workload as recorded", _ {
		const char* print = std::getenv("PITCH_SHIFT_PRINT_RECORDS");
		const bool printing = print != nullptr && std::strcmp(print, "1") == 0;
		for (const pw::Workload& w : pw::all_workloads()) {
			const pw::WorkloadRecord record = pw::record<LegacySystem>(w);
			if (printing) {
				std::cout << "    // " << w.name << "\n    " << record << ",\n";
			}
			else {
				expect(record).to_equal(recorded(w));
			}
		}
	});

	it("the oracle renders each workload as recorded", _ {
		for (const pw::Workload& w : pw::all_workloads()) {
			expect(pw::record<pw::ReferenceSystem>(w)).to_equal(recorded(w));
		}
	});

	it("the workloads reach every path", _ {
		ref::reset_counts();
		for (const pw::Workload& w : pw::all_workloads()) {
			std::ignore = pw::record<pw::ReferenceSystem>(w);
		}
		const ref::Counts& c = ref::counts;
		expect(c.hops_up).to_be_greater_than(std::size_t{0});
		expect(c.hops_down).to_be_greater_than(std::size_t{0});
		expect(c.cuts).to_be_greater_than(std::size_t{0});
		expect(c.shortenings).to_be_greater_than(std::size_t{0});
		expect(c.sign_flips).to_be_greater_than(std::size_t{0});
		expect(c.early_flips).to_be_greater_than(std::size_t{0});
		expect(c.sub_sample_offsets).to_be_greater_than(std::size_t{0});
		expect(c.direct_blocks).to_be_greater_than(std::size_t{0});
		expect(c.sinc_blocks).to_be_greater_than(std::size_t{0});
		expect(c.linear_blocks).to_be_greater_than(std::size_t{0});
	});
});
// clang-format on

CPPSPEC_MAIN(reference_live_pitch_shifter);
