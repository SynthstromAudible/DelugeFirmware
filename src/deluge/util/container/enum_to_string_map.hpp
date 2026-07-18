
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

	/// Convert enum to string, returning the last mapped string if the value has no entry
	constexpr const char* operator()(Enum a) {
		auto index = static_cast<std::size_t>(static_cast<std::underlying_type_t<Enum>>(a));
		if (index >= N) {
			index = N - 1;
		}
		return stringList_[index];
	}

	/// Convert string to enum, returning enum variant N on failure
	constexpr Enum operator()(const char* str) {
		for (int i = 0; i < N; i++) {
			if (!strcmp(str, stringList_[i])) {
				return static_cast<Enum>(i);
			}
		}

		char popup[25];
		D_PRINTLN(popup, "no match for:%s", str);

		return static_cast<Enum>(N - 1);
	}

private:
	const char* stringList_[N];
};
