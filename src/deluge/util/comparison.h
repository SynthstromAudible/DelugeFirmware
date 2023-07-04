#pragma once
#include <initializer_list>

namespace util {

/**
 * @brief Check if the first paramater is one of the values in the second parameter
 *
 * @param value The value to check
 * @param comparators A list of values to check against
 * @return true value == a comparator
 * @return false value != any comparator
 */
template <typename T>
constexpr bool one_of(T value, std::initializer_list<T> comparators) {
	for (auto& comparator : comparators) {
		if (value == comparator) {
			return true;
		}
	}
	return false;
}

/**
 * @brief (Variadic) Check if the first paramater is one of the values in the second parameter
 *
 * @param value The value to check
 * @param comparators A list of values to check against
 * @return true value == a comparator
 * @return false value != any comparator
 */
template <typename T, typename... Ts>
constexpr bool one_of(T value, Ts... comparators) {
	for (auto& comparator : {comparators...}) {
		if (value == comparator) {
			return true;
		}
	}
	return false;
}

} // namespace util
