#pragma once

/// @file
/// @brief A note's probability and fill condition, as the note menus edit them.

#include <cstdint>

namespace deluge::project {

/// @brief When a note plays relative to the FILL button.
enum class FillMode : uint8_t {
	OFF,      ///< regardless of FILL
	NOT_FILL, ///< only while FILL is not held
	FILL,     ///< only while FILL is held
};

/// How many fill conditions a note's probability can name: `NOT_FILL` and `FILL`.
inline constexpr int32_t kFillModeCount = 2;

/// How many probability steps a note has, 5% each.
inline constexpr int32_t kProbabilityValueCount = 20;

} // namespace deluge::project
