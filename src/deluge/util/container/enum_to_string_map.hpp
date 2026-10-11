
#pragma once

#include "definitions.h"
#include "io/debug/log.h"
#include "mem_functions.h"
#include <array>
#include <cstddef>
#include <cstring>
#include <utility>
template <class Enum, std::size_t N>
class EnumStringMap {

public:
	constexpr EnumStringMap(const std::array<std::pair<Enum, const char*>, N>& init) : stringList_() {
		for (std::size_t i = 0; i < N; ++i) {
			const auto& p = init[i];
			const auto index = static_cast<std::underlying_type_t<Enum>>(p.first);
			stringList_[index] = p.second;
		}
		static_assert(std::is_enum_v<Enum>, "EnumStringMap requires an enum type");
		static_assert(N > 0, "EnumStringMap requires at least one enum value");
		static_assert(N == std::size(init),
		              "EnumStringMap requires an enum type with the same number of values as the initializer list");
	}

	constexpr const char* operator()(Enum a) { return stringList_[static_cast<std::underlying_type_t<Enum>>(a)]; }

	/// Convert string to enum, returning fallback when the string is not mapped.
	constexpr Enum operator()(const char* str, Enum fallback) {
		for (std::size_t i = 0; i < N; i++) {
			if (stringList_[i] != nullptr && !strcmp(str, stringList_[i])) {
				return static_cast<Enum>(i);
			}
		}

		char popup[25];
		D_PRINTLN(popup, "no match for:%s", str);

		return fallback;
	}

	/// Convert string to enum, returning the last mapped enum variant on failure.
	constexpr Enum operator()(const char* str) { return (*this)(str, static_cast<Enum>(N - 1)); }

private:
	const char* stringList_[N];
};
