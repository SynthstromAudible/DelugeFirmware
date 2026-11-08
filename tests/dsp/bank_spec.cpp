// Bank<K, N, Mix>: one input into N copies, each with its own settings, mixed: Sum equals the Mono outputs summed in
// lane order, Weighted the same with each lane's gain, StereoPair the even lanes to left and the odd to right, bit for
// bit, at N = 2 and 4; a Smoother bank (which prepares each block) equals the summed Monos; reset and state act on one
// lane; configure between blocks takes effect at the next block.

#include "dsp-next/core/bank.h"
#include "dsp-next/core/flush_to_zero.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/float/filter/smoother.h"
#include "dsp-next/float/filter/svf.h"

#include <array>
#include <bit>
#include <cppspec.hpp>
#include <cstring>
#include <random>
#include <vector>

using namespace deluge::dsp;

namespace {

constexpr std::size_t kBlock = 128;

[[nodiscard]] fp::Svf::Settings band(std::size_t l) {
	return {.cutoff = 0.01f * static_cast<f32>(1U << l), .q = 5.f, .mode = fp::Svf::Mode::BAND_PASS};
}

[[nodiscard]] std::vector<f32> input(std::size_t n = kBlock * 2) {
	std::mt19937 rng{11};
	std::uniform_real_distribution<f32> d{-1.f, 1.f};
	std::vector<f32> v(n);
	for (f32& x : v) {
		x = d(rng);
	}
	return v;
}

template <class S>
[[nodiscard]] bool same_bits(const S& a, const S& b) {
	return std::memcmp(&a, &b, sizeof(S)) == 0;
}

/// Each lane's Mono output for the input, configured by band().
template <std::size_t N>
[[nodiscard]] std::array<std::vector<f32>, N> mono_outputs(const std::vector<f32>& in) {
	std::array<std::vector<f32>, N> r{};
	for (std::size_t l = 0; l < N; ++l) {
		Mono<fp::Svf> m;
		m.configure(band(l));
		r[l].resize(in.size());
		m.render_block(fp::Signal{in}, fp::Buffer{r[l]});
	}
	return r;
}

/// Runs @p blocks blocks of kBlock samples through a Sum bank and N Monos, every lane reconfigured from
/// @p settings_for(lane, block) before each block, and expects the bank's output to be the Monos' outputs added in lane
/// order, and each lane's state to equal its Mono's, bit for bit.
template <class K, std::size_t N, class SettingsFor>
void expect_sum_equals_monos(auto& self, SettingsFor settings_for, std::size_t blocks) {
	fp::FlushToZero ftz;
	Bank<K, N, mix::Sum> bank;
	std::array<Mono<K>, N> mono{};
	const auto in = input(kBlock * blocks);
	for (std::size_t b = 0; b < blocks; ++b) {
		const auto is = fp::Signal{in}.subspan(b * kBlock, kBlock);
		std::array<std::vector<f32>, N> ref{};
		for (std::size_t l = 0; l < N; ++l) {
			bank.configure(l, settings_for(l, b));
			mono[l].configure(settings_for(l, b));
			ref[l].resize(kBlock);
			mono[l].render_block(is, fp::Buffer{ref[l]});
		}
		std::vector<f32> out(kBlock);
		bank.render_block(is, fp::Buffer{out});
		for (std::size_t i = 0; i < kBlock; ++i) {
			f32 expected = ref[0][i];
			for (std::size_t l = 1; l < N; ++l) {
				expected = expected + ref[l][i];
			}
			expect(std::bit_cast<uint32_t>(out[i])).to_equal(std::bit_cast<uint32_t>(expected));
		}
		for (std::size_t l = 0; l < N; ++l) {
			expect(same_bits(bank.state(l), mono[l].state())).to_be_true();
		}
	}
}

template <std::size_t N>
void bank_specs(auto& self) {
	// clang-format off
	it("sums the lanes in lane order", _ {
		fp::FlushToZero ftz;
		const auto in = input();
		const auto ref = mono_outputs<N>(in);
		Bank<fp::Svf, N, mix::Sum> bank;
		for (std::size_t l = 0; l < N; ++l) {
			bank.configure(l, band(l));
		}
		std::vector<f32> out(in.size());
		bank.render_block(fp::Signal{in}, fp::Buffer{out});
		for (std::size_t i = 0; i < in.size(); ++i) {
			f32 expected = ref[0][i];
			for (std::size_t l = 1; l < N; ++l) {
				expected = expected + ref[l][i];
			}
			expect(std::bit_cast<uint32_t>(out[i])).to_equal(std::bit_cast<uint32_t>(expected));
		}
	});
	it("weights each lane by its gain, multiplying then adding unfused", _ {
		fp::FlushToZero ftz;
		const auto in = input();
		const auto ref = mono_outputs<N>(in);
		Bank<fp::Svf, N, mix::Weighted> bank;
		constexpr std::array<f32, 4> gains{1.f, 0.5f, 0.25f, 2.f};
		for (std::size_t l = 0; l < N; ++l) {
			bank.configure(l, band(l));
			bank.set_gain(l, gains[l]);
		}
		std::vector<f32> out(in.size());
		bank.render_block(fp::Signal{in}, fp::Buffer{out});
		for (std::size_t i = 0; i < in.size(); ++i) {
			// The bank's order: y * gains, then the lane sum. Written with explicit temporaries so no multiply-add
			// is formed here either.
			f32 expected = ref[0][i] * gains[0];
			for (std::size_t l = 1; l < N; ++l) {
				const f32 term = ref[l][i] * gains[l];
				expected = expected + term;
			}
			expect(std::bit_cast<uint32_t>(out[i])).to_equal(std::bit_cast<uint32_t>(expected));
		}
	});
	it("mixes a Weighted lane whose gain was never set at gain 1", _ {
		fp::FlushToZero ftz;
		const auto in = input();
		const auto ref = mono_outputs<N>(in);
		Bank<fp::Svf, N, mix::Weighted> bank;
		for (std::size_t l = 0; l < N; ++l) {
			bank.configure(l, band(l));
		}
		bank.set_gain(0, 0.5f);
		std::vector<f32> out(in.size());
		bank.render_block(fp::Signal{in}, fp::Buffer{out});
		for (std::size_t i = 0; i < in.size(); ++i) {
			f32 expected = ref[0][i] * 0.5f;
			for (std::size_t l = 1; l < N; ++l) {
				const f32 term = ref[l][i] * 1.f;
				expected = expected + term;
			}
			expect(std::bit_cast<uint32_t>(out[i])).to_equal(std::bit_cast<uint32_t>(expected));
		}
	});
	it("sends even lanes left and odd lanes right", _ {
		fp::FlushToZero ftz;
		const auto in = input();
		const auto ref = mono_outputs<N>(in);
		Bank<fp::Svf, N, mix::StereoPair> bank;
		for (std::size_t l = 0; l < N; ++l) {
			bank.configure(l, band(l));
		}
		std::vector<f32> left(in.size()), right(in.size());
		bank.render_block(fp::Signal{in}, fp::Buffer{left}, fp::Buffer{right});
		for (std::size_t i = 0; i < in.size(); ++i) {
			const f32 el = N == 4 ? ref[0][i] + ref[N - 2][i] : ref[0][i];
			const f32 er = N == 4 ? ref[1][i] + ref[N - 1][i] : ref[1][i];
			expect(std::bit_cast<uint32_t>(left[i])).to_equal(std::bit_cast<uint32_t>(el));
			expect(std::bit_cast<uint32_t>(right[i])).to_equal(std::bit_cast<uint32_t>(er));
		}
	});
	it("steps a Smoother bank, which prepares each block, as the summed Monos in both modes", _ {
		for (const auto mode : {fp::Smoother::Mode::EXPONENTIAL, fp::Smoother::Mode::LINEAR}) {
			expect_sum_equals_monos<fp::Smoother, N>(self,
			    [mode](std::size_t l, std::size_t) {
				    return fp::Smoother::Settings{.target = 0.2f * static_cast<f32>(l + 1), .time = 10.f + static_cast<f32>(l),
				                                  .block = static_cast<f32>(kBlock), .mode = mode};
			    },
			    3);
		}
	});
	it("applies a configure made between blocks at the next block", _ {
		expect_sum_equals_monos<fp::Svf, N>(self,
		    [](std::size_t l, std::size_t b) {
			    return fp::Svf::Settings{.cutoff = 0.01f * static_cast<f32>(1U << ((l + b) % 4)), .q = 2.f + static_cast<f32>(b),
			                             .mode = fp::Svf::Mode::BAND_PASS};
		    },
		    4);
	});
	it("reads back each lane's state, and resets one lane alone", _ {
		fp::FlushToZero ftz;
		const auto in = input();
		Bank<fp::Svf, N, mix::Sum> bank;
		std::array<Mono<fp::Svf>, N> mono{};
		std::vector<f32> out(in.size()), sink(in.size());
		for (std::size_t l = 0; l < N; ++l) {
			bank.configure(l, band(l));
			mono[l].configure(band(l));
			mono[l].render_block(fp::Signal{in}, fp::Buffer{sink});
		}
		bank.render_block(fp::Signal{in}, fp::Buffer{out});
		for (std::size_t l = 0; l < N; ++l) {
			expect(same_bits(bank.state(l), mono[l].state())).to_be_true();
		}
		expect(same_bits(bank.state(1), fp::Svf::State{})).to_be_false();
		bank.reset(1);
		mono[1].reset();
		for (std::size_t l = 0; l < N; ++l) {
			expect(same_bits(bank.state(l), mono[l].state())).to_be_true();
		}
		expect(same_bits(bank.state(1), fp::Svf::State{})).to_be_true();
	});
	// clang-format on
}

} // namespace

// clang-format off
describe bank_spec("Bank<K, N, Mix>", ${
	context("at N = 2", _ { bank_specs<2>(self); });
	context("at N = 4", _ { bank_specs<4>(self); });
});
// clang-format on

CPPSPEC_MAIN(bank_spec);
