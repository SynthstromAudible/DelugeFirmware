#include "colour.h"
#include "util/functions.h"

 Colour Colour::fromHue(int32_t hue){
	Colour rgb;
	hue = (uint16_t)(hue + 1920) % 192;

	for (int32_t c = 0; c < 3; c++) {
		int32_t channelDarkness;
		if (c == 0) {
			if (hue < 64) {
				channelDarkness = hue;
			}
			else {
				channelDarkness = std::min<int32_t>(64, std::abs(192 - hue));
			}
		}
		else {
			channelDarkness = std::min<int32_t>(64, std::abs(c * 64 - hue));
		}

		if (channelDarkness < 64) {
			rgb[c] = ((uint32_t)getSine(((channelDarkness << 3) + 256) & 1023, 10) + 2147483648u) >> 24;
		}
		else {
			rgb[c] = 0;
		}
	}
	return rgb;
}

constexpr int32_t kMaxPastel = 230;

Colour Colour::fromHuePastel(int32_t hue) {
	Colour rgb;
	hue = (uint16_t)(hue + 1920) % 192;

	for (int32_t c = 0; c < 3; c++) {
		int32_t channelDarkness;
		if (c == 0) {
			if (hue < 64) {
				channelDarkness = hue;
			}
			else {
				channelDarkness = std::min<int32_t>(64, std::abs(192 - hue));
			}
		}
		else {
			channelDarkness = std::min<int32_t>(64, std::abs(c * 64 - hue));
		}

		if (channelDarkness < 64) {
			uint32_t basicValue = (uint32_t)getSine(((channelDarkness << 3) + 256) & 1023, 10)
			                      + 2147483648u; // Goes all the way up to 4294967295
			uint32_t flipped = 4294967295 - basicValue;
			uint32_t flippedScaled = (flipped >> 8) * kMaxPastel;
			rgb[c] = (4294967295 - flippedScaled) >> 24;
		}
		else {
			rgb[c] = 256 - kMaxPastel;
		}
	}
	return rgb;
}
