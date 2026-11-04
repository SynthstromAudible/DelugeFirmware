#include "modulation/params/param.h"
#include "raw_curves.h"
#include "util/param_functions.h"

#include <cppspec.hpp>
#include <cstdint>
#include <limits>

namespace params = deluge::modulation::params;

namespace {
constexpr int32_t kNeutral = 4096; ///< An attack's neutral rate.
} // namespace

// clang-format off
describe attack_param("an envelope attack's final value", ${
	it("is the exponential of the negated combination: more patching, a faster attack", _ {
		expect(getFinalParameterValueExpWithDumbEnvelopeHack(kNeutral, -(1 << 28), params::LOCAL_ENV_0_ATTACK))
		    .to_equal(deluge::dsp::test::raw_curves::curve_exponential(kNeutral, 1 << 28));
	});
	it("is the fastest at the most negative combination, not the slowest", _ {
		// Four full-depth cables saturate the combination at INT32_MIN; its negation saturates at INT32_MAX.
		const int32_t fastest = deluge::dsp::test::raw_curves::curve_exponential(kNeutral, std::numeric_limits<int32_t>::max());
		expect(getFinalParameterValueExpWithDumbEnvelopeHack(kNeutral, std::numeric_limits<int32_t>::min(),
		                                                     params::LOCAL_ENV_0_ATTACK))
		    .to_equal(fastest);
		expect(getFinalParameterValueExpWithDumbEnvelopeHack(kNeutral, std::numeric_limits<int32_t>::min(),
		                                                     params::LOCAL_ENV_3_ATTACK))
		    .to_equal(fastest);
	});
});
// clang-format on

CPPSPEC_MAIN(attack_param);
