// Reference copies of the q31 param curves (getFinalParameterValueHybrid/Volume/Linear/Exp, lookupReleaseRate and
// releaseRateTable64, without their commented-out code), kept verbatim as the oracle for
// dsp-next/fixed/curves/param_curve.h.
#pragma once

#include "dsp-next/core/q31_ops.h"
#include "dsp-next/fixed/curves/exp_curve.h"

#include <cstdint>

namespace param_curve_reference {

// clang-format off
alignas(16) inline const int32_t releaseRateTable64[65] = {
1959518848, 240577040, 126456408, 84972672, 63518640, 50408868, 41567836, 35202512,
30400724, 26649400, 23637820, 21166812, 19102814, 17352918, 15850494, 14546511,
13404089, 12394955, 11497068, 10692993, 9968758, 9313033, 8716538, 8171592,
7671792, 7211749, 6786901, 6393359, 6027784, 5687299, 5369406, 5071928,
4792960, 4530827, 4284050, 4051316, 3831462, 3623446, 3426337, 3239300,
3061582, 2892503, 2731448, 2577859, 2431229, 2291096, 2157036, 2028664,
1905625, 1787593, 1674270, 1565377, 1460662, 1359888, 1262836, 1169305,
1079105, 992062, 908014, 826807, 748301, 672363, 598870, 527703,
458757,
};
// clang-format on

inline int32_t getFinalParameterValueHybrid(int32_t paramNeutralValue, int32_t patchedValue) {
	int32_t preLimits = (paramNeutralValue >> 2) + (patchedValue >> 1);
	return signed_saturate<32 - 3>(preLimits) << 2;
}

inline int32_t getFinalParameterValueVolume(int32_t paramNeutralValue, int32_t patchedValue) {
	int32_t positivePatchedValue = patchedValue + 536870912;
	positivePatchedValue = (positivePatchedValue >> 16) * (positivePatchedValue >> 15);
	return lshift_and_saturate<5>(multiply_32x32_rshift32(positivePatchedValue, paramNeutralValue));
}

inline int32_t getFinalParameterValueLinear(int32_t paramNeutralValue, int32_t patchedValue) {
	int32_t positivePatchedValue = patchedValue + 536870912;
	return lshift_and_saturate<3>(multiply_32x32_rshift32(positivePatchedValue, paramNeutralValue));
}

inline int32_t getFinalParameterValueExp(int32_t paramNeutralValue, int32_t patchedValue) {
	return deluge::dsp::fixed::exp_curve(paramNeutralValue, patchedValue);
}

inline int32_t lookupReleaseRate(int32_t input) {
	int32_t magnitude = 24;
	int32_t whichValue = input >> magnitude;                           // 25
	int32_t howMuchFurther = (input << (31 - magnitude)) & 2147483647; // 6
	whichValue += 32;                                                  // Put it in the range 0 to 64
	if (whichValue < 0) {
		return releaseRateTable64[0];
	}
	else if (whichValue >= 64) {
		return releaseRateTable64[64];
	}
	int32_t value1 = releaseRateTable64[whichValue];
	int32_t value2 = releaseRateTable64[whichValue + 1];
	return (multiply_32x32_rshift32(value2, howMuchFurther)
	        + multiply_32x32_rshift32(value1, 2147483647 - howMuchFurther))
	       << 1;
}

} // namespace param_curve_reference
