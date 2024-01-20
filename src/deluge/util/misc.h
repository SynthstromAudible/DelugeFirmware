#pragma once
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace util {
template <class Enum>
concept enumeration = std::is_enum_v<Enum>;

template <enumeration Enum>
constexpr std::underlying_type_t<Enum> to_underlying(Enum e) noexcept {
	return static_cast<std::underlying_type_t<Enum>>(e);
}

/**
 * @brief The nth bit
 */
template <std::size_t n, typename T = int32_t>
constexpr T bit = T{1} << n;

/**
 * @brief Generate a value with a given bitwidth where the topmost bit is set
 *
 * @tparam width The number of bits in the value
 * @tparam T The output type
 */
template <std::size_t width, typename T = int32_t>
constexpr T bit_value = bit<width - 1, T>;

/**
 * @brief Generate a fully positive bitmask of n bits
 *
 * @tparam width the number of bits in the bitmask
 * @tparam T The output type
 */
template <std::size_t width, typename T = int32_t>
constexpr T bitmask = bit<width, T> - 1;

template <typename T>
constexpr T bit_from_top(const std::size_t n) {
	return T{1} << (std::numeric_limits<T>::digits - (n + 1));
}

template <typename T>
constexpr T top_n_bits(const std::size_t n) {
	T output{0};
	for (std::size_t i = 0; i < n; i++) {
		output |= bit_from_top<T>(i);
	}
	return output;
}

template <typename T>
constexpr T top_bit = T{1} << (std::numeric_limits<T>::digits - 1);

template <std::integral T>
constexpr T median_value = (std::numeric_limits<T>::max() / 2) + (std::numeric_limits<T>::min() / 2) + 1;

template <typename T>
constexpr T set_top_bit(const T v) {
	return v | top_bit<T>;
}

template <typename T, std::size_t n>
constexpr T set_top_n_bits(const T v) {
	T output = v;
	for (std::size_t i = 0; i < n; i++) {
		output |= bit_from_top<T>(i);
	}
	return output;
}
} // namespace util

// unsigned literal operators
consteval uint_least64_t operator"" _u64(unsigned long long arg) {
	return arg;
};
consteval uint_least32_t operator"" _u32(unsigned long long arg) {
	return arg;
};
consteval uint_least16_t operator"" _u16(unsigned long long arg) {
	return arg;
};
consteval uint_least8_t operator"" _u8(unsigned long long arg) {
	return arg;
};

// signed literal operators
consteval int_least64_t operator"" _i64(unsigned long long arg) {
	return arg;
};
consteval int_least32_t operator"" _i32(unsigned long long arg) {
	return arg;
};
consteval int_least16_t operator"" _i16(unsigned long long arg) {
	return arg;
};
consteval int_least8_t operator"" _i8(unsigned long long arg) {
	return arg;
};
