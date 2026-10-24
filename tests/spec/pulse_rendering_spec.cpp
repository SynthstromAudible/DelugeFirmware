#include "cppspec.hpp"
#include "processing/vector_rendering_function.h"

#include <cstdint>
#include <vector>

namespace {
// Scalar reference for the original pulse renderer. The test table stays below
// saturation, so the NEON multiply/add operations reduce to ordinary arithmetic.
int32_t referencePulse(uint32_t phase, uint32_t offset, const int16_t* table, int magnitude) {
	auto interpolate = [&](uint32_t position, bool inverted) -> int64_t {
		uint32_t index = position >> (32 - magnitude);
		int32_t fraction = (position >> (32 - magnitude - 16)) & 0x7FFF;
		if (inverted) {
			return 2LL * ((fraction - 32768LL) * table[index] - int64_t(fraction) * table[index + 1]);
		}
		return 2LL * ((32767LL - fraction) * table[index] + int64_t(fraction) * table[index + 1]);
	};
	int64_t product = interpolate(phase, true) * interpolate(phase + offset, false);
	return static_cast<int32_t>(((product + (1LL << 30)) >> 31) * 2);
}
} // namespace

// clang-format off
describe pulse_rendering("Pulse rendering", $ {
	it("matches the original interpolation across table sizes and phase wraparound", _ {
		for (int magnitude = 6; magnitude <= 13; ++magnitude) {
			std::vector<int16_t> table((1U << magnitude) + 1);
			for (size_t i = 0; i + 1 < table.size(); ++i) {
				table[i] = static_cast<int16_t>(static_cast<int32_t>((i * 7919U) % 20001U) - 10000);
			}
			table.back() = table.front();
			for (uint32_t phase : {0U, 0x12345678U, 0xFFFF1234U}) {
				for (uint32_t increment : {0x12345U, 0x1020304U}) {
					for (uint32_t offset : {0U, 0x13579BDFU, 0xF0001234U}) {
						auto [output, nextPhase] = waveRenderingFunctionPulse(
						    phase, increment, offset, table.data(), magnitude);
						auto actual = output.to_array();
						for (uint32_t lane = 0; lane < 4; ++lane) {
							expect(actual[lane]).to_equal(referencePulse(
							    phase + increment * (lane + 1), offset, table.data(), magnitude));
						}
						expect(nextPhase).to_equal(phase + increment * 4);
					}
				}
			}
		}
	});
});

CPPSPEC_SPEC(pulse_rendering)
