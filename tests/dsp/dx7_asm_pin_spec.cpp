// The device's NEON FM kernel (a test-local copy, reference_neon_fm_kernel.s), pinned under qemu over every padding
// form, gain ramp and phase run: the oracle the Argon operator must equal.
#include "dx7_asm_pin_records.h"
#include "dx7_asm_pin_workloads.h"
#include "reference_dx7.h"
#include <algorithm>
#include <cppspec.hpp>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace dp = dx7_asm_pin;

// clang-format off
describe dx7_asm_pin_spec("the NEON FM kernel, pinned", ${
	static dx7_reference::Tables tables;
	dx7_reference::init_tables(tables);
	for (std::size_t i = 0; i < dp::all_cases().size(); ++i) {
		const dp::PinCase& pin = dp::all_cases()[i];
		it(pin.name, _ {
			alignas(16) static int32_t zeros[144] = {};
			dp::Inputs inputs;
			dp::fill_inputs(pin, inputs);
			alignas(16) int32_t out[144];
			std::copy(std::begin(inputs.accumulator), std::end(inputs.accumulator), out);
			const dp::Gains g = dp::modern_gains(tables, pin.gain_out, pin.level_in, pin.n);
			reference_neon_fm_kernel(pin.modulated ? inputs.modulation : zeros, pin.add ? out : zeros, out,
			                         dp::simd_n(pin.n), pin.phase0, pin.freq, g.gain1, g.dgain);
			// past what the asm writes, the buffer is untouched
			std::size_t touched = 0;
			for (int k = dp::asm_writes(pin.n); k < 144; ++k) {
				touched += out[k] != inputs.accumulator[k] ? 1 : 0;
			}
			expect(touched).to_equal(std::size_t{0});
			const uint64_t d = dp::digest_first_n(out, pin.n);
			if (std::getenv("DELUGE_DX7_RECORD") != nullptr) {
				std::printf("    PIN 0x%llxULL, // %s\n", (unsigned long long)d, pin.name);
			}
			expect(d).to_equal(dp::kRecordedAsm.at(i));
		});
	}
	it("is not the scalar operator: lane 0 starts at gain1, the sine is a polynomial", _ {
		const dp::PinCase pin{"pure_set_n12_rising", false, false, 12, 0, 0x00123456, 0, 16 << 24, 1};
		const dp::Gains g = dp::modern_gains(tables, pin.gain_out, pin.level_in, pin.n);
		alignas(16) static int32_t zeros[144] = {};
		alignas(16) int32_t asm_out[144] = {};
		int32_t scalar_out[144] = {};
		reference_neon_fm_kernel(zeros, zeros, asm_out, dp::simd_n(pin.n), pin.phase0, pin.freq, g.gain1, g.dgain);
		dx7_reference::compute_pure(tables, scalar_out, pin.n, pin.phase0, pin.freq, g.gain1, g.gain2, g.dgain, false);
		expect(std::equal(asm_out, asm_out + pin.n, scalar_out)).to_be_false();
	});
});
// clang-format on

CPPSPEC_MAIN(dx7_asm_pin_spec);
