// The reverb reference (reference_reverb.h) over every workload (reverb_workloads.h): its records, and that the
// workloads reach every path.
#include "reference_reverb.h"
#include "reverb_workload_records.h"
#include "reverb_workloads.h"
#include <cppspec.hpp>
#include <tuple>

namespace {
namespace rw = reverb_workloads;
} // namespace

// clang-format off
describe reverb_reference("the reverb reference", ${
	it("renders each workload as recorded", _ {
		for (const rw::Workload& w : rw::all_workloads()) {
			expect(rw::record<deluge::dsp::test::ReferenceRunner>(w))
			    .to_equal(rw::kRecordedReference.at(static_cast<std::size_t>(w.id)));
		}
	});
	it("lists the workloads in WorkloadId order", _ {
		for (std::size_t i = 0; i < rw::all_workloads().size(); ++i) {
			expect(static_cast<std::size_t>(rw::all_workloads()[i].id)).to_equal(i);
		}
		expect(rw::all_workloads().size()).to_equal(rw::kWorkloadCount);
	});
	it("reaches every path", _ {
		deluge::dsp::test::counts = {};
		for (const rw::Workload& w : rw::all_workloads()) {
			std::ignore = rw::record<deluge::dsp::test::ReferenceRunner>(w);
		}
		const auto& c = deluge::dsp::test::counts;
		expect(c.freeverb_blocks).to_be_greater_than(std::size_t{0});
		expect(c.mutable_blocks).to_be_greater_than(std::size_t{0});
		expect(c.digital_blocks).to_be_greater_than(std::size_t{0});
		expect(c.freeverb_wet2_blocks).to_be_greater_than(std::size_t{0});
		expect(c.freeverb_comb_sum_wrapped).to_be_greater_than(std::size_t{0});
		expect(c.freeverb_output_wrapped).to_be_greater_than(std::size_t{0});
		expect(c.freeverb_dc_nonzero).to_be_greater_than(std::size_t{0});
		expect(c.lfo_steps).to_be_greater_than(std::size_t{1000});
		expect(c.ring_laps).to_be_greater_than(std::size_t{0});
		expect(c.saturated_high).to_be_greater_than(std::size_t{0});
		expect(c.saturated_low).to_be_greater_than(std::size_t{0});
		expect(c.plate_filtered_blocks).to_be_greater_than(std::size_t{0});
		expect(c.model_changes).to_be_greater_than(std::size_t{6});
	});
});
// clang-format on
CPPSPEC_MAIN(reverb_reference);
