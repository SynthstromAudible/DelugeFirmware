#include "dsp-next/core/buffer_view.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/noise/noise.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace deluge::dsp;

namespace {

/// The one noise stream both channels of a NoisyAdd draw from.
fixed::SeededNoise shared_noise{99};

/// A noise policy reading shared_noise.
struct SharedSeeded {
	[[gnu::always_inline]] static q31 render() { return q31::from_raw(shared_noise.render()); }
};

/// A processor kernel adding one draw of @p Noise to each sample.
template <typename Noise>
struct NoisyAdd {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return add_wrap(x, Noise::render());
	}
};

/// A processor kernel whose prepare draws one sample of @p Noise per block, added to every sample of the block.
template <typename Noise>
struct NoisyPrepare {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	struct Block {
		q31 offset; ///< The block's draw.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static Block prepare(const Coefficients& /*c*/, State& /*s*/, std::size_t /*n*/) {
		return {.offset = Noise::render()};
	}
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& b, State& /*s*/, q31 x) {
		return add_wrap(x, b.offset);
	}
};

/// A processor kernel without modes or prepare: a Q31 gain (Q1.31, scaled ×2 to reach unity).
struct Gain {
	using value_type = q31;
	using step_type = q31;
	struct Settings {
		q31 gain; ///< Q1.31.
	};
	struct Coefficients {
		q31 gain; ///< Q1.31.
	};
	struct State {};
	static Coefficients coefficients(const Settings& s) { return {.gain = s.gain}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return mul_as_wrap<q31>(x, c.gain);
	}
};

/// How many times CountingCoefficients::coefficients has run.
int coefficient_calls = 0;

/// A pass-through kernel whose coefficients() counts its calls.
struct CountingCoefficients {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {};
	static Coefficients coefficients(const Settings& /*s*/) {
		++coefficient_calls;
		return {};
	}
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A pass-through kernel whose prepare counts the blocks it is called for.
struct CountingPrepare {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		int prepares; ///< How many times prepare has run.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	static EmptyBlock prepare(const Coefficients& /*c*/, State& s, std::size_t /*n*/) {
		++s.prepares;
		return {};
	}
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& /*s*/, q31 x) {
		return x;
	}
};

/// A processor kernel returning the running sum of its input.
struct Accumulate {
	using value_type = q31;
	using step_type = q31;
	struct Settings {};
	struct Coefficients {};
	struct State {
		q31 acc; ///< The sum so far.
	};
	static Coefficients coefficients(const Settings& /*s*/) { return {}; }
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s, q31 x) {
		s.acc = add_wrap(s.acc, x);
		return s.acc;
	}
};

/// A moded processor with a state-dependent Block: accumulates its input halved or doubled by mode, and offsets
/// each output by a sixteenth of the sum at the start of the block.
struct ModedBiased {
	using value_type = q31;
	using step_type = q31;
	enum class Mode : uint8_t { HALF, DOUBLE };
	using Modes = ModeList<Mode::HALF, Mode::DOUBLE>;
	struct Settings {
		Mode mode;
	};
	struct Coefficients {
		Mode mode;
	};
	struct State {
		q31 acc; ///< The sum so far.
	};
	struct Block {
		q31 bias; ///< A sixteenth of the sum at the start of the block.
	};
	static Coefficients coefficients(const Settings& s) { return {.mode = s.mode}; }
	static Block prepare(const Coefficients& /*c*/, State& s, std::size_t /*n*/) {
		return {.bias = scale_shr_floor<4>(s.acc)};
	}
	template <Mode M>
	[[gnu::always_inline]] static q31 step(const Coefficients& /*c*/, const Block& b, State& s, q31 x) {
		if constexpr (M == Mode::HALF) {
			s.acc = add_wrap(s.acc, scale_shr_floor<1>(x));
		}
		else {
			s.acc = add_wrap(s.acc, scale_shl_wrap<1>(x));
		}
		return add_wrap(s.acc, b.bias);
	}
};

static_assert(processor<Stereo<Gain>>);
static_assert(processor<Stereo<ModedBiased>>);
static_assert(std::same_as<Stereo<Gain>::value_type, StereoSample>);
static_assert(!std::is_polymorphic_v<Stereo<Gain>>);

// An owning temporary would leave the view dangling; a non-q31 range is not samples at all.
template <typename R>
concept viewable_as_frames = requires(R&& r) { as_frames(std::forward<R>(r)); };
static_assert(!viewable_as_frames<std::vector<q31>>);
static_assert(!viewable_as_frames<std::vector<float>&>);
static_assert(viewable_as_frames<std::vector<q31>&>);
static_assert(viewable_as_frames<std::span<q31>>);

/// A deterministic test input of @p n stereo frames, distinct per @p seed and per channel.
std::vector<StereoSample> frames_of(std::size_t n, int32_t seed) {
	std::vector<StereoSample> v(n);
	for (std::size_t i = 0; i < n; ++i) {
		const auto k = static_cast<int32_t>(i + 1);
		v[i] = {.l = Q<31>::from_raw(seed * k - 0x01234567), .r = Q<31>::from_raw(0x00765432 - seed * k * 3)};
	}
	return v;
}

/// One hand step of ModedBiased at @p mode.
q31 moded_biased_step(ModedBiased::Mode mode, const ModedBiased::Block& b, ModedBiased::State& s, q31 x) {
	const ModedBiased::Coefficients c{.mode = mode};
	return mode == ModedBiased::Mode::HALF ? ModedBiased::step<ModedBiased::Mode::HALF>(c, b, s, x)
	                                       : ModedBiased::step<ModedBiased::Mode::DOUBLE>(c, b, s, x);
}

/// The left (or right) channel of @p frames.
std::vector<q31> channel(const std::vector<StereoSample>& frames, bool right) {
	std::vector<q31> v(frames.size());
	for (std::size_t i = 0; i < frames.size(); ++i) {
		v[i] = right ? frames[i].r : frames[i].l;
	}
	return v;
}

} // namespace

// clang-format off
describe stereo("stereo frames and Stereo<K>", ${
	it("adds frames per channel, wrapping as add_wrap", _ {
		const StereoSample a{.l = q<31>(0.75), .r = q<31>(-0.75)};
		const StereoSample b{.l = q<31>(0.5), .r = q<31>(-0.5)};
		const StereoSample sum = a + b;
		expect(sum.l.raw()).to_equal(add_wrap(a.l, b.l).raw());
		expect(sum.r.raw()).to_equal(add_wrap(a.r, b.r).raw());
		expect(sum.l.raw()).to_be_less_than(0); // 1.25 wraps negative
		expect(sum.r.raw()).to_be_greater_than(0); // -1.25 wraps positive
		StereoSample acc = a;
		acc += b;
		expect(acc.l.raw()).to_equal(sum.l.raw());
		expect(acc.r.raw()).to_equal(sum.r.raw());
	});

	it("views an interleaved buffer as frames, left then right", _ {
		std::array<int32_t, 6> raw{1, 2, 3, 4, 5, 6};
		auto frames = as_frames(typed_view(std::span{raw}));
		expect(frames.size()).to_equal(3u);
		expect(frames[1].l.raw()).to_equal(3);
		expect(frames[1].r.raw()).to_equal(4);
		frames[2].r = Q<31>::from_raw(60);
		expect(raw[5]).to_equal(60);
	});
	it("views a const buffer as a read-only signal", _ {
		std::array<int32_t, 4> raw{1, 2, 3, 4};
		fixed::StereoSignal frames = as_frames(typed_view(std::span{std::as_const(raw)}));
		expect(frames.size()).to_equal(2u);
		expect(frames[1].l.raw()).to_equal(3);
		expect(frames[1].r.raw()).to_equal(4);
	});
	it("leaves an odd trailing sample outside the view", _ {
		std::array<int32_t, 5> raw{1, 2, 3, 4, 5};
		expect(as_frames(typed_view(std::span{raw})).size()).to_equal(2u);
	});

	it("draws for the left channel before the right on every frame", _ {
		fixed::SeededNoise expected{shared_noise.state()};
		Stereo<NoisyAdd<SharedSeeded>> st;
		for (const std::size_t n : {1u, 4u, 7u}) {
			std::vector<StereoSample> frames(n);
			st.render_block(frames, frames);
			for (const StereoSample& frame : frames) {
				expect(frame.l.raw()).to_equal(expected.render());
				expect(frame.r.raw()).to_equal(expected.render());
			}
		}
		const StereoSample one = st.render(StereoSample{});
		expect(one.l.raw()).to_equal(expected.render());
		expect(one.r.raw()).to_equal(expected.render());
	});

	it("prepares the left channel before the right on every block", _ {
		fixed::SeededNoise expected{shared_noise.state()};
		Stereo<NoisyPrepare<SharedSeeded>> st;
		for (const std::size_t n : {1u, 4u, 7u}) {
			std::vector<StereoSample> frames(n);
			st.render_block(frames, frames);
			const int32_t left = expected.render();
			const int32_t right = expected.render();
			for (const StereoSample& frame : frames) {
				expect(frame.l.raw()).to_equal(left);
				expect(frame.r.raw()).to_equal(right);
			}
		}
	});

	it("computes coefficients once per configure, shared by both channels", _ {
		const int before = coefficient_calls;
		Stereo<CountingCoefficients> st{{}};
		expect(coefficient_calls - before).to_equal(1);
		st.configure({});
		expect(coefficient_calls - before).to_equal(2);
		std::vector<StereoSample> frames(5);
		st.render_block(frames, frames);
		expect(coefficient_calls - before).to_equal(2);
	});

	it("runs prepare once per channel per block", _ {
		std::vector<StereoSample> frames(13);
		Stereo<CountingPrepare> st;
		st.render_block(frames, frames);
		expect(st.left().prepares).to_equal(1);
		expect(st.right().prepares).to_equal(1);
		st.render_block(frames, frames);
		st.render_block(frames, frames);
		expect(st.left().prepares).to_equal(3);
		expect(st.right().prepares).to_equal(3);
		std::ignore = st.render({.l = Q<31>::from_raw(1), .r = Q<31>::from_raw(2)});
		expect(st.left().prepares).to_equal(4);
		expect(st.right().prepares).to_equal(4);
	});

	it("keeps a state per channel", _ {
		const std::vector<StereoSample> in = frames_of(9, 0x00031415);
		std::vector<StereoSample> out(in.size());
		Stereo<Accumulate> st;
		st.render_block(in, out);
		int32_t l = 0;
		int32_t r = 0;
		for (std::size_t i = 0; i < in.size(); ++i) {
			l = wrapping_add(l, in[i].l.raw());
			r = wrapping_add(r, in[i].r.raw());
			expect(out[i].l.raw()).to_equal(l);
			expect(out[i].r.raw()).to_equal(r);
		}
		expect(st.left().acc.raw()).to_equal(l);
		expect(st.right().acc.raw()).to_equal(r);
		st.reset();
		expect(st.left().acc.raw()).to_equal(0);
		expect(st.right().acc.raw()).to_equal(0);
	});

	it("equals two Monos and a hand loop over step, across a mode switch", _ {
		for (std::size_t n = 1; n <= 9; ++n) {
			Stereo<ModedBiased> st{{.mode = ModedBiased::Mode::HALF}};
			Mono<ModedBiased> left{{.mode = ModedBiased::Mode::HALF}};
			Mono<ModedBiased> right{{.mode = ModedBiased::Mode::HALF}};
			ModedBiased::State hand_l{};
			ModedBiased::State hand_r{};
			ModedBiased::Mode mode = ModedBiased::Mode::HALF;
			for (int block = 0; block < 3; ++block) {
				if (block == 1) {
					mode = ModedBiased::Mode::DOUBLE;
					st.configure({.mode = mode});
					left.configure({.mode = mode});
					right.configure({.mode = mode});
				}
				const std::vector<StereoSample> in = frames_of(n, 0x00012345 * (block + 1));
				std::vector<StereoSample> out(n);
				st.render_block(in, out);
				const std::vector<q31> l_in = channel(in, false);
				const std::vector<q31> r_in = channel(in, true);
				std::vector<q31> l_out(n);
				std::vector<q31> r_out(n);
				left.render_block(l_in, l_out);
				right.render_block(r_in, r_out);
				const ModedBiased::Coefficients c{.mode = mode};
				const ModedBiased::Block hand_bl = ModedBiased::prepare(c, hand_l, n);
				const ModedBiased::Block hand_br = ModedBiased::prepare(c, hand_r, n);
				for (std::size_t i = 0; i < n; ++i) {
					expect(out[i].l.raw()).to_equal(l_out[i].raw());
					expect(out[i].r.raw()).to_equal(r_out[i].raw());
					expect(out[i].l.raw()).to_equal(moded_biased_step(mode, hand_bl, hand_l, in[i].l).raw());
					expect(out[i].r.raw()).to_equal(moded_biased_step(mode, hand_br, hand_r, in[i].r).raw());
				}
			}
			expect(st.left().acc.raw()).to_equal(left.state().acc.raw());
			expect(st.right().acc.raw()).to_equal(right.state().acc.raw());
			expect(st.coefficients().mode).to_equal(ModedBiased::Mode::DOUBLE);
		}
	});

	it("renders in place exactly as out of place", _ {
		for (std::size_t n = 1; n <= 9; ++n) {
			const std::vector<StereoSample> in = frames_of(n, 0x00054321);
			std::vector<StereoSample> out(n);
			std::vector<StereoSample> in_place = in;
			Stereo<ModedBiased> st_out{{.mode = ModedBiased::Mode::DOUBLE}};
			Stereo<ModedBiased> st_in{{.mode = ModedBiased::Mode::DOUBLE}};
			for (int block = 0; block < 2; ++block) {
				st_out.render_block(in, out);
				st_in.render_block(in_place, in_place);
				for (std::size_t i = 0; i < n; ++i) {
					expect(in_place[i].l.raw()).to_equal(out[i].l.raw());
					expect(in_place[i].r.raw()).to_equal(out[i].r.raw());
				}
				in_place = in;
			}
		}
	});

	it("renders every frame of a block of 1 to 9 frames and nothing past it", _ {
		const Gain::Settings settings{.gain = q31::from_raw(0x30000000)};
		const Gain::Coefficients c = Gain::coefficients(settings);
		for (std::size_t n = 1; n <= 9; ++n) {
			const std::vector<StereoSample> in = frames_of(n + 1, 0x00321fed);
			std::vector<StereoSample> out(n + 1, StereoSample{.l = Q<31>::from_raw(77), .r = Q<31>::from_raw(-77)});
			Stereo<Gain> st{settings};
			st.render_block(std::span{in}.first(n), std::span{out}.first(n));
			Gain::State s{};
			for (std::size_t i = 0; i < n; ++i) {
				expect(out[i].l.raw()).to_equal(Gain::step(c, {}, s, in[i].l).raw());
				expect(out[i].r.raw()).to_equal(Gain::step(c, {}, s, in[i].r).raw());
			}
			expect(out[n].l.raw()).to_equal(77);
			expect(out[n].r.raw()).to_equal(-77);
		}
	});

	it("configures both channels from one setting and renders a block", _ {
		Stereo<Gain> st;
		st.configure({.gain = q31::from_raw(0x40000000)});
		expect(st.coefficients().gain.raw()).to_equal(0x40000000);
		std::array<int32_t, 4> raw{2000, 4000, 6000, 8000};
		auto frames = as_frames(typed_view(std::span{raw}));
		st.render_block(frames, frames);
		expect(raw).to_equal(std::array<int32_t, 4>{1000, 2000, 3000, 4000});
	});
});

CPPSPEC_MAIN(stereo);
