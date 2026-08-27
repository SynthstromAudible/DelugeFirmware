#pragma once
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

/// Internal to `ui/oled`; not part of any widget's interface. Widgets that have to render a
/// number all need the same legacy conversion, and this is the one copy of it.
namespace deluge::ui::oled::detail {

/// `intToString(number, buffer, minNumDigits)` (foundation/string_format.c:35-65), as a view
/// into \p buf: the decimal digits of |number| zero-padded to at least \p min_digits, with a
/// leading `-` for negatives that does *not* count towards \p min_digits.
///
/// \p min_digits of 1 is `deluge::string::appendInt(out, number)` (util/string.cpp:103-107),
/// which is what every "just print the number" call site wants.
///
/// The magnitude is taken as `0u - (uint32_t)number` for the same reason legacy spells it that
/// way (string_format.c:39-40): `-number` is undefined for INT32_MIN.
inline std::string_view int_to_string(int32_t number, int32_t min_digits, std::span<char> buf) {
	const uint32_t magnitude =
	    number < 0 ? 0u - static_cast<uint32_t>(number) : static_cast<uint32_t>(number); // string_format.c:39-40
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
