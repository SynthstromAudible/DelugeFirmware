// Carriers are drawn as filled boxes with an inverted digit, modulators as outlined boxes.
// Column-major 1-bit bitmaps for OLED::drawGraphicMultiLine(): numBytesTall bytes per column, LSB = top pixel.
#pragma once

#include <cstdint>

namespace deluge::gui::menu_item::dx {

struct AlgorithmBitmap {
	uint16_t offset; // into kAlgorithmBitmapData
	uint8_t x;
	uint8_t y;
	uint8_t width;
	uint8_t height;
	uint8_t numBytesTall;
};

inline constexpr int32_t kNumAlgorithms = 32;

// Placed in SDRAM, see algorithm_bitmaps.cpp
extern const uint8_t kAlgorithmBitmapData[];
extern const AlgorithmBitmap kAlgorithmBitmaps[kNumAlgorithms];

} // namespace deluge::gui::menu_item::dx
