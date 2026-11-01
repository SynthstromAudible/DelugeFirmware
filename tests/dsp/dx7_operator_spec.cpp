// dx7::Operator through Mono, against the reference's scalar operator functions: every mode (two engines, three
// inputs, set or add) at every block length 1 to 132, the five gain sets and four phase sets of the asm pin, and
// three feedback shifts for the feedback modes. The samples, the held phase and gain, and the feedback history
// match; then the audibility gate and the skipped block's bookkeeping.

#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/dx7/graph.h"
#include "dsp-next/fixed/dx7/operator.h"
#include "dsp-next/fixed/dx7/tables.h"
#include "dx7_asm_pin_workloads.h"
#include "reference_dx7.h"
#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>

namespace dx7 = deluge::dsp::fixed::dx7;
using deluge::dsp::Mono;
using dx7::Engine;
using dx7::Input;
using dx7::Operator;
using dx7::Output;

namespace {

constexpr std::array<int32_t, 3> kFeedbackShifts{1, 4, 16};
constexpr dx7::FeedbackHistory kHistory{.y0 = 0x00123456, .y1 = -0x00234567};

/// @brief EngineMkI::render's gains for an op: the attenuation the last block ended at (0 read as just below
/// silence), the level's, and the per-sample step.
DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC dx7_asm_pin::Gains mki_gains(int32_t gain_out, int32_t level_in, int n) {
	const int inv_n = (1 << 30) / n;
	const int32_t gain1 = gain_out == 0 ? (16384 - 1) : gain_out;
	const int32_t gain2 = 16384 - (level_in >> (28 - 14));
	const int32_t dgain = (int)((((int64_t)(gain2 - gain1 + (n >> 1))) * (int64_t)(inv_n)) >> 30);
	return {gain1, gain2, dgain};
}

struct Reference {
	alignas(16) int32_t out[dx7::kMaxBlock]{}; // NOLINT(*-avoid-c-arrays)
	int32_t fb[2]{};                           // NOLINT(*-avoid-c-arrays)
	dx7_asm_pin::Gains gains{};
};

/// @brief The reference's block: its gains, then its scalar function for the mode, into @p r.
void run_reference(const dx7_reference::Tables& t, Engine engine, Input input, bool add, int n, int32_t phase0,
                   int32_t freq, int32_t gain_out, int32_t level_in, int32_t shift, const int32_t* modulation,
                   Reference& r) {
	r.gains = engine == Engine::MODERN ? dx7_asm_pin::modern_gains(t, gain_out, level_in, n)
	                                   : mki_gains(gain_out, level_in, n);
	const auto [g1, g2, dg] = r.gains;
	r.fb[0] = kHistory.y0;
	r.fb[1] = kHistory.y1;
	if (engine == Engine::MODERN) {
		switch (input) {
		case Input::NONE:
			dx7_reference::compute_pure(t, r.out, n, phase0, freq, g1, g2, dg, add);
			break;
		case Input::BUS:
			dx7_reference::compute(t, r.out, n, modulation, phase0, freq, g1, g2, dg, add);
			break;
		case Input::FEEDBACK:
			dx7_reference::compute_fb(t, r.out, n, phase0, freq, g1, g2, dg, r.fb, shift, add);
			break;
		}
	}
	else {
		switch (input) {
		case Input::NONE:
			dx7_reference::mki_compute_pure(t, r.out, n, phase0, freq, g1, g2, dg, add);
			break;
		case Input::BUS:
			dx7_reference::mki_compute(t, r.out, n, modulation, phase0, freq, g1, g2, dg, add);
			break;
		case Input::FEEDBACK:
			dx7_reference::mki_compute_fb(t, r.out, n, phase0, freq, g1, g2, dg, r.fb, shift, add);
			break;
		}
	}
}

} // namespace

// clang-format off
describe dx7_operator_spec("dx7::Operator", ${
	static dx7::Tables tables;
	static dx7_reference::Tables reference_tables;
	dx7::generate(tables);
	dx7_reference::init_tables(reference_tables);

	it("renders every mode as the reference at every block length, gain, phase and feedback shift", _ {
		static dx7_asm_pin::Inputs inputs;
		static Reference reference;
		alignas(16) static int32_t out[dx7::kMaxBlock];
		dx7_asm_pin::fill_inputs({.modulated = true, .seed = 7}, inputs);

		for (uint8_t m = 0; m < 12; ++m) {
			const auto mode = static_cast<Operator::Mode>(m);
			const Engine engine = Operator::engine_of(mode);
			const Input input = Operator::input_of(mode);
			const Output output = Operator::output_of(mode);
			const bool add = output == Output::ADD;
			const std::size_t shifts = input == Input::FEEDBACK ? kFeedbackShifts.size() : 1;
			for (int n = 1; n <= static_cast<int>(dx7::kMaxBlock); ++n) {
				for (const dx7_asm_pin::GainSet& g : dx7_asm_pin::gain_sets()) {
					for (const dx7_asm_pin::PhaseSet& p : dx7_asm_pin::kPhases) {
						for (std::size_t k = 0; k < shifts; ++k) {
							const int32_t shift = kFeedbackShifts[k];
							std::copy_n(inputs.accumulator, dx7::kMaxBlock, reference.out);
							std::copy_n(inputs.accumulator, dx7::kMaxBlock, out);
							run_reference(reference_tables, engine, input, add, n, p.phase0, p.freq, g.gain_out,
							              g.level_in, shift, inputs.modulation, reference);

							Mono<Operator> unit{{.engine = engine, .input = input, .output = output,
							                     .level_in = g.level_in, .freq = p.freq, .feedback_shift = shift,
							                     .tables = &tables}};
							unit.on<&Operator::resume>(dx7::OperatorState{.phase = p.phase0, .gain_out = g.gain_out},
							                           kHistory);
							const auto len = static_cast<std::size_t>(n);
							const int32_t* modulation = input == Input::BUS ? inputs.modulation : out;
							unit.render_block({modulation, len}, {out, len}, {out, len});

							expect(std::ranges::equal(std::span{out, len}, std::span{reference.out, len})).to_be_true();
							expect(unit.state().op.gain_out).to_equal(reference.gains.gain2);
							expect(unit.state().op.phase)
							    .to_equal(Operator::advance_phase(p.phase0, p.freq, len));
							if (input == Input::FEEDBACK) {
								expect(unit.state().loop.feedback.y0).to_equal(reference.fb[0]);
								expect(unit.state().loop.feedback.y1).to_equal(reference.fb[1]);
							}
						}
					}
				}
			}
		}
	});

	it("gates and skips a block as the reference's render loops do", _ {
		for (const Engine engine : {Engine::MODERN, Engine::MKI}) {
			for (int n = 1; n <= static_cast<int>(dx7::kMaxBlock); ++n) {
				for (const dx7_asm_pin::GainSet& g : dx7_asm_pin::gain_sets()) {
					const dx7_asm_pin::Gains r = engine == Engine::MODERN
					    ? dx7_asm_pin::modern_gains(reference_tables, g.gain_out, g.level_in, n)
					    : mki_gains(g.gain_out, g.level_in, n);
					const bool gate = engine == Engine::MODERN
					    ? (r.gain1 >= dx7::kGainLevelThresh || r.gain2 >= dx7::kGainLevelThresh)
					    : (r.gain1 <= dx7::kMkILevelThresh || r.gain2 <= dx7::kMkILevelThresh);
					const dx7::OperatorState held{.phase = 0x7fff0000, .gain_out = g.gain_out};
					Mono<Operator> unit{{.engine = engine, .level_in = g.level_in, .freq = 0x01000000,
					                     .tables = &tables}};
					const auto len = static_cast<std::size_t>(n);
					expect(Operator::audible(unit.coefficients(), held, len)).to_equal(gate);

					unit.on<&Operator::resume>(held, dx7::FeedbackHistory{});
					unit.on<&Operator::skip>(len);
					expect(unit.state().op.gain_out).to_equal(r.gain2);
					expect(unit.state().op.phase).to_equal(Operator::advance_phase(held.phase, 0x01000000, len));
				}
			}
		}
	});
});
// clang-format on

CPPSPEC_MAIN(dx7_operator_spec);
