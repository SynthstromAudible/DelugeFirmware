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

// The compressor's references (reference_compressor.h) over every recorded workload (compressor_workloads.h).
//
// The verbatim reference is the record of the legacy compressor. A probe through its own detector shows which workloads
// reach a level defect: a sum of squares past 2^31, or a detector output of exactly INT32_MIN.
//
// The fixed reference against the verbatim one: the same wherever no defect is reached, parted wherever the sum wraps,
// its mean square never negative and its level never NaN, and INT32_MIN measured as 2^31.
//
// What the legacy compressor renders is pinned on the workloads up to MASTER_VOLUME_STEPS; what the fixed reference
// renders is pinned on every workload (compressor_workload_records.h), and the firmware's compressor must render the
// same.

#include "compressor_workload_records.h"
#include "compressor_workloads.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h"
#include "reference_compressor.h"

#include <algorithm>
#include <cmath>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

namespace fixed = deluge::dsp::fixed;
namespace cw = compressor_workloads;
using compressor_reference::FixedReferenceRmsFeedbackCompressor;
using compressor_reference::ReferenceRmsFeedbackCompressor;
using deluge::dsp::q31;
using deluge::dsp::StereoSample;

/// @return @p parting named with @p workload's name and the block.
[[nodiscard]] std::string named(std::string_view workload, const cw::Parting& parting) {
	return std::string{workload} + " block " + std::to_string(parting.block) + ": " + parting.what;
}

/// @return Each workload where compressor @p B parts from compressor @p A, named with the block and what parted.
/// Only the workloads @p keep(id) holds are played.
template <typename A, typename B, typename Keep>
[[nodiscard]] std::vector<std::string> partings(Keep keep) {
	std::vector<std::string> parted;
	for (const cw::CompressorWorkload& workload : cw::all_compressor_workloads()) {
		if (!keep(workload.id)) {
			continue;
		}
		const auto parting = cw::first_parting<A, B>(workload);
		if (parting) {
			parted.push_back(named(workload.name, *parting));
		}
	}
	return parted;
}

/// @return Whether neither level defect is reached in workload @p id.
[[nodiscard]] bool defect_free(cw::CompressorWorkloadId id) {
	return !cw::defects_fire(id);
}

/// @return Workload @p id.
[[nodiscard]] const cw::CompressorWorkload& workload_of(cw::CompressorWorkloadId id) {
	return cw::all_compressor_workloads().at(static_cast<size_t>(id));
}

/// @brief What the verbatim reference's level measurement met in one workload.
struct LevelProbe {
	size_t rendered = 0;       ///< Blocks rendered.
	size_t wrapped_blocks = 0; ///< Blocks whose sum of squares, added in 64 bits, passes INT32_MAX.
	size_t min_frames = 0;     ///< Frames whose high-passed left is INT32_MIN while the right is smaller in magnitude.
	size_t negative_means = 0; ///< Blocks after which the reference's mean square is negative.
};

/// @return |@p x| as an unsigned value: INT32_MIN gives 2^31.
[[nodiscard]] uint32_t magnitude(q31 x) {
	return x < 0 ? 0u - static_cast<uint32_t>(x) : static_cast<uint32_t>(x);
}

/// @return What the verbatim reference's level measurement meets over @p workload: its detector run again, from the
/// one-pole memories each block starts with and at the moveability it renders with, over each rendered block's output,
/// with each block's sum of squares added in 64 bits.
[[nodiscard]] LevelProbe probe_levels(const cw::CompressorWorkload& workload) {
	cw::CompressorRun<ReferenceRmsFeedbackCompressor> run{workload};
	LevelProbe probe;
	std::vector<StereoSample> block;
	for (size_t b = 0; b < workload.blocks; ++b) {
		auto detector = run.compressor().detector();
		std::ignore = run.block(b, block);
		if (!workload.step(b).render) {
			continue;
		}
		++probe.rendered;
		const q31 moveability = run.compressor().view().detector_moveability;
		uint64_t sum = 0;
		for (const StereoSample frame : block) {
			const q31 l = deluge::dsp::wrapping_sub(frame.l, detector[0].lowPass(frame.l, moveability));
			const q31 r = deluge::dsp::wrapping_sub(frame.r, detector[1].lowPass(frame.r, moveability));
			probe.min_frames += static_cast<size_t>(l == INT32_MIN && r != INT32_MIN);
			const uint64_t s = std::max(magnitude(l), magnitude(r));
			sum += (s * s) >> 32;
		}
		probe.wrapped_blocks += static_cast<size_t>(sum > INT32_MAX);
		probe.negative_means += static_cast<size_t>(run.compressor().view().mean < 0);
	}
	return probe;
}

/// @return How many blocks of @p workload leave the fixed reference's mean square negative or its level NaN.
[[nodiscard]] size_t bad_levels(const cw::CompressorWorkload& workload) {
	cw::CompressorRun<FixedReferenceRmsFeedbackCompressor> run{workload};
	std::vector<StereoSample> block;
	size_t bad = 0;
	for (size_t b = 0; b < workload.blocks; ++b) {
		std::ignore = run.block(b, block);
		const compressor_reference::View v = run.compressor().view();
		bad += static_cast<size_t>(v.mean < 0 || std::isnan(v.log_rms));
	}
	return bad;
}

} // namespace

// clang-format off
describe verbatim_compressor("the compressor's verbatim reference", ${
	it("holds every workload once, in id order", _ {
		const auto& workloads = cw::all_compressor_workloads();
		expect(workloads.size()).to_equal(cw::kCompressorWorkloadCount);
		for (size_t i = 0; i < workloads.size(); ++i) {
			expect(static_cast<size_t>(workloads[i].id)).to_equal(i);
		}
	});

	it("reaches a level defect in exactly the workloads defects_fire names, as a probe through its arithmetic shows", _ {
		for (const cw::CompressorWorkload& workload : cw::all_compressor_workloads()) {
			const LevelProbe probe = probe_levels(workload);
			expect(probe.wrapped_blocks > 0 || probe.min_frames > 0).to_equal(cw::defects_fire(workload.id));
		}
		for (const cw::CompressorWorkloadId id :
		     {cw::CompressorWorkloadId::MASTER_ONSET_PARTIAL_WET, cw::CompressorWorkloadId::SOUND_ONSET_PARTIAL_WET,
		      cw::CompressorWorkloadId::CLIP_ONSET_PARTIAL_WET, cw::CompressorWorkloadId::WRAPPED_SUM}) {
			expect(probe_levels(workload_of(id)).wrapped_blocks).to_be_greater_than(size_t{0});
		}
	});

	it("wraps its sum to a negative mean square in WRAPPED_SUM", _ {
		expect(probe_levels(workload_of(cw::CompressorWorkloadId::WRAPPED_SUM)).negative_means)
		    .to_be_greater_than(size_t{0});
	});

	it("high-passes one frame per block to a left of exactly INT32_MIN in INT32_MIN_DETECTOR, with no sum wrapping", _ {
		const LevelProbe probe = probe_levels(workload_of(cw::CompressorWorkloadId::INT32_MIN_DETECTOR));
		expect(probe.rendered).to_equal(size_t{40});
		expect(probe.min_frames).to_equal(probe.rendered);
		expect(probe.wrapped_blocks).to_equal(size_t{0});
	});

	it("renders the workloads up to MASTER_VOLUME_STEPS as recorded: each block's output and gain reduction, and the "
	   "state", _ {
		for (const cw::CompressorWorkload& workload : cw::all_compressor_workloads()) {
			if (static_cast<size_t>(workload.id) >= cw::kLegacyRecordedCount) {
				continue;
			}
			const cw::WorkloadRecord& recorded = cw::kCompressorRecorded.at(static_cast<size_t>(workload.id));
			expect(recorded.outputs.size()).to_equal(workload.blocks);
			expect(cw::record<ReferenceRmsFeedbackCompressor>(workload)).to_equal(recorded);
		}
	});
});

describe fixed_compressor("the compressor's fixed reference", ${
	it("renders as the verbatim one does wherever no level defect is reached: each block's output and gain "
	   "reduction, and the whole view", _ {
		expect(partings<ReferenceRmsFeedbackCompressor, FixedReferenceRmsFeedbackCompressor>(defect_free))
		    .to_equal(std::vector<std::string>{});
	});

	it("parts from the verbatim one wherever the legacy sum wraps", _ {
		for (const cw::CompressorWorkload& workload : cw::all_compressor_workloads()) {
			if (probe_levels(workload).wrapped_blocks > 0) {
				expect(cw::first_parting<ReferenceRmsFeedbackCompressor, FixedReferenceRmsFeedbackCompressor>(workload))
				    .to_have_value();
			}
		}
	});

	it("keeps its mean square non-negative and its level a number after every block of every workload", _ {
		for (const cw::CompressorWorkload& workload : cw::all_compressor_workloads()) {
			expect(bad_levels(workload)).to_equal(size_t{0});
		}
	});

	it("measures a detector output of INT32_MIN as 2^31", _ {
		// INT32_MIN_DETECTOR's first block is one frame, left INT32_MIN and right -2^30 coming out, through a
		// detector whose high-pass is 0 and whose memory is 0: its sum of squares is (2^31)^2 >> 32 = 2^30.
		const cw::CompressorWorkload& workload = workload_of(cw::CompressorWorkloadId::INT32_MIN_DETECTOR);
		cw::CompressorRun<FixedReferenceRmsFeedbackCompressor> run{workload};
		std::vector<StereoSample> block;
		std::ignore = run.block(0, block);
		expect(block.size()).to_equal(size_t{1});
		expect(block[0].l).to_equal(INT32_MIN);
		expect(block[0].r).to_equal(-(1 << 30));
		const compressor_reference::View v = run.compressor().view();
		expect(v.detector_moveability).to_equal(0);
		expect(v.detector_l.z).to_equal(0);
		constexpr float kNs = 2;
		const float mean = (static_cast<float>(int64_t{1} << 30) / ONE_Q31f) / kNs;
		expect(v.mean).to_equal((mean * kNs + 0) / (1 + kNs));
	});

	it("renders every workload as recorded, as the firmware's compressor does: each block's output and gain reduction, "
	   "and the state", _ {
		for (const cw::CompressorWorkload& workload : cw::all_compressor_workloads()) {
			const cw::WorkloadRecord& recorded = cw::kFixedCompressorRecorded.at(static_cast<size_t>(workload.id));
			expect(recorded.outputs.size()).to_equal(workload.blocks);
			expect(cw::record<FixedReferenceRmsFeedbackCompressor>(workload)).to_equal(recorded);
			expect(cw::record<fixed::RmsFeedbackCompressor>(workload)).to_equal(recorded);
		}
	});
});
// clang-format on

CPPSPEC_MAIN(verbatim_compressor, fixed_compressor);
