#include "scenes/bind/param_display.h"

#include <limits>

namespace deluge::scenes::bind {

namespace {

// Legacy's menu bounds (definitions_cxx.hpp's kMaxMenuValue, kMidMenuValue, kMaxMenuRelativeValue).
constexpr int32_t kMaxMenu = 50;
constexpr int32_t kMidMenu = 25;
constexpr int32_t kMaxRelative = 25;
constexpr int32_t kMinWord = std::numeric_limits<int32_t>::min();
constexpr int32_t kMaxWord = std::numeric_limits<int32_t>::max();

} // namespace

int32_t menu_of(engine::ParamDisplay display, int32_t stored) {
	switch (display) {
	case engine::ParamDisplay::HALF_PRECISION: // computeCurrentValueForHalfPrecisionMenuItem
		return static_cast<int32_t>(((int64_t)stored * (kMaxMenu * 2) + 2147483648) >> 32);
	case engine::ParamDisplay::PAN: // computeCurrentValueForPan
		return static_cast<int32_t>(((int64_t)stored * (kMaxRelative * 2) + 2147483648) >> 32);
	case engine::ParamDisplay::STANDARD: // computeCurrentValueForStandardMenuItem
		break;
	}
	return static_cast<int32_t>((((int64_t)stored + 2147483648) * kMaxMenu + 2147483648) >> 32);
}

int32_t stored_of(engine::ParamDisplay display, int32_t menu) {
	switch (display) {
	case engine::ParamDisplay::HALF_PRECISION: // computeFinalValueForHalfPrecisionMenuItem: comp params and a pulse
	                                           // width take no negative word, so the floor is 0
		if (menu == kMaxMenu) {
			return kMaxWord;
		}
		if (menu == 0) {
			return 0;
		}
		return static_cast<int32_t>((uint32_t)menu * (2147483648 / kMidMenu) >> 1);
	case engine::ParamDisplay::PAN: // computeFinalValueForPan
		if (menu == kMaxRelative) {
			return kMaxWord;
		}
		if (menu == -kMaxRelative) {
			return kMinWord;
		}
		return menu * static_cast<int32_t>(2147483648 / (kMaxRelative * 2)) * 2;
	case engine::ParamDisplay::STANDARD: // computeFinalValueForStandardMenuItem
		break;
	}
	if (menu == kMaxMenu) {
		return kMaxWord;
	}
	if (menu == 0) {
		return kMinWord;
	}
	return static_cast<int32_t>((uint32_t)menu * 85899345 - 2147483648); // 2147483648 / kMidMenu
}

std::pair<int32_t, int32_t> menu_range(engine::ParamDisplay display) {
	return display == engine::ParamDisplay::PAN ? std::pair{-kMaxRelative, kMaxRelative} : std::pair{0, kMaxMenu};
}

} // namespace deluge::scenes::bind
