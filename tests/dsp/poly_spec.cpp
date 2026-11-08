// Poly<K, N>: every lane, configured with its own settings, equals Mono<K> with those settings, bit for bit, at
// N = 2 and 4, for OnePole, Svf (every mode) and Smoother (both modes), on noise and on an impulse decaying through the
// flush-to-zero range; a fresh Poly has every lane parked; a parked lane changes no other; configure mid-stream takes
// effect at the next block.

#include "dsp-next/core/flush_to_zero.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/poly.h"
#include "dsp-next/float/filter/one_pole.h"
#include "dsp-next/float/filter/smoother.h"
#include "dsp-next/float/filter/svf.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cppspec.hpp>
#include <cstring>
#include <random>
#include <tuple>
#include <vector>

using namespace deluge::dsp;

// The spec's own float arithmetic rounds as written, as the kernels' does, so its twin built with the device's float
// flags (poly_unsafe_math_spec) compares the kernels against IEEE order. The headers above pin themselves.
#pragma clang fp contract(off) reassociate(off) reciprocal(off)

namespace nested_test {
template <class V>
struct Inner {
	V a{};
	V b{};
	// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
	static constexpr auto fields = std::tuple{&Inner::a, &Inner::b};
};
template <class V>
struct Outer {
	std::array<Inner<V>, 3> items{};
	Inner<V> one{};
	V x{};
	int tag = 7; // not a field: untouched by set_lane
	// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name poly_kernel requires.
	static constexpr auto fields = std::tuple{&Outer::items, &Outer::one, &Outer::x};
};
} // namespace nested_test

namespace {

/// Runs @p blocks blocks of @p block samples through a Poly and N Monos configured by @p settings_for, and expects
/// every unparked lane's output, and its state, to equal its Mono's bit for bit.
template <class K, std::size_t N, class SettingsFor>
void expect_lanes_equal_mono(auto& self, SettingsFor settings_for, uint32_t seed, std::size_t block = 128,
                             std::size_t parked_lane = N) {
	constexpr std::size_t kBlocks = 3;
	fp::FlushToZero ftz;
	Poly<K, N> poly;
	std::array<Mono<K>, N> mono{};
	std::array<std::vector<f32>, N> in{}, out{}, ref{};
	std::mt19937 rng{seed};
	std::uniform_real_distribution<f32> d{-1.f, 1.f};
	for (std::size_t l = 0; l < N; ++l) {
		poly.configure(l, settings_for(l));
		mono[l].configure(settings_for(l));
		in[l].resize(block * kBlocks);
		for (f32& x : in[l]) {
			x = d(rng);
		}
		out[l].resize(in[l].size());
		ref[l].resize(in[l].size());
	}
	if (parked_lane < N) {
		poly.park(parked_lane);
	}
	for (std::size_t b = 0; b < kBlocks; ++b) {
		std::array<fp::Signal, N> is{};
		std::array<fp::Buffer, N> os{};
		for (std::size_t l = 0; l < N; ++l) {
			is[l] = fp::Signal{in[l]}.subspan(b * block, block);
			os[l] = fp::Buffer{out[l]}.subspan(b * block, block);
			mono[l].render_block(is[l], fp::Buffer{ref[l]}.subspan(b * block, block));
		}
		poly.render_block(is, os);
	}
	for (std::size_t l = 0; l < N; ++l) {
		if (l == parked_lane) {
			expect(poly.parked(l)).to_be_true();
			// A parked lane is at rest.
			const auto s = poly.state(l);
			expect(std::bit_cast<std::array<uint32_t, sizeof(s) / 4>>(s)
			       == std::bit_cast<std::array<uint32_t, sizeof(s) / 4>>(typename K::State{}))
			    .to_be_true();
			continue;
		}
		for (std::size_t i = 0; i < out[l].size(); ++i) {
			expect(std::bit_cast<uint32_t>(out[l][i])).to_equal(std::bit_cast<uint32_t>(ref[l][i]));
		}
		const auto ps = poly.state(l);
		const auto ms = mono[l].state();
		expect(std::bit_cast<std::array<uint32_t, sizeof(ps) / 4>>(ps)
		       == std::bit_cast<std::array<uint32_t, sizeof(ms) / 4>>(ms))
		    .to_be_true();
	}
}

/// Whether two states, plain structs of floats, are bit-identical.
template <class S>
bool same_bits(const S& a, const S& b) {
	return std::memcmp(&a, &b, sizeof(S)) == 0;
}

constexpr uint32_t kSentinel = 0x7fc12345U; ///< The bit pattern parked lanes' outputs are prefilled with.

/// Five blocks of @p block samples with every lane reconfigured from @p settings_for(lane, block) before each block,
/// lane @p parked_lane parked before block @p park_block and un-parked (by its configure) before block
/// @p unpark_block. Unparked lanes' outputs and states must equal their Monos' bit for bit; a parked lane's state is
/// State{} and its output span is left unwritten, or empty when @p empty_when_parked.
template <class K, std::size_t N, class SettingsFor>
void expect_scenario_equals_mono(auto& self, SettingsFor settings_for, std::size_t block, std::size_t parked_lane,
                                 std::size_t park_block, std::size_t unpark_block, bool empty_when_parked) {
	constexpr std::size_t kBlocks = 5;
	fp::FlushToZero ftz;
	Poly<K, N> poly;
	std::array<Mono<K>, N> mono{};
	std::mt19937 rng{park_block * 31 + block};
	std::uniform_real_distribution<f32> d{-1.f, 1.f};
	for (std::size_t b = 0; b < kBlocks; ++b) {
		std::array<std::vector<f32>, N> in{}, out{}, ref{};
		std::array<fp::Signal, N> is{};
		std::array<fp::Buffer, N> os{};
		for (std::size_t l = 0; l < N; ++l) {
			if (b == park_block && l == parked_lane) {
				poly.park(l);
				mono[l].reset();
				expect(same_bits(poly.state(l), typename K::State{})).to_be_true();
			}
			if (!(l == parked_lane && b >= park_block && b < unpark_block)) {
				poly.configure(l, settings_for(l, b));
				mono[l].configure(settings_for(l, b));
			}
			in[l].resize(block);
			for (f32& x : in[l]) {
				x = d(rng);
			}
			out[l].assign(block, std::bit_cast<f32>(kSentinel));
			ref[l].resize(block);
			is[l] = fp::Signal{in[l]};
			os[l] = fp::Buffer{out[l]};
			if (poly.parked(l) && empty_when_parked) {
				is[l] = {};
				os[l] = {};
			}
		}
		poly.render_block(is, os);
		for (std::size_t l = 0; l < N; ++l) {
			if (poly.parked(l)) {
				expect(same_bits(poly.state(l), typename K::State{})).to_be_true();
				for (const f32 x : out[l]) {
					expect(std::bit_cast<uint32_t>(x)).to_equal(kSentinel);
				}
				continue;
			}
			mono[l].render_block(fp::Signal{in[l]}, fp::Buffer{ref[l]});
			for (std::size_t i = 0; i < block; ++i) {
				expect(std::bit_cast<uint32_t>(out[l][i])).to_equal(std::bit_cast<uint32_t>(ref[l][i]));
			}
			expect(same_bits(poly.state(l), mono[l].state())).to_be_true();
		}
	}
}

/// An impulse into every lane (lane l's of height 1/(l + 1)), then silence, for @p blocks blocks of 128, each lane
/// configured from @p settings_for(lane, block) before each block. Every lane's output and state must equal its Mono's
/// bit for bit all the way down, through the smallest normals, where flush-to-zero flushes the products to zero.
template <class K, std::size_t N, class SettingsFor>
void expect_decay_equals_mono(auto& self, SettingsFor settings_for, std::size_t blocks) {
	constexpr std::size_t kDecayBlock = 128;
	fp::FlushToZero ftz;
	Poly<K, N> poly;
	std::array<Mono<K>, N> mono{};
	std::array<std::vector<f32>, N> in{}, out{}, ref{};
	for (std::size_t l = 0; l < N; ++l) {
		in[l].assign(kDecayBlock * blocks, 0.f);
		in[l][0] = 1.f / static_cast<f32>(l + 1);
		out[l].resize(in[l].size());
		ref[l].resize(in[l].size());
	}
	for (std::size_t b = 0; b < blocks; ++b) {
		std::array<fp::Signal, N> is{};
		std::array<fp::Buffer, N> os{};
		for (std::size_t l = 0; l < N; ++l) {
			poly.configure(l, settings_for(l, b));
			mono[l].configure(settings_for(l, b));
			is[l] = fp::Signal{in[l]}.subspan(b * kDecayBlock, kDecayBlock);
			os[l] = fp::Buffer{out[l]}.subspan(b * kDecayBlock, kDecayBlock);
			mono[l].render_block(is[l], fp::Buffer{ref[l]}.subspan(b * kDecayBlock, kDecayBlock));
		}
		poly.render_block(is, os);
	}
	for (std::size_t l = 0; l < N; ++l) {
		f32 smallest = 1.f;
		for (std::size_t i = 0; i < out[l].size(); ++i) {
			expect(std::bit_cast<uint32_t>(out[l][i])).to_equal(std::bit_cast<uint32_t>(ref[l][i]));
			if (ref[l][i] != 0.f) {
				smallest = std::min(smallest, std::fabs(ref[l][i]));
			}
		}
		// The decay reached the range where a product of the state and a gain below 1 is subnormal.
		expect(smallest < 1e-37f).to_be_true();
		expect(same_bits(poly.state(l), mono[l].state())).to_be_true();
	}
}

template <std::size_t N>
void poly_specs(auto& self) {
	// clang-format off
	it("steps each OnePole lane exactly as Mono with its own cutoff", _ {
		for (uint32_t seed = 0; seed < 8; ++seed) {
			expect_lanes_equal_mono<fp::OnePole, N>(self,
			    [seed](std::size_t l) {
				    return fp::OnePole::Settings{.cutoff = 0.001f + (0.07f * static_cast<f32>((l + seed) % 7)),
				                                 .mode = fp::OnePole::Mode::HIGH_PASS};
			    },
			    seed);
		}
	});
	it("steps each Svf lane exactly as Mono with its own cutoff and Q, in every mode", _ {
		for (const auto mode : {fp::Svf::Mode::LOW_PASS, fp::Svf::Mode::BAND_PASS, fp::Svf::Mode::HIGH_PASS,
		                        fp::Svf::Mode::NOTCH}) {
			for (uint32_t seed = 0; seed < 8; ++seed) {
				expect_lanes_equal_mono<fp::Svf, N>(self,
				    [seed, mode](std::size_t l) {
					    return fp::Svf::Settings{.cutoff = 0.002f + (0.06f * static_cast<f32>((l + seed) % 5)),
					                             .q = 0.6f + static_cast<f32>((l * 3 + seed) % 9), .mode = mode};
				    },
				    seed);
			}
		}
	});
	it("decays each lane from an impulse through the flush-to-zero range exactly as Mono", _ {
		expect_decay_equals_mono<fp::OnePole, N>(self,
		    [](std::size_t l, std::size_t /*b*/) {
			    return fp::OnePole::Settings{.cutoff = 0.05f + (0.03f * static_cast<f32>(l))};
		    },
		    4);
		for (const auto mode : {fp::Svf::Mode::LOW_PASS, fp::Svf::Mode::BAND_PASS, fp::Svf::Mode::HIGH_PASS,
		                        fp::Svf::Mode::NOTCH}) {
			expect_decay_equals_mono<fp::Svf, N>(self,
			    [mode](std::size_t l, std::size_t /*b*/) {
				    return fp::Svf::Settings{.cutoff = 0.05f + (0.02f * static_cast<f32>(l)),
				                             .q = 0.6f + (0.2f * static_cast<f32>(l)), .mode = mode};
			    },
			    8);
		}
		// The smoother jumps to the lane's height in the first block, then decays toward zero.
		expect_decay_equals_mono<fp::Smoother, N>(self,
		    [](std::size_t l, std::size_t b) {
			    return b == 0 ? fp::Smoother::Settings{.target = 1.f / static_cast<f32>(l + 1), .time = 0.f}
			                  : fp::Smoother::Settings{.target = 0.f, .time = 3.f + static_cast<f32>(l)};
		    },
		    8);
	});
	it("steps each OnePole lane exactly as Mono over a block that is not a multiple of N", _ {
		expect_lanes_equal_mono<fp::OnePole, N>(self,
		    [](std::size_t l) {
			    return fp::OnePole::Settings{.cutoff = 0.02f + (0.05f * static_cast<f32>(l)),
			                                 .mode = fp::OnePole::Mode::LOW_PASS};
		    },
		    3, 125);
	});
	it("steps each exponential Smoother lane exactly as Mono with its own target", _ {
		expect_lanes_equal_mono<fp::Smoother, N>(self,
		    [](std::size_t l) {
			    return fp::Smoother::Settings{.target = -0.5f + (0.3f * static_cast<f32>(l)), .time = 20.f + static_cast<f32>(l),
			                                  .mode = fp::Smoother::Mode::EXPONENTIAL};
		    },
		    5);
	});
	it("steps each linear Smoother lane exactly as Mono with its own target, block by block", _ {
		for (const std::size_t block : {128uz, 125uz, 7uz, 1uz}) {
			expect_lanes_equal_mono<fp::Smoother, N>(self,
			    [](std::size_t l) {
				    return fp::Smoother::Settings{.target = 0.9f - (0.45f * static_cast<f32>(l)), .time = 0.f,
				                                  .mode = fp::Smoother::Mode::LINEAR};
			    },
			    11, block);
		}
	});
	it("leaves the other lanes unchanged when one is parked", _ {
		expect_lanes_equal_mono<fp::OnePole, N>(self,
		    [](std::size_t l) { return fp::OnePole::Settings{.cutoff = 0.01f * static_cast<f32>(l + 1)}; }, 9, 128, 1);
	});
	it("leaves the other lanes unchanged when a linear Smoother lane is parked", _ {
		expect_lanes_equal_mono<fp::Smoother, N>(self,
		    [](std::size_t l) {
			    return fp::Smoother::Settings{.target = 0.2f * static_cast<f32>(l + 1), .mode = fp::Smoother::Mode::LINEAR};
		    },
		    4, 125, 0);
	});
	it("takes a lane parked mid-stream to rest and leaves the others equal to Mono", _ {
		for (const bool empty : {false, true}) {
			for (const std::size_t block : {128uz, 126uz}) {
				expect_scenario_equals_mono<fp::OnePole, N>(
				    self,
				    [](std::size_t l, std::size_t /*b*/) {
					    return fp::OnePole::Settings{.cutoff = 0.03f * static_cast<f32>(l + 1)};
				    },
				    block, N - 1, 2, 5, empty);
				expect_scenario_equals_mono<fp::Smoother, N>(
				    self,
				    [](std::size_t l, std::size_t /*b*/) {
					    return fp::Smoother::Settings{.target = 0.3f * static_cast<f32>(l + 1), .mode = fp::Smoother::Mode::LINEAR};
				    },
				    block, 0, 2, 5, empty);
			}
		}
	});
	it("runs a lane that is configured again after parking exactly as Mono from rest", _ {
		for (const bool empty : {false, true}) {
			expect_scenario_equals_mono<fp::OnePole, N>(
			    self,
			    [](std::size_t l, std::size_t /*b*/) {
				    return fp::OnePole::Settings{.cutoff = 0.02f * static_cast<f32>(l + 2)};
			    },
			    126, 1, 1, 3, empty);
			expect_scenario_equals_mono<fp::Smoother, N>(
			    self,
			    [](std::size_t l, std::size_t b) {
				    return fp::Smoother::Settings{.target = 0.2f * static_cast<f32>(l) - 0.1f * static_cast<f32>(b),
				                                  .mode = fp::Smoother::Mode::LINEAR};
			    },
			    126, 1, 1, 3, empty);
		}
	});
	it("applies a Smoother configure made between blocks at the next block, in both modes", _ {
		for (const auto mode : {fp::Smoother::Mode::EXPONENTIAL, fp::Smoother::Mode::LINEAR}) {
			expect_scenario_equals_mono<fp::Smoother, N>(
			    self,
			    [mode](std::size_t l, std::size_t b) {
				    return fp::Smoother::Settings{.target = 0.25f * static_cast<f32>(b) - 0.2f * static_cast<f32>(l),
				                                  .time = 10.f + static_cast<f32>(3 * b + l), .mode = mode};
			    },
			    126, N, 5, 5, false);
		}
	});
	it("starts with every lane parked, so a lane never configured is never rendered", _ {
		fp::FlushToZero ftz;
		Poly<fp::OnePole, N> poly;
		for (std::size_t l = 0; l < N; ++l) {
			expect(poly.parked(l)).to_be_true();
		}
		Mono<fp::OnePole> mono;
		poly.configure(0, {.cutoff = 0.07f});
		mono.configure({.cutoff = 0.07f});
		std::vector<f32> in(125), out(125), ref(125);
		for (std::size_t i = 0; i < in.size(); ++i) {
			in[i] = std::sin(0.1f * static_cast<f32>(i));
		}
		std::array<fp::Signal, N> is{};
		std::array<fp::Buffer, N> os{};
		is[0] = fp::Signal{in};
		os[0] = fp::Buffer{out};
		poly.render_block(is, os);
		mono.render_block(fp::Signal{in}, fp::Buffer{ref});
		for (std::size_t i = 0; i < in.size(); ++i) {
			expect(std::bit_cast<uint32_t>(out[i])).to_equal(std::bit_cast<uint32_t>(ref[i]));
		}
		expect(poly.parked(0)).to_be_false();
		for (std::size_t l = 1; l < N; ++l) {
			expect(poly.parked(l)).to_be_true();
			expect(same_bits(poly.state(l), fp::OnePole::State{})).to_be_true();
		}
	});
	it("un-parks a lane when it is configured", _ {
		Poly<fp::OnePole, N> poly;
		poly.configure(0, {.cutoff = 0.1f});
		poly.park(0);
		expect(poly.parked(0)).to_be_true();
		poly.configure(0, {.cutoff = 0.1f});
		expect(poly.parked(0)).to_be_false();
	});
	it("applies a configure made between blocks at the next block", _ {
		fp::FlushToZero ftz;
		Poly<fp::OnePole, N> poly;
		Mono<fp::OnePole> mono;
		poly.configure(0, {.cutoff = 0.05f});
		mono.configure({.cutoff = 0.05f});
		std::vector<f32> in(64, 1.f), a(64), b(64);
		for (int block = 0; block < 2; ++block) {
			std::array<fp::Signal, N> is{};
			std::array<fp::Buffer, N> os{};
			std::vector<f32> sink(64);
			for (std::size_t l = 0; l < N; ++l) {
				poly.configure(l, {.cutoff = l == 0 ? 0.05f + (0.1f * static_cast<f32>(block)) : 0.2f});
				is[l] = fp::Signal{in};
				os[l] = l == 0 ? fp::Buffer{a} : fp::Buffer{sink};
			}
			mono.configure({.cutoff = 0.05f + (0.1f * static_cast<f32>(block))});
			mono.render_block(fp::Signal{in}, fp::Buffer{b});
			poly.render_block(is, os);
			for (std::size_t i = 0; i < 64; ++i) {
				expect(std::bit_cast<uint32_t>(a[i])).to_equal(std::bit_cast<uint32_t>(b[i]));
			}
		}
	});
	it("moves a lane through arrays and nested field structs", _ {
		using nested_test::Outer;
		const auto make = [](f32 base) {
			Outer<f32> o;
			f32 v = base;
			for (auto& item : o.items) {
				item.a = v++;
				item.b = v++;
			}
			o.one.a = v++;
			o.one.b = v++;
			o.x = v;
			return o;
		};
		const auto same = [](const Outer<f32>& p, const Outer<f32>& q) {
			const auto b = [](f32 x) { return std::bit_cast<uint32_t>(x); };
			bool eq = b(p.x) == b(q.x) && b(p.one.a) == b(q.one.a) && b(p.one.b) == b(q.one.b);
			for (std::size_t k = 0; k < 3; ++k) {
				eq = eq && b(p.items[k].a) == b(q.items[k].a) && b(p.items[k].b) == b(q.items[k].b);
			}
			return eq;
		};
		Outer<Pack<f32, N>> packed;
		for (std::size_t l = 0; l < N; ++l) {
			set_lane(packed, l, make(10.f * static_cast<f32>(l + 1)));
		}
		for (std::size_t l = 0; l < N; ++l) {
			expect(same(get_lane<Outer<f32>>(packed, l), make(10.f * static_cast<f32>(l + 1)))).to_be_true();
		}
		Outer<f32> changed = make(-5.f);
		changed.tag = 99;
		set_lane(packed, 0, changed);
		expect(same(get_lane<Outer<f32>>(packed, 0), make(-5.f))).to_be_true();
		for (std::size_t l = 1; l < N; ++l) {
			expect(same(get_lane<Outer<f32>>(packed, l), make(10.f * static_cast<f32>(l + 1)))).to_be_true();
		}
		expect(packed.tag).to_equal(7);
		expect(get_lane<Outer<f32>>(packed, 0).tag).to_equal(7);
	});
	it("reads back each lane's state, and resets one lane alone", _ {
		fp::FlushToZero ftz;
		Poly<fp::OnePole, N> poly;
		std::array<Mono<fp::OnePole>, N> mono{};
		for (std::size_t l = 0; l < N; ++l) {
			poly.configure(l, {.cutoff = 0.1f});
			mono[l].configure({.cutoff = 0.1f});
		}
		std::array<std::vector<f32>, N> in{}, out{};
		std::array<fp::Signal, N> is{};
		std::array<fp::Buffer, N> os{};
		for (std::size_t l = 0; l < N; ++l) {
			in[l].assign(16, 0.25f + (static_cast<f32>(l) * 0.25f));
			out[l].resize(16);
			is[l] = fp::Signal{in[l]};
			os[l] = fp::Buffer{out[l]};
			std::vector<f32> sink(16);
			mono[l].render_block(is[l], fp::Buffer{sink});
		}
		poly.render_block(is, os);
		for (std::size_t l = 0; l < N; ++l) {
			expect(std::bit_cast<uint32_t>(poly.state(l).s)).to_equal(std::bit_cast<uint32_t>(mono[l].state().s));
			expect(poly.state(l).s > 0.f).to_be_true();
		}
		poly.reset(1);
		expect(poly.state(1).s).to_equal(0.f);
		expect(poly.state(0).s > 0.f).to_be_true();
	});
	// clang-format on
}

} // namespace

// clang-format off
describe poly_spec("Poly<K, N>", ${
	context("at N = 2", _ { poly_specs<2>(self); });
	context("at N = 4", _ { poly_specs<4>(self); });
});
// clang-format on

CPPSPEC_MAIN(poly_spec);
