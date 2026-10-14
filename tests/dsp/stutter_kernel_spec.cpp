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

// The Stutter kernel (dsp-next/fixed/stutter/stutter_kernel.h) driven as its engine component drives it, against the
// stutter reference (reference_stutter.h) over every workload (stutter_workloads.h): block by block, and as recorded.

#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/stutter/stutter_kernel.h"
#include "reference_stutter.h"
#include "stutter_workload_records.h"
#include "stutter_workloads.h"

#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

namespace sw = stutter_workloads;
using deluge::dsp::Mono;
using deluge::dsp::StereoSample;
using deluge::dsp::fixed::DelayTape;
using deluge::dsp::fixed::Stutter;
using deluge::dsp::test::ReferenceStutter;

/// @brief Mono<Stutter> over an owned tape, driven as effects::Stutter drives it: configured each block with the
/// rate, the tape and whether it plays, rendered in place, and finished after a recording block whose frames ran out.
class KernelStutter {
public:
	void begin(uint32_t rate, const deluge::effects::StutterState& settings, StereoSample fill = {.l = 0, .r = 0}) {
		const auto [size, clamped] = DelayTape::idealSizeForRate(rate);
		storage_.assign(static_cast<std::size_t>(size) + static_cast<std::size_t>(DelayTape::kWriteLag), fill);
		tape_ = DelayTape{std::span<StereoSample>{storage_}, static_cast<std::size_t>(size), rate};
		if (clamped) {
			tape_.makeNativeRatePrecise();
		}
		mono_.reset();
		mono_.on<&Stutter::start>(static_cast<int32_t>(tape_.size()), settings.reversed, settings.ping_pong);
	}

	void process(std::span<StereoSample> audio, uint32_t rate) {
		mono_.configure({.rate = rate, .tape = &tape_, .playing = mono_.state().playing});
		mono_.renderBlock(audio, audio);
		if (!mono_.state().playing && mono_.state().frames_left < 0) {
			mono_.on<&Stutter::finish_block>(tape_);
		}
	}

	[[nodiscard]] bool playing() const { return mono_.state().playing; }
	[[nodiscard]] bool reversed() const { return mono_.state().reverse; }
	[[nodiscard]] int32_t frames_left() const { return mono_.state().frames_left; }
	[[nodiscard]] const DelayTape& tape() const { return tape_; }

private:
	std::vector<StereoSample> storage_;
	DelayTape tape_;
	Mono<Stutter> mono_;
};

/// @return Every block's output of @p workload through @p Fx, one vector per block.
template <typename Fx>
[[nodiscard]] std::vector<std::vector<int32_t>> blocks(const sw::Workload& workload) {
	Fx fx;
	std::vector<std::vector<int32_t>> out;
	sw::play(workload, fx, [&](std::size_t, std::span<const StereoSample> block) {
		std::vector<int32_t>& words = out.emplace_back();
		for (const StereoSample frame : block) {
			words.push_back(frame.l);
			words.push_back(frame.r);
		}
	});
	return out;
}

} // namespace

// clang-format off
describe stutter_kernel("the Stutter kernel", ${
	it("renders every block of every workload as the reference does", _ {
		for (const sw::Workload& workload : sw::all_workloads()) {
			const std::vector<std::vector<int32_t>> kernel = blocks<KernelStutter>(workload);
			const std::vector<std::vector<int32_t>> reference = blocks<ReferenceStutter>(workload);
			std::size_t first_differing = kernel.size();
			for (std::size_t b = 0; b < kernel.size(); ++b) {
				if (kernel[b] != reference[b]) {
					first_differing = b;
					break;
				}
			}
			expect(first_differing).to_equal(kernel.size());
		}
	});

	it("renders each workload as recorded", _ {
		for (const sw::Workload& workload : sw::all_workloads()) {
			expect(sw::record<KernelStutter>(workload)).to_equal(sw::kRecordedReference.at(static_cast<std::size_t>(workload.id)));
		}
	});

	it("passes the input through while it records", _ {
		const sw::Workload& workload = sw::all_workloads()[static_cast<std::size_t>(sw::WorkloadId::RECORD_NATIVE)];
		const std::vector<std::vector<int32_t>> out = blocks<KernelStutter>(workload);
		for (std::size_t b = 0; b < 16; ++b) {
			std::vector<int32_t> in;
			for (const StereoSample frame : sw::input(128, b, false, b * 128)) {
				in.push_back(frame.l);
				in.push_back(frame.r);
			}
			expect(out[b]).to_equal(in);
		}
	});
});
// clang-format on

CPPSPEC_MAIN(stutter_kernel);
