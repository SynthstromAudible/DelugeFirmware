// dx7::MkIFeedbackStack through Mono, against the reference's fused MkI stacks for algorithms 6 (two ops) and 4
// (three ops) plus the render loop's phase bookkeeping: op 6's five gain sets, five levels for the ops after it and
// eight feedback shifts, over consecutive blocks of every length. Then the held and falling attenuation quirk.

#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/dx7/graph.h"
#include "dsp-next/fixed/dx7/mki_feedback_stack.h"
#include "dsp-next/fixed/dx7/tables.h"
#include "dx7_asm_pin_workloads.h"
#include "dx7_workloads.h"
#include "reference_dx7.h"
#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dx7 = deluge::dsp::fixed::dx7;
using deluge::dsp::Mono;

namespace {

constexpr std::array<int32_t, 5> kLevels{0, 1 << 24, 14 << 24, 0x0fffffff, 0x10000000}; // the last attenuates to 0
constexpr std::array<int32_t, 8> kShifts{1, 2, 3, 4, 5, 6, 7, 16};
constexpr std::array<int32_t, 3> kFreqs{0x00123456, 0x01000000, 0x7fffffff};
constexpr std::array<int32_t, 3> kPhases{0x7fff0000, 0, 0x00ffffff};
constexpr dx7::FeedbackHistory kHistory{.y0 = 0x00123456, .y1 = -0x00234567};

/// @brief EngineMkI::render's gains for op 6.
DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC dx7_asm_pin::Gains mki_gains(int32_t gain_out, int32_t level_in, int n) {
	const int inv_n = (1 << 30) / n;
	const int32_t gain1 = gain_out == 0 ? (16384 - 1) : gain_out;
	const int32_t gain2 = 16384 - (level_in >> (28 - 14));
	const int32_t dgain = (int)((((int64_t)(gain2 - gain1 + (n >> 1))) * (int64_t)(inv_n)) >> 30);
	return {gain1, gain2, dgain};
}

/// @brief One block of the reference: op 6's gains and gain_out, the fused stack, then every op's phase.
template <std::size_t N>
DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC void reference_block(const dx7_reference::Tables& t, int32_t* out, int n,
                                                            dx7_reference::FmOpParams* params, int32_t* fb,
                                                            int32_t shift) {
	const dx7_asm_pin::Gains g = mki_gains(params[0].gain_out, params[0].level_in, n);
	params[0].gain_out = g.gain2;
	const int32_t stack_shift = std::min(shift + 2, int32_t{16});
	if constexpr (N == 3) {
		dx7_reference::mki_compute_fb3(t, out, n, params, g.gain1, g.gain2, g.dgain, fb, stack_shift);
	}
	else {
		dx7_reference::mki_compute_fb2(t, out, n, params, g.gain1, g.gain2, g.dgain, fb, stack_shift);
	}
	for (std::size_t k = 0; k < N; ++k) {
		params[k].phase += params[k].freq * n;
	}
}

} // namespace

// clang-format off
describe dx7_mki_feedback_stack_spec("dx7::MkIFeedbackStack", ${
	static dx7::Tables tables;
	static dx7_reference::Tables reference_tables;
	dx7::generate(tables);
	dx7_reference::init_tables(reference_tables);

	const auto check = [&]<std::size_t N>(auto& self) {
		alignas(16) static int32_t out[dx7::kMaxBlock];
		alignas(16) static int32_t reference_out[dx7::kMaxBlock];
		for (const dx7_asm_pin::GainSet& g : dx7_asm_pin::gain_sets()) {
			for (const int32_t level : kLevels) {
				for (const int32_t shift : kShifts) {
					std::array<dx7_reference::FmOpParams, N> params{};
					std::array<dx7::OperatorState, N> ops{};
					for (std::size_t k = 0; k < N; ++k) {
						params[k] = {.level_in = k == 0 ? g.level_in : level, .gain_out = k == 0 ? g.gain_out : 0,
						             .freq = kFreqs[k], .phase = kPhases[k]};
						ops[k] = {.phase = kPhases[k], .gain_out = params[k].gain_out};
					}
					int32_t fb[2] = {kHistory.y0, kHistory.y1}; // NOLINT(*-avoid-c-arrays): the reference's history

					typename dx7::MkIFeedbackStack<N>::Settings settings{.feedback_shift = shift, .tables = &tables};
					for (std::size_t k = 0; k < N; ++k) {
						settings.level_in[k] = params[k].level_in;
						settings.freq[k] = params[k].freq;
					}
					Mono<dx7::MkIFeedbackStack<N>> stack{settings};
					stack.template on<&dx7::MkIFeedbackStack<N>::resume>(ops, kHistory);

					for (const uint16_t n : dx7_workloads::block_lengths_1_to_132()) {
						reference_block<N>(reference_tables, reference_out, n, params.data(), fb, shift);
						stack.render_block({out, n});
						expect(std::ranges::equal(std::span{out, n}, std::span{reference_out, n})).to_be_true();
						for (std::size_t k = 0; k < N; ++k) {
							expect(stack.state().op[k].phase).to_equal(params[k].phase);
							expect(stack.state().op[k].gain_out).to_equal(params[k].gain_out);
						}
						expect(stack.state().loop.feedback.y0).to_equal(fb[0]);
						expect(stack.state().loop.feedback.y1).to_equal(fb[1]);
					}
				}
			}
		}
	};

	it("renders algorithm 6's two-op stack as the reference", _ { check.template operator()<2>(self); });
	it("renders algorithm 4's three-op stack as the reference", _ { check.template operator()<3>(self); });

	it("holds an op's new attenuation, or falls from silence when it is 0", _ {
		using Stack = dx7::MkIFeedbackStack<3>;
		const Stack::Coefficients c = Stack::coefficients(
		    {.level_in = {14 << 24, 0x10000000, 1 << 24}, .freq = {0, 0, 0}, .tables = &tables});
		Stack::State s{};
		const Stack::Block b = Stack::prepare(c, s, 64);
		expect(s.op[1].gain_out).to_equal(0);
		expect(s.loop.gain[1]).to_equal(dx7::kEnvMax - 1);
		expect(b.dgain[1]).to_equal(-(dx7::kEnvMax - 1));
		expect(s.op[2].gain_out).to_equal(dx7::kEnvMax - 1024);
		expect(s.loop.gain[2]).to_equal(dx7::kEnvMax - 1024);
		expect(b.dgain[2]).to_equal(0);
	});
});
// clang-format on

CPPSPEC_MAIN(dx7_mki_feedback_stack_spec);
