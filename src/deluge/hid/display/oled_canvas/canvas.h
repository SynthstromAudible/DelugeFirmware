/*
 * Copyright © 2024 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
// FACADE (remove after UI phase 5): the firmware's OLED canvas is gfx/oled/canvas.h plus
// four pieces of policy (rounded-corner and menu-highlighting settings, the highlighting
// primitive, an etl overload) and the 2-D image-store accessor that hid/display/oled.cpp
// still composites with. New code must call the gfx (snake_case) names directly; the
// camelCase forwarders below exist only so the legacy call sites compile unchanged.
#include "gfx/oled/canvas.h"
#include "gfx/oled/font.h"
#include "gfx/oled/icon.h"
#include "util/etl_string.h"
#include <cstdint>
#include <string_view>

namespace deluge::hid::display {
class OLED;

using Icon = deluge::gfx::oled::Icon;

namespace oled_canvas {
using deluge::gfx::oled::BorderRadius;
using deluge::gfx::oled::DrawLineOptions;
using deluge::gfx::oled::Point;

// Legacy spellings of the two BorderRadius values, kept for the app call sites and the golden
// specs. The library enum is now `enum class BorderRadius : uint8_t { Small, Big }` (the
// unscoped SMALL/BIG used to leak into `deluge::gfx::oled`); this façade is the app layer, where
// the legacy names belong.
inline constexpr BorderRadius SMALL = BorderRadius::Small;
inline constexpr BorderRadius BIG = BorderRadius::Big;

class Canvas : public deluge::gfx::oled::OwnedCanvas<deluge::gfx::oled::kDelugeOled> {
	using Base = deluge::gfx::oled::OwnedCanvas<deluge::gfx::oled::kDelugeOled>;

public:
	Canvas() = default;

	/// Whether the *Rounded primitives actually round their corners. Kept in sync with the
	/// RoundedCorners community feature by the settings/menu layer; defaults to on so popups
	/// drawn before settings are loaded at boot match the feature's default.
	static inline bool roundedCornersEnabled = true;

	/// Whether menu highlighting inverts (true) or draws an edge line (false). Mirrors
	/// FlashStorage::accessibilityMenuHighlighting; the settings layer keeps it in sync.
	static inline bool menuHighlightingInverts = true;

	/// @{
	/// @name Legacy names → gfx names. One forwarder per Canvas method; `clear()` is
	///       inherited unchanged. Defaults match the legacy header, with the gfx
	///       `kEdge`/`kCentre` sentinels standing in for the panel width / half-width.

	void clearAreaExact(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY) {
		clear_area_exact(minX, minY, maxX, maxY);
	}
	void drawPixel(int32_t x, int32_t y) { draw_pixel(x, y); }
	void clearPixel(int32_t x, int32_t y) { clear_pixel(x, y); }
	void invertPixel(int32_t x, int32_t y) { invert_pixel(x, y); }
	void drawHorizontalLine(int32_t pixelY, int32_t startX, int32_t endX) {
		draw_horizontal_line(pixelY, startX, endX);
	}
	void drawVerticalLine(int32_t pixelX, int32_t startY, int32_t endY) { draw_vertical_line(pixelX, startY, endY); }
	void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const DrawLineOptions& options = {}) {
		draw_line(x0, y0, x1, y1, options);
	}
	void drawRectangle(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY) {
		draw_rectangle(minX, minY, maxX, maxY);
	}
	void drawRectangleRounded(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY, BorderRadius radius = SMALL) {
		draw_rectangle_rounded(minX, minY, maxX, maxY, radius, roundedCornersEnabled);
	}
	void drawCircle(int32_t centerX, int32_t centerY, int32_t radius, bool fill = false) {
		draw_circle(centerX, centerY, radius, fill);
	}
	void drawString(std::string_view str, int32_t pixelX, int32_t pixelY, int32_t textWidth, int32_t textHeight,
	                int32_t scrollPos = 0, int32_t endX = deluge::gfx::oled::kEdge, bool useTextWidth = false) {
		draw_string(str, pixelX, pixelY, textWidth, textHeight, scrollPos, endX, useTextWidth);
	}
	void drawStringCentred(char const* string, int32_t pixelY, int32_t textWidth, int32_t textHeight,
	                       int32_t centrePos = deluge::gfx::oled::kCentre) {
		draw_string_centred(string, pixelY, textWidth, textHeight, centrePos);
	}
	void drawStringCentered(char const* string, int32_t startX, int32_t startY, int32_t textSpacingX,
	                        int32_t textSpacingY, int32_t totalWidth) {
		draw_string_centered(string, startX, startY, textSpacingX, textSpacingY, totalWidth);
	}
	/// Overload for the etl string buffers the menus build their values in. The trimming the
	/// legacy overload did in place now happens on gfx's own copy, which no caller observed.
	void drawStringCentered(etl::istring& stringBuf, int32_t startX, int32_t startY, int32_t textSpacingX,
	                        int32_t textSpacingY, int32_t totalWidth) {
		draw_string_centered(stringBuf.c_str(), startX, startY, textSpacingX, textSpacingY, totalWidth);
	}
	void drawStringCentredShrinkIfNecessary(char const* string, int32_t pixelY, int32_t textWidth, int32_t textHeight) {
		draw_string_centred_shrink_if_necessary(string, pixelY, textWidth, textHeight);
	}
	void drawStringAlignRight(char const* string, int32_t pixelY, int32_t textWidth, int32_t textHeight,
	                          int32_t rightPos = deluge::gfx::oled::kEdge) {
		draw_string_align_right(string, pixelY, textWidth, textHeight, rightPos);
	}
	void drawChar(uint8_t theChar, int32_t pixelX, int32_t pixelY, int32_t textWidth, int32_t textHeight,
	              int32_t scrollPos = 0, int32_t endX = deluge::gfx::oled::kEdge) {
		draw_char(theChar, pixelX, pixelY, textWidth, textHeight, scrollPos, endX);
	}
	int32_t getCharIndex(uint8_t theChar) { return deluge::gfx::oled::glyph_index(theChar); }
	int32_t getCharWidthInPixels(uint8_t theChar, int32_t textHeight) {
		return get_char_width_in_pixels(theChar, textHeight);
	}
	int32_t getCharSpacingInPixels(uint8_t theChar, int32_t textHeight, bool isLastChar) {
		return get_char_spacing_in_pixels(theChar, textHeight, isLastChar);
	}
	int32_t getStringWidthInPixels(char const* string, int32_t textHeight) {
		return get_string_width_in_pixels(string, textHeight);
	}
	void drawGraphicMultiLine(uint8_t const* graphic, int32_t startX, int32_t startY, int32_t width, int32_t height = 8,
	                          int32_t numBytesTall = 1, bool reversed = false) {
		draw_graphic_multi_line(graphic, startX, startY, width, height, numBytesTall, reversed);
	}
	void drawIcon(const Icon& icon, int32_t x, int32_t y, bool reversed = false) { draw_icon(icon, x, y, reversed); }
	void drawIconCentered(const Icon& icon, int32_t startX, int32_t totalWidth, int32_t y, bool reversed = false) {
		draw_icon_centered(icon, startX, totalWidth, y, reversed);
	}
	void drawScreenTitle(std::string_view text, bool drawSeparator = true) { draw_screen_title(text, drawSeparator); }
	void invertArea(int32_t xMin, int32_t width, int32_t startY, int32_t endY) {
		invert_area(xMin, width, startY, endY);
	}
	void invertAreaRounded(int32_t xMin, int32_t width, int32_t startY, int32_t endY, BorderRadius radius = SMALL) {
		invert_area_rounded(xMin, width, startY, endY, radius, roundedCornersEnabled);
	}

	/// Invert just the left edge of the canvas (or draw a line there, per the accessibility setting).
	///
	/// @param xMin Minimum X coordinate, inclusive
	/// @param width Width of the region to invert. End coordinate is excluded.
	/// @param startY Minimum Y coordinate, inclusive
	/// @param endY Maximum Y coordinate, inclusive
	void invertLeftEdgeForMenuHighlighting(int32_t xMin, int32_t width, int32_t startY, int32_t endY);

	/// @}

	/// Height of the image in bytes
	static constexpr uint32_t kImageHeight = deluge::gfx::oled::kDelugeOled.pages();
	/// Width of the image in pixels
	static constexpr uint32_t kImageWidth = deluge::gfx::oled::kDelugeOled.width;

	using ImageStore = uint8_t[kImageHeight][kImageWidth];

	/// XXX: DO NOT USE THIS OUTSIDE OF THE CORE OLED CODE; it disappears with the shell (phase 5).
	ImageStore& hackGetImageStore() { return *reinterpret_cast<ImageStore*>(bytes().data()); }
};
} // namespace oled_canvas
} // namespace deluge::hid::display
