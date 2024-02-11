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
			stringList_[static_cast<std::underlying_type_t<Enum>>(p.first)] = p.second;
		}
	}

	constexpr const char* operator()(Enum a) { return stringList_[static_cast<std::underlying_type_t<Enum>>(a)]; }

	/// Convert string to enum, returning enum variant N on failure
	constexpr Enum operator()(const char* str) {
		for (int i = 0; i < N; i++) {
			if (!strcmp(str, stringList_[i])) {
				return static_cast<Enum>(i);
			}
		}

		char popup[25];
		D_PRINTLN(popup, "no match for:%s", str);

		return static_cast<Enum>(N);
	}

private:
	const char* stringList_[N];
};
