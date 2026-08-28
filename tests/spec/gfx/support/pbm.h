#pragma once
#include <cstdint>
#include <string>

/// @brief Convert a 1-bpp page-major OLED framebuffer to a P1 (plain-text) PBM image.
///
/// @param bytes  Page-major framebuffer: `bytes[page*width+col]`, bit `b` = row `page*8+b`.
/// @param width  Framebuffer width, in pixels.
/// @param height Framebuffer height, in pixels.
/// @return A P1 PBM: `"P1\n<w> <h>\n"` then one line per pixel row of '1'/'0' separated by spaces.
inline std::string page_major_to_pbm(const uint8_t* bytes, int width, int height) {
	std::string out = "P1\n" + std::to_string(width) + " " + std::to_string(height) + "\n";
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			bool on = bytes[(y >> 3) * width + x] & (1u << (y & 7));
			out += on ? '1' : '0';
			if (x + 1 < width)
				out += ' ';
		}
		out += '\n';
	}
	return out;
}
