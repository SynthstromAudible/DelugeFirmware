#pragma once
#include <limits>

namespace util {

template <typename T, std::size_t n>
constexpr T bit = T{1} << n;

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