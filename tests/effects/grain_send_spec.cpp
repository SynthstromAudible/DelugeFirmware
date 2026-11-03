#include "effects/chain_core.h"

#include <cppspec.hpp>
#include <cstdint>

using deluge::effects::grain_send_amount;

namespace {
constexpr int32_t kUnity = 1 << 27; ///< The post-FX volume's unity, Q27.
constexpr int32_t kHalf = 1 << 30;  ///< Half the send, Q31.
} // namespace

// clang-format off
describe grain_send("the granular's reverb send", ${
	it("is the Q27 volume times the Q31 send, in Q31", _ {
		expect(grain_send_amount(kUnity, kHalf)).to_equal(kHalf);
		expect(grain_send_amount(kUnity >> 1, kHalf)).to_equal(kHalf >> 1);
		expect(grain_send_amount(0, kHalf)).to_equal(0);
	});
	it("holds at full scale where volume × send reaches 1.0, rather than wrapping", _ {
		expect(grain_send_amount(2 * kUnity, kHalf)).to_equal(INT32_MAX);
		expect(grain_send_amount(INT32_MAX, INT32_MAX)).to_equal(INT32_MAX);
	});
	it("is exact just under the edge", _ {
		// 2^27 × (2^31 − 1) / 2^32 floors to 2^26 − 1, which is 2^31 − 32 in Q31
		expect(grain_send_amount(kUnity, INT32_MAX)).to_equal(INT32_MAX - 31);
	});
});
// clang-format on

CPPSPEC_SPEC(grain_send)
