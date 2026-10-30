// The Freeverb kernel (dsp-next/fixed/reverb/freeverb.h) against the reverb reference over every Freeverb workload.
#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/reverb/freeverb.h"
#include "reference_reverb.h"
#include "reverb_kernel_views.h"
#include "reverb_workload_records.h"
#include "reverb_workloads.h"
#include <cppspec.hpp>

#include "util/fixedpoint.h"
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <vector>

namespace {
namespace rw = reverb_workloads;
using deluge::dsp::StereoSample;
using deluge::dsp::fixed::Freeverb;

/// @brief Mono<Freeverb> over memory it owns, as effects::Reverb holds it: a model change zeroes the memory and resets
/// the State.
struct KernelRunner {
	/// Model changes asked for that are not Freeverb, across every runner; the spec asserts it stays 0.
	static inline std::size_t foreign_models = 0;
	std::vector<int32_t> memory = std::vector<int32_t>(Freeverb::kMemoryWords);
	deluge::dsp::Mono<Freeverb> mono;
	KernelRunner() { mono.on<&Freeverb::set_memory>(memory.data()); }
	void set_model(rw::Model m) {
		if (m != rw::Model::FREEVERB) {
			++foreign_models;
		}
		std::ranges::fill(memory, 0);
		mono.reset();
		mono.on<&Freeverb::set_memory>(memory.data());
	}
	void set_knobs(const rw::Knobs& k) {
		mono.configure({.room_size = k.room_size, .damping = k.damping, .width = k.width});
	}
	void render(std::span<int32_t> send, std::span<StereoSample> out, int32_t l, int32_t r) {
		mono.on<&Freeverb::set_send>(send.data(), l, r);
		mono.renderBlock(out, out);
	}
	/// @brief The same block rendered from @p in into a separate @p out.
	void render_out_of_place(std::span<int32_t> send, std::span<const StereoSample> in, std::span<StereoSample> out,
	                         int32_t l, int32_t r) {
		mono.on<&Freeverb::set_send>(send.data(), l, r);
		mono.renderBlock(in, out);
	}
	void digest_state(rw::Digest& d) const {
		rw::digest_freeverb(d, deluge::dsp::test::freeverb_view(mono.state(), memory));
	}
};
[[nodiscard]] bool is_freeverb(const rw::Workload& w) {
	return w.model == rw::Model::FREEVERB;
}
/// @brief A small LCG, for the in-place case's blocks.
struct Lcg {
	uint32_t seed = 1;
	int32_t next() {
		seed = (seed * 1664525U) + 1013904223U;
		return static_cast<int32_t>(seed) >> 2;
	}
};

} // namespace

// clang-format off
describe freeverb_kernel("the Freeverb kernel", ${
	it("renders each Freeverb workload's output and state as the reference recorded", _ {
		KernelRunner::foreign_models = 0;
		for (const rw::Workload& w : rw::all_workloads() | std::views::filter(is_freeverb)) {
			const rw::WorkloadRecord got = rw::record<KernelRunner>(w);
			const rw::WorkloadRecord& want = rw::kRecordedReference.at(static_cast<std::size_t>(w.id));
			expect(got.outputs).to_equal(want.outputs);
			expect(got.state).to_equal(want.state);
			expect(got.send).to_equal(rw::record_send_untouched(w)); // the kernel leaves the send as it came
		}
		expect(KernelRunner::foreign_models).to_equal(std::size_t{0});
	});
	it("computes the reference's coefficients at every knob step of 1/50, width 0 included", _ {
		// Static storage, as the app's reverb has: its constructor reads `damp` before setting it, so it must
		// start zeroed.
		static deluge::dsp::test::reverb_ref::ReferenceFreeverb reference_storage;
		auto* const reference = &reference_storage;
		for (int32_t k = 0; k <= 50; ++k) {
			const float knob = static_cast<float>(k) / 50.f;
			reference->setRoomSize(knob);
			reference->setDamping(knob);
			reference->setWidth(knob);
			const Freeverb::Coefficients c =
			    Freeverb::coefficients({.room_size = knob, .damping = knob, .width = knob});
			expect(c.feedback).to_equal(reference->comb_feedback());
			expect(c.damp1).to_equal(reference->comb_damp1());
			expect(c.damp2).to_equal(reference->comb_damp2());
			expect(c.wet2).to_equal(reference->wet2_coefficient());
		}
	});
	it("takes out_r's cross-mix from the out_l it has just updated", _ {
		// Zeroed lines but each comb's first slot: every comb outputs its slot, the allpasses (empty) pass the
		// sum through unchanged (two sign flips twice), and a silent send adds nothing.
		constexpr int32_t kLeftTail = 1 << 24;
		constexpr int32_t kRightTail = 3 << 22;
		constexpr int32_t kLevel = std::numeric_limits<int32_t>::max();
		KernelRunner runner;
		runner.set_model(rw::Model::FREEVERB);
		const Freeverb::Settings settings{.room_size = 0.5f, .damping = 0.5f, .width = 0.5f};
		runner.mono.configure(settings);
		for (std::size_t i = 0; i < Freeverb::kCombs; ++i) {
			runner.memory[Freeverb::kLayout.comb[0][i].base] = kLeftTail;
			runner.memory[Freeverb::kLayout.comb[1][i].base] = kRightTail;
		}
		std::vector<int32_t> send(1, 0);
		std::vector<StereoSample> frame(1, StereoSample{.l = 0, .r = 0});
		runner.render(send, frame, kLevel, kLevel);

		const int32_t wet2 = Freeverb::coefficients(settings).wet2;
		const int32_t left = 8 * kLeftTail;
		const int32_t right = 8 * kRightTail;
		const int32_t left_mixed = (left + multiply_32x32_rshift32_rounded(right, wet2)) << 1;
		const int32_t right_from_updated = (right + multiply_32x32_rshift32_rounded(left_mixed, wet2)) << 1;
		const int32_t right_from_stale = (right + multiply_32x32_rshift32_rounded(left, wet2)) << 1;
		expect(right_from_updated != right_from_stale).to_be_true(); // the two orders are told apart
		expect(frame[0].l).to_equal(multiply_32x32_rshift32_rounded(left_mixed, kLevel));
		expect(frame[0].r).to_equal(multiply_32x32_rshift32_rounded(right_from_updated, kLevel));
	});
	it("renders in place as out of place", _ {
		KernelRunner in_place_runner;
		KernelRunner out_of_place_runner;
		for (KernelRunner* r : {&in_place_runner, &out_of_place_runner}) {
			r->set_model(rw::Model::FREEVERB);
			r->set_knobs(rw::kTurned);
		}
		Lcg lcg;
		std::vector<int32_t> send_a(128);
		std::vector<int32_t> send_b(128);
		std::vector<StereoSample> dry(128);
		std::vector<StereoSample> in_place(128);
		std::vector<StereoSample> out(128);
		std::size_t mismatches = 0;
		for (int32_t block = 0; block < 60; ++block) {
			for (std::size_t i = 0; i < dry.size(); ++i) {
				send_a[i] = send_b[i] = lcg.next();
				dry[i] = {.l = lcg.next(), .r = lcg.next()};
			}
			in_place = dry;
			in_place_runner.render(send_a, in_place, rw::kCentred, rw::kCentred);
			out_of_place_runner.render_out_of_place(send_b, dry, out, rw::kCentred, rw::kCentred);
			for (std::size_t i = 0; i < out.size(); ++i) {
				mismatches += (in_place[i].l != out[i].l || in_place[i].r != out[i].r) ? 1 : 0;
			}
		}
		expect(mismatches).to_equal(std::size_t{0});
	});
	it("lays its lines end to end in kMemoryWords at the reference's tunings", _ {
		namespace ref = deluge::dsp::test::reverb_ref;
		// The reference's tunings (Jezar's tuning.h), left then right for each line.
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): a table read by index
		constexpr int32_t kCombs[2][Freeverb::kCombs]{
		    {ref::combtuningL1, ref::combtuningL2, ref::combtuningL3, ref::combtuningL4, ref::combtuningL5,
		     ref::combtuningL6, ref::combtuningL7, ref::combtuningL8},
		    {ref::combtuningR1, ref::combtuningR2, ref::combtuningR3, ref::combtuningR4, ref::combtuningR5,
		     ref::combtuningR6, ref::combtuningR7, ref::combtuningR8}};
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): as kCombs
		constexpr int32_t kAllpasses[2][Freeverb::kAllpasses]{
		    {ref::allpasstuningL1, ref::allpasstuningL2, ref::allpasstuningL3, ref::allpasstuningL4},
		    {ref::allpasstuningR1, ref::allpasstuningR2, ref::allpasstuningR3, ref::allpasstuningR4}};
		const Freeverb::Layout& l = Freeverb::kLayout;
		uint32_t base = 0;
		for (std::size_t ch = 0; ch < 2; ++ch) {
			for (std::size_t i = 0; i < Freeverb::kCombs; ++i) {
				expect(l.comb[ch][i].base).to_equal(base);
				expect(static_cast<int32_t>(l.comb[ch][i].length)).to_equal(kCombs[ch][i]);
				base += l.comb[ch][i].length;
			}
		}
		for (std::size_t ch = 0; ch < 2; ++ch) {
			for (std::size_t i = 0; i < Freeverb::kAllpasses; ++i) {
				expect(l.allpass[ch][i].base).to_equal(base);
				expect(static_cast<int32_t>(l.allpass[ch][i].length)).to_equal(kAllpasses[ch][i]);
				base += l.allpass[ch][i].length;
			}
		}
		expect(static_cast<std::size_t>(base)).to_equal(Freeverb::kMemoryWords);
		expect(Freeverb::kMemoryWords).to_equal(std::size_t{25450});
	});
});
// clang-format on
CPPSPEC_MAIN(freeverb_kernel);
