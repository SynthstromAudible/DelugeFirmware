#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace deluge::plaits_control {
inline int modelFromFinal(int base, int32_t value) {
	return std::clamp(base + int(std::lround(value / 536870912.0)), 0, 2);
}
inline int modelFromPreset(int base, int32_t value) {
	return std::clamp(base + int(std::lround(value / 1073741824.0)), 0, 2);
}
inline int32_t presetForModel(int base, int model) {
	return std::clamp<int64_t>((model - base) * 1073741824LL, INT32_MIN, INT32_MAX);
}
} // namespace deluge::plaits_control
