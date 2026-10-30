// dx7::ModernVectorOperator against the device's NEON kernel: every asm pin case (both inputs, set and add, fifteen
// lengths, five gain sets, four phase sets) equals the pinned asm's digest on every architecture, and on ARM equals
// the asm itself sample by sample. Then the held state against the scalar operator, vcvt's conversion rule and the
// polynomial's constants.

#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/dx7/modern_vector_operator.h"
#include "dsp-next/fixed/dx7/operator.h"
#include "dsp-next/fixed/dx7/tables.h"
#include "dx7_asm_pin_records.h"
#include "dx7_asm_pin_workloads.h"
#include "reference_dx7.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace dx7 = deluge::dsp::fixed::dx7;
namespace dp = dx7_asm_pin;

namespace {

#if DELUGE_DX7_ASM_PIN
/// @brief The reference's tables, built once: dp::modern_gains takes them.
const dx7_reference::Tables& tables_as_reference() {
	static const dx7_reference::Tables t = [] {
		dx7_reference::Tables r{};
		dx7_reference::init_tables(r);
		return r;
	}();
	return t;
}
#endif

} // namespace

// clang-format off
describe dx7_modern_vector_operator_spec("dx7::ModernVectorOperator", ${
	static dx7::Tables tables;
	dx7::generate(tables);

	for (std::size_t i = 0; i < dp::all_cases().size(); ++i) {
		const dp::PinCase& pin = dp::all_cases()[i];
		it(pin.name, _ {
			static dp::Inputs inputs;
			dp::fill_inputs(pin, inputs);
			alignas(16) std::array<int32_t, 144> out{};
			std::copy(std::begin(inputs.accumulator), std::end(inputs.accumulator), out.begin());
			deluge::dsp::Mono<dx7::ModernVectorOperator> op{{.input = pin.modulated ? dx7::Input::BUS : dx7::Input::NONE,
			                                                .output = pin.add ? dx7::Output::ADD : dx7::Output::SET,
			                                                .level_in = pin.level_in, .freq = pin.freq,
			                                                .tables = &tables}};
			op.on<&dx7::ModernVectorOperator::resume>(dx7::OperatorState{.phase = pin.phase0, .gain_out = pin.gain_out});
			const auto n = static_cast<std::size_t>(pin.n);
			op.render_block({inputs.modulation, n}, {out.data(), n}, {out.data(), n});
			bool untouched = true; // writes only n
			for (std::size_t k = n; k < out.size(); ++k) {
				untouched = untouched && out[k] == inputs.accumulator[k];
			}
			expect(untouched).to_be_true();
			expect(dp::digest_first_n(out, pin.n)).to_equal(dp::kRecordedAsm.at(i));
#if DELUGE_DX7_ASM_PIN
			// on ARM, beside the asm itself, sample by sample, for a readable first difference
			alignas(16) static int32_t zeros[144] = {};
			alignas(16) std::array<int32_t, 144> asm_out{};
			std::copy(std::begin(inputs.accumulator), std::end(inputs.accumulator), asm_out.begin());
			const dp::Gains g = dp::modern_gains(tables_as_reference(), pin.gain_out, pin.level_in, pin.n);
			reference_neon_fm_kernel(pin.modulated ? inputs.modulation : zeros, pin.add ? asm_out.data() : zeros,
			                         asm_out.data(), dp::simd_n(pin.n), pin.phase0, pin.freq, g.gain1, g.dgain);
			for (std::size_t k = 0; k < n; ++k) {
				expect(out[k]).to_equal(asm_out[k]);
			}
#endif
		});
	}

	it("moves the held state as the scalar operator does", _ {
		for (const dp::PinCase& pin : dp::all_cases()) {
			const auto n = static_cast<std::size_t>(pin.n);
			deluge::dsp::Mono<dx7::Operator> a{{.level_in = pin.level_in, .freq = pin.freq, .tables = &tables}};
			a.on<&dx7::Operator::resume>(dx7::OperatorState{.phase = pin.phase0, .gain_out = pin.gain_out},
			                             dx7::FeedbackHistory{});
			deluge::dsp::Mono<dx7::ModernVectorOperator> b{{.level_in = pin.level_in, .freq = pin.freq, .tables = &tables}};
			b.on<&dx7::ModernVectorOperator::resume>(dx7::OperatorState{.phase = pin.phase0, .gain_out = pin.gain_out});
			alignas(16) std::array<int32_t, 144> scratch{};
			a.render_block({scratch.data(), n}, {scratch.data(), n}, {scratch.data(), n});
			b.render_block({scratch.data(), n}, {scratch.data(), n}, {scratch.data(), n});
			expect(b.state().op.phase).to_equal(a.state().op.phase);
			expect(b.state().op.gain_out).to_equal(a.state().op.gain_out);
		}
	});

	it("converts float to int as vcvt does: truncating, saturating, NaN to 0", _ {
		// × 2^24 then truncate toward zero; 2^32 saturates; NaN converts to 0
		const std::array<float, 4> in{-1.5F / (1 << 24), 1.75F / (1 << 24), 256.0F,
		                              std::numeric_limits<float>::quiet_NaN()};
		const std::array<int32_t, 4> want{-1, 1, INT32_MAX, 0};
		std::array<int32_t, 4> got{};
		Argon<float>::Load(in.data()).ConvertTo<int32_t, 24>().StoreTo(got.data());
		for (std::size_t k = 0; k < 4; ++k) {
			expect(got[k]).to_equal(want[k]);
		}
		const std::array<float, 4> negative{-256.0F, -0.5F / (1 << 24), 0.0F, -0.0F};
		const std::array<int32_t, 4> want_negative{INT32_MIN, 0, 0, 0};
		Argon<float>::Load(negative.data()).ConvertTo<int32_t, 24>().StoreTo(got.data());
		for (std::size_t k = 0; k < 4; ++k) {
			expect(got[k]).to_equal(want_negative[k]);
		}
	});

	it("keeps the polynomial's constants bit for bit", _ {
		expect(std::bit_cast<uint32_t>(dx7::ModernVectorOperator::kPoly0)).to_equal(0xbc9a1459U);
		expect(std::bit_cast<uint32_t>(dx7::ModernVectorOperator::kPoly1)).to_equal(0x3e811a23U);
		expect(std::bit_cast<uint32_t>(dx7::ModernVectorOperator::kPoly2)).to_equal(0xbf9dde37U);
		expect(std::bit_cast<uint32_t>(dx7::ModernVectorOperator::kPoly3)).to_equal(0x3f800000U);
	});
});
// clang-format on

CPPSPEC_MAIN(dx7_modern_vector_operator_spec);
