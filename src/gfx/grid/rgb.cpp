#include "rgb.h"
#include "gfx/grid/sine_table.h"
#include <cstdlib>

using deluge::gfx::grid::sine_q15;

RGB RGB::fromHue(int32_t hue) {
	RGB rgb{};
	hue = (uint16_t)(hue + 1920) % 192;

	for (int32_t c = 0; c < rgb.size(); c++) {
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
			rgb[c] =
			    ((uint32_t)sine_q15(((channelDarkness << 3)
			                         + deluge::gfx::grid::detail::bit_value<9>)&deluge::gfx::grid::detail::bitmask<10>,
			                        10)
			     + deluge::gfx::grid::detail::median_value<uint32_t>)
			    >> 24;
		}
		else {
			rgb[c] = 0;
		}
	}
	return rgb;
}

constexpr int32_t kMaxPastel = 230;

RGB RGB::fromHuePastel(int32_t hue) {
	RGB rgb{};
	hue = (uint16_t)(hue + 1920) % 192;

	for (int32_t c = 0; c < rgb.size(); c++) {
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
			uint32_t basicValue =
			    (uint32_t)sine_q15(((channelDarkness << 3)
			                        + deluge::gfx::grid::detail::bit_value<9>)&deluge::gfx::grid::detail::bitmask<10>,
			                       10)
			    + deluge::gfx::grid::detail::median_value<uint32_t>; // Goes all the way up to max uint32_t
			uint32_t flipped = std::numeric_limits<uint32_t>::max() - basicValue;
			uint32_t flippedScaled = (flipped >> 8) * kMaxPastel;
			rgb[c] = (std::numeric_limits<uint32_t>::max() - flippedScaled) >> 24;
		}
		else {
			rgb[c] = 256 - kMaxPastel;
		}
	}
	return rgb;
}
