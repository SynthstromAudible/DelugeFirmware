#pragma once
#include "gfx/grid/pad_grid.h"
#include <string>

/// @brief Serialize a PadGrid to the deluge-sdk headless `.state` format.
///
/// @param g The grid to serialize.
/// @return One `pad x y r g b` line per non-black pad.
inline std::string grid_to_state(const deluge::gfx::grid::PadGrid& g) {
	std::string out;
	for (uint8_t y = 0; y < g.dims().rows; y++) {
		for (uint8_t x = 0; x < g.dims().cols(); x++) {
			const RGB& c = g.at(x, y);
			if (c.r | c.g | c.b) {
				out += "pad " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(c.r) + " "
				       + std::to_string(c.g) + " " + std::to_string(c.b) + "\n";
			}
		}
	}
	return out;
}
