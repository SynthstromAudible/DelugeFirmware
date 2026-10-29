// The reverb reference (reference_reverb.h) over every workload (reverb_workloads.h): its records, that it renders
// as today's app reverb does, and that the workloads reach every path.
#include "reference_reverb.h"
#include "reverb_workload_records.h"
#include "reverb_workloads.h"
#if !defined(DELUGE_DSP_UBSAN)
#include "dsp/reverb/reverb.hpp"
#endif
#include <cppspec.hpp>
#include <memory>
#include <tuple>

namespace {
namespace rw = reverb_workloads;
#if !defined(DELUGE_DSP_UBSAN)
/// @brief Today's app reverb behind the runner shape: the engine's call order (getReverbParamsFromSong, renderReverb).
struct LegacyRunner {
	std::unique_ptr<deluge::dsp::Reverb> reverb = std::make_unique<deluge::dsp::Reverb>();
	void set_model(rw::Model m) { reverb->setModel(static_cast<deluge::dsp::Reverb::Model>(m)); }
	void set_knobs(const rw::Knobs& k) {
		reverb->setRoomSize(k.room_size);
		reverb->setHPF(k.hpf);
		reverb->setLPF(k.lpf);
		reverb->setDamping(k.damping);
		reverb->setWidth(k.width);
	}
	void render(std::span<int32_t> send, std::span<StereoSample> out, int32_t l, int32_t r) {
		reverb->setPanLevels(l, r);
		reverb->process(send, out);
	}
	void digest_state(rw::Digest&) const {} // the legacy leg compares outputs and sends only
};
#endif
} // namespace

// clang-format off
describe reverb_reference("the reverb reference", ${
	it("renders each workload as recorded", _ {
		for (const rw::Workload& w : rw::all_workloads()) {
			expect(rw::record<deluge::dsp::test::ReferenceRunner>(w))
			    .to_equal(rw::kRecordedReference.at(static_cast<std::size_t>(w.id)));
		}
	});
#if !defined(DELUGE_DSP_UBSAN)
	it("renders and writes back the send exactly as today's app reverb", _ {
		for (const rw::Workload& w : rw::all_workloads()) {
			const rw::WorkloadRecord legacy = rw::record<LegacyRunner>(w);
			const rw::WorkloadRecord reference = rw::record<deluge::dsp::test::ReferenceRunner>(w);
			expect(legacy.outputs).to_equal(reference.outputs);
			expect(legacy.send).to_equal(reference.send);
		}
	});
#endif
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
