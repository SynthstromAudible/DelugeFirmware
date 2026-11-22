#pragma once

#include <algorithm>
#include <cstdint>

namespace deluge::project {

/// @brief A [0, 1] fraction as the Q31 a song file holds, saturating at the largest Q31.
/// @param fraction The fraction.
/// @return Its Q31.
[[nodiscard]] inline int32_t q31_of(float fraction) {
	const uint32_t q = static_cast<uint32_t>(fraction * 2147483648.0f);
	return static_cast<int32_t>(std::min(q, uint32_t{2147483647}));
}

/// @brief A file's Q31 as the [0, 1] fraction a record holds.
/// @param q31 The Q31.
/// @return Its fraction.
[[nodiscard]] inline float fraction_of(int32_t q31) {
	return static_cast<float>(q31) / 2147483648.0f;
}

} // namespace deluge::project
