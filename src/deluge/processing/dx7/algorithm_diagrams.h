#pragma once

/// @brief The 32 DX7 algorithm diagrams the DX7 parameter page draws for its algorithm.
///
/// Carriers are drawn as filled boxes with an inverted digit, modulators as outlined boxes. Each is a column-major
/// 1-bit bitmap for `Canvas::draw_graphic_multi_line`: `num_bytes_tall` bytes per column, least significant bit at the
/// top pixel. The data lives in SDRAM, which is why it lives here, where both the legacy menu and the ported one may
/// include it, rather than in the UI toolkit, which has no memory placement of its own.

#include <cstdint>

namespace deluge::dx7 {

/// @brief How many DX7 algorithms there are.
inline constexpr int32_t kNumAlgorithms = 32;

/// @brief One algorithm's diagram and where on the OLED it is drawn, as in the spreadsheet mockups.
struct AlgorithmDiagram {
	const uint8_t* data;    ///< the bitmap
	uint8_t x;              ///< left edge, in OLED pixels
	uint8_t y;              ///< top edge, in OLED pixels
	uint8_t width;          ///< in pixels
	uint8_t height;         ///< in pixels
	uint8_t num_bytes_tall; ///< bytes per column
};

/// @brief The diagram of algorithm @p n.
/// @param n The algorithm, 0-based, clamped to `[0, kNumAlgorithms - 1]`.
AlgorithmDiagram algorithm_diagram(int32_t n);

} // namespace deluge::dx7
