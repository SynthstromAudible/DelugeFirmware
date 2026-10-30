#include "dsp-next/core/kernel.h"
#include "dsp-next/core/lane_pack.h"
#include "dsp-next/core/lanes.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/fixed/noise/cong.h"

#include <cppspec.hpp>
#include <span>
#include <vector>

using namespace deluge::dsp;
using deluge::dsp::fixed::Cong;

namespace {
/// Adds the next draw of its context to each sample: the order of draws is visible in the output.
struct AddNoise {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	using Context = Cong;
	static constexpr Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[nodiscard, gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                                  q31 x, Context& noise) {
		return x + static_cast<q31>(noise.next() >> 8);
	}
};
static_assert(processor_kernel<AddNoise>);
static_assert(has_context<AddNoise>);
static_assert(std::same_as<kernel_context_t<AddNoise>, Cong>);
static_assert(!lane_packable<AddNoise>);

/// The same, with no context.
struct Identity {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static constexpr Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[nodiscard, gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/,
	                                                  q31 x) {
		return x;
	}
};
static_assert(processor_kernel<Identity>);
static_assert(!has_context<Identity>);

// An owner cannot forget the context: a kernel with one has no context-free block render, and one without has no
// context-taking render.
template <typename M>
concept renders_without_context =
    requires(M m, std::span<const q31> in, std::span<q31> out) { m.renderBlock(in, out); };
template <typename M>
concept renders_with_context =
    requires(M m, std::span<const q31> in, std::span<q31> out, Cong& c) { m.renderBlock(in, out, c); };
static_assert(!renders_without_context<Mono<AddNoise>> && renders_with_context<Mono<AddNoise>>);
static_assert(renders_without_context<Mono<Identity>> && !renders_with_context<Mono<Identity>>);
} // namespace

// clang-format off
describe kernel_context("a kernel's per-block context", ${
	it("is drawn once per sample and written back after the block, under Mono", _ {
		Mono<AddNoise> mono;
		Cong noise{99};
		Cong reference{99};
		std::vector<q31> block(37, 0);
		mono.renderBlock(block, block, noise);
		int32_t mismatches = 0;
		for (q31 v : block) {
			mismatches += v != static_cast<q31>(reference.next() >> 8);
		}
		expect(mismatches).to_equal(0);
		expect(noise.state()).to_equal(reference.state());
	});

	it("is shared by both lanes under Stereo, left then right in every frame", _ {
		Stereo<AddNoise> stereo;
		Cong noise{7};
		Cong reference{7};
		std::vector<q31> interleaved(2 * 19, 0);
		stereo.renderBlock(as_frames(interleaved), as_frames(interleaved), noise);
		int32_t mismatches = 0;
		for (q31 v : interleaved) { // L0 R0 L1 R1 ...: the draws in order
			mismatches += v != static_cast<q31>(reference.next() >> 8);
		}
		expect(mismatches).to_equal(0);
		expect(noise.state()).to_equal(reference.state());
	});

	it("carries across blocks through the owner's stream", _ {
		Stereo<AddNoise> stereo;
		Cong noise{3};
		std::vector<q31> first(2 * 8, 0);
		std::vector<q31> second(2 * 8, 0);
		stereo.renderBlock(as_frames(first), as_frames(first), noise);
		stereo.renderBlock(as_frames(second), as_frames(second), noise);
		Stereo<AddNoise> whole;
		Cong once{3};
		std::vector<q31> both(2 * 16, 0);
		whole.renderBlock(as_frames(both), as_frames(both), once);
		first.insert(first.end(), second.begin(), second.end());
		expect(first).to_equal(both);
	});
});
// clang-format on

CPPSPEC_MAIN(kernel_context);
