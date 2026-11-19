#pragma once

/// @brief The 32 DX7 algorithm diagrams the DX7 parameter page draws for its algorithm.
///
/// Carriers are drawn as filled boxes with an inverted digit, modulators as outlined boxes. Each is a column-major
/// 1-bit bitmap for `Canvas::draw_graphic_multi_line`: `num_bytes_tall` bytes per column, least significant bit at the
/// top pixel. Only the DX menu draws them, so they are the menu's own art; the tables live in this namespace's
/// `.rodata`, which the linker places in SDRAM.

#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::scenes::menu {

/// @brief How many DX7 algorithms there are.
inline constexpr size_t kDxAlgorithms = 32;

/// @brief One algorithm's diagram and where on the OLED it is drawn, as in the spreadsheet mockups.
struct DxAlgorithmDiagram {
	std::span<const uint8_t> data; ///< the bitmap: `width * num_bytes_tall` bytes
	uint8_t x;                     ///< left edge, in OLED pixels
	uint8_t y;                     ///< top edge, in OLED pixels
	uint8_t width;                 ///< in pixels
	uint8_t height;                ///< in pixels
	uint8_t num_bytes_tall;        ///< bytes per column
};

/// @brief The diagram of algorithm @p algorithm.
/// @param algorithm The algorithm, 0-based; out of range is clamped to the last algorithm.
/// @return The diagram, which lives for the whole run.
[[nodiscard]] const DxAlgorithmDiagram& dx_algorithm_diagram(size_t algorithm);

} // namespace deluge::scenes::menu
