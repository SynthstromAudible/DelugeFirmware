#include "dsp-next/core/stereo.h"
#include "dsp-next/fixed/noise/noise.h"

#include <array>
#include <cppspec.hpp>
#include <span>
#include <utility>
#include <vector>

using namespace deluge::dsp;

namespace {
/// A mono processor that adds a draw from a noise stream it shares with its sibling.
struct NoisyAdd : Processor<NoisyAdd, q31> {
	explicit NoisyAdd(fixed::SeededNoise* shared) : noise{shared} {}
	fixed::SeededNoise* noise; // test-only: both channels draw from one stream
	[[nodiscard]] q31 render(q31 s) { return s + noise->render(); }
};
struct Gain : Processor<Gain, q31> {
	struct Settings {
		q31 gain;
	};
	q31 gain = 1;
	void configure(const Settings& s) { gain = s.gain; }
	[[nodiscard]] q31 render(q31 s) { return s * gain; }
};
static_assert(processor<Stereo<Gain>>);
static_assert(std::same_as<Stereo<Gain>::value_type, StereoSample>);
// An owning temporary would leave the view dangling; a non-q31 range is not samples at all.
template <typename R>
concept viewable_as_frames = requires(R&& r) { as_frames(std::forward<R>(r)); };
static_assert(!viewable_as_frames<std::vector<q31>>);
static_assert(!viewable_as_frames<std::vector<float>&>);
static_assert(viewable_as_frames<std::vector<q31>&>);
static_assert(viewable_as_frames<std::span<q31>>);
} // namespace

// clang-format off
describe stereo("stereo frames and Stereo<P>", ${
	it("views an interleaved buffer as frames, left then right", _ {
		std::array<q31, 6> raw{1, 2, 3, 4, 5, 6};
		auto frames = as_frames(raw);
		expect(frames.size()).to_equal(3u);
		expect(frames[1].l).to_equal(3);
		expect(frames[1].r).to_equal(4);
		frames[2].r = 60;
		expect(raw[5]).to_equal(60);
	});
	it("views a const buffer as a read-only signal", _ {
		std::array<q31, 4> raw{1, 2, 3, 4};
		fixed::StereoSignal frames = as_frames(std::as_const(raw));
		expect(frames.size()).to_equal(2u);
		expect(frames[1].l).to_equal(3);
		expect(frames[1].r).to_equal(4);
	});
	it("leaves an odd trailing sample outside the view", _ {
		std::array<q31, 5> raw{1, 2, 3, 4, 5};
		expect(as_frames(raw).size()).to_equal(2u);
	});
	it("draws for the left channel before the right on every frame", _ {
		fixed::SeededNoise shared{99};
		Stereo<NoisyAdd> st{NoisyAdd{&shared}, NoisyAdd{&shared}};
		fixed::SeededNoise expected{99};
		for (int i = 0; i < 4; i++) {
			const StereoSample out = st.render({.l = 0, .r = 0});
			expect(out.l).to_equal(expected.render());
			expect(out.r).to_equal(expected.render());
		}
	});
	it("configures both channels from one setting and renders a block", _ {
		Stereo<Gain> st;
		st.configure({.gain = 3});
		std::array<q31, 4> raw{1, 2, 3, 4};
		auto frames = as_frames(raw);
		st.renderBlock(frames, frames);
		expect(raw).to_equal(std::array<q31, 4>{3, 6, 9, 12});
	});
});

CPPSPEC_MAIN(stereo);
