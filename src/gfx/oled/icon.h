#pragma once
#include <cstdint>

namespace deluge::gfx::oled {
/// 1-bpp column-major bitmap: `width` columns of `num_bytes_tall` bytes, bit 0 = top row.
struct Icon {
	const uint8_t* data;
	uint8_t width;
	uint8_t height;
	uint8_t num_bytes_tall;
};
} // namespace deluge::gfx::oled
