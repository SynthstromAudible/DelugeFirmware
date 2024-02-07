#include "definitions.h"
#include "mem_functions.h"
#include <array>
#include <cstddef>
#include <cstring>
#include <utility>
template <class Enum, std::size_t N>
class EnumStringMap {
	const char* stringList[N];

public:
	constexpr EnumStringMap(const std::array<std::pair<Enum, const char*>, N>& init) : stringList() {
		for (std::size_t i = 0; i < N; ++i) {
			const auto& p = init[i];
			stringList[static_cast<std::underlying_type_t<Enum>>(p.first)] = p.second;
		}
	}

	constexpr const char* operator()(Enum a) { return stringList[static_cast<std::underlying_type_t<Enum>>(a)]; }

	constexpr Enum operator()(const char* str) {
		for (int i = 0; i < N; i++) {
			if (!strcmp(str, stringList[i])) {
				return static_cast<Enum>(i);
			}
		}
#if ALPHA_OR_BETA_VERSION
		freezeWithError("no match");
#endif
		return static_cast<Enum>(N);
	}
};
