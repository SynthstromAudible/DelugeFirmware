#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace deluge::plaits_control {
constexpr int roundedSteps(int32_t value, int shift) {
	const int64_t magnitude = value < 0 ? -int64_t(value) : int64_t(value);
	const int steps = (magnitude + (int64_t{1} << (shift - 1))) >> shift;
	return value < 0 ? -steps : steps;
}
inline int modelFromFinal(int base, int32_t value) {
	return std::clamp(base + roundedSteps(value, 29), 0, 2);
}
// A narrow dead band prevents noisy modulation repeatedly reinitializing a model.
// Discrete 1/2/3 selections remain far outside it and switch immediately.
inline int stableModelFromFinal(int base, int32_t value, int current) {
	const int candidate = modelFromFinal(base, value);
	constexpr int64_t step = int64_t{1} << 29;
	constexpr int64_t margin = step / 1024;
	const int64_t center = int64_t(current - base) * step;
	if (candidate > current && int64_t(value) < center + step / 2 + margin)
		return current;
	if (candidate < current && int64_t(value) > center - step / 2 - margin)
		return current;
	return candidate;
}
inline int modelFromPreset(int base, int32_t value) {
	return std::clamp(base + roundedSteps(value, 30), 0, 2);
}
inline int32_t presetForModel(int base, int model) {
	return std::clamp<int64_t>((model - base) * 1073741824LL, INT32_MIN, INT32_MAX);
}
} // namespace deluge::plaits_control
