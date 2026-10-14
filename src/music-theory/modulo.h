#pragma once

#include <cstdint>

namespace deluge::music::detail {

/// @brief @p a modulo @p b, non-negative for positive @p b (unlike `%`, which keeps @p a's sign). In `detail` so that
/// argument-dependent lookup on the library's types (e.g. Scale) never offers it beside the app's own mod().
[[nodiscard]] constexpr int32_t mod(int32_t a, int32_t b) noexcept {
	return ((a % b) + b) % b;
}

} // namespace deluge::music::detail
