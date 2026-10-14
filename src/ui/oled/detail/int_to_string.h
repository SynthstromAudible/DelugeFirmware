#pragma once
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

/// @brief Internal to `ui/oled`; not part of any widget's interface.
///
/// Widgets that have to render a number all need the same legacy conversion, and this is the one
/// copy of it.
namespace deluge::ui::oled::detail {

/// @brief Render \p number as decimal digits into \p buf, zero-padded to at least \p min_digits.
///
/// The decimal digits of |number|, zero-padded to at least \p min_digits, with a leading `-` for
/// negatives that does *not* count towards \p min_digits. \p min_digits of 1 is equivalent to
/// `deluge::string::appendInt(out, number)`, which is what every "just print the number" call
/// site wants.
///
/// @note The magnitude is computed as `0u - (uint32_t)number` rather than `-number`, because
///       negating `INT32_MIN` is undefined behaviour.
///
/// @param number     The value to render.
/// @param min_digits Minimum digit count to zero-pad the magnitude to (a leading `-` is not
///                    counted).
/// @param buf        Destination buffer. If it is too small, the output is silently truncated to
///                    fit.
/// @return A view into \p buf holding the rendered digits.
inline std::string_view int_to_string(int32_t number, int32_t min_digits, std::span<char> buf) {
	const uint32_t magnitude = number < 0 ? 0u - static_cast<uint32_t>(number) : static_cast<uint32_t>(number);
	std::array<char, 10> digits{}; // uint32_t is 10 digits at most
	const std::to_chars_result r = std::to_chars(digits.data(), digits.data() + digits.size(), magnitude);
	const int32_t n = static_cast<int32_t>(r.ptr - digits.data());

	size_t out = 0;
	const auto push = [&](char c) {
		if (out < buf.size()) {
			buf[out++] = c;
		}
	};
	if (number < 0) {
		push('-');
	}
	for (int32_t pad = n; pad < min_digits; pad++) {
		push('0');
	}
	for (int32_t i = 0; i < n; i++) {
		push(digits[static_cast<size_t>(i)]);
	}
	return std::string_view{buf.data(), out};
}

} // namespace deluge::ui::oled::detail
