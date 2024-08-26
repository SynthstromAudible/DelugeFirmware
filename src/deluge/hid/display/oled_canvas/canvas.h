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

#include "RZA1/cpu_specific.h"
#include "definitions.h"
#include "deluge/util/d_string.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string_view>

namespace deluge::hid::display {
class OLED;
struct Icon;

namespace oled_canvas {

enum BorderRadius : uint8_t {
	SMALL = 0, //< 1px
	BIG = 1    //< 2px
};

struct Point {
	int32_t x;
	int32_t y;
	bool operator==(const Point& other) const noexcept { return x == other.x && y == other.y; }
};

struct DrawLineOptions {
	bool thick{false};
	std::optional<uint8_t> min_x{std::nullopt};
	std::optional<uint8_t> max_x{std::nullopt};
	std::optional<std::function<void(Point)>> point_callback{std::nullopt};
};

class Canvas {
public:
	Canvas() = default;
	~Canvas() = default;

	Canvas(Canvas const& other) = delete;
	Canvas(Canvas&& other) = delete;

	Canvas& operator=(Canvas const& other) = delete;
	Canvas& operator=(Canvas&& other) = delete;

	/// @{
	/// @name Rendering routines

	/// Clear the entire image
	void clear() {
		// Takes about 1 fast-timer tick (whereas entire rendering takes around 8 to 15).
		// So, not worth trying to use DMA here or anything.
		memset(image_, 0, sizeof(image_));
	};

	/// Clear only a subset of the image
	///
	/// @param minX minimum X coordinate, inclusive
	/// @param minY minimum Y coordinate, inclusive
	/// @param maxX maximum X coordinate, inclusive
	/// @param maxY maximum Y coordinate, *exclusive*
	void clearAreaExact(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY);

	/// Set a single pixel
	void drawPixel(int32_t x, int32_t y);

	/// Clear a single pixel
	void clearPixel(int32_t x, int32_t y);

	/// Invert a single pixel
	void invertPixel(int32_t x, int32_t y);

	/// Draw a horizontal line.
	///
	/// @param pixelY Y coordinate of the line to draw
	/// @param startX Starting X coordinate, inclusive
	/// @param endY Ending Y coordinate, inclusive
	void drawHorizontalLine(int32_t pixelY, int32_t startX, int32_t endX);

	/// Draw a vertical line.
	///
	/// @param pixelX X coordinate of the line
	/// @param startY Y coordinate of the line, inclusive
	/// @param endY Y coordinate of the line, inclusive
	void drawVerticalLine(int32_t pixelX, int32_t startY, int32_t endY);

	/// Draw a line using Bresenham algorithm
	/// @param x0 Start X coordinate of the line
	/// @param y0 Start Y coordinate of the line
	/// @param x1 End X coordinate of the line
	/// @param y1 End Y coordinate of the line
	/// @param options Draw options
	void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const DrawLineOptions& options = {});

	/// Draw a 1-px wide rectangle.
	///
	/// @param minX Minimum X coordinate, inclusive
	/// @param minY Minimum Y coordinate, inclusive
	/// @param maxX Maximum X coordinate, inclusive
	/// @param maxY Maximum Y coordinate, inclusive
	void drawRectangle(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY);

	/// Draw a 1-px wide rectangle with rounded corners
	///
	/// @param minX Minimum X coordinate, inclusive
	/// @param minY Minimum Y coordinate, inclusive
	/// @param maxX Maximum X coordinate, inclusive
	/// @param maxY Maximum Y coordinate, inclusive
	void drawRectangleRounded(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY, BorderRadius radius = SMALL);

	/// Draw a circle using Bresenham algorithm
	///
	/// @param centerX Center X coordinate
	/// @param centerY Center Y coordinate
	/// @param radius Circle radius
	void drawCircle(int32_t centerX, int32_t centerY, int32_t radius, bool fill = false);

	/// Draw a string
	///
	/// @param str The string
	/// @param pixelX X coordinate of the left side of the string
	/// @param pixelY Y coordinate of the top side of the string
	/// @param textWidth Base width in pixels of each character
	/// @param textHeight Height in pixels of each character
	/// @param scrollPos Offset in pixels representing how far the text has scrolled from the left.
	/// @param endX Maximum X coordinate after which we bail out. N.B. this means the *actual* maximum X coordinate
	///             rendered is endX + textWidth, as the individual character rendering work can overshoot.
	void drawString(std::string_view str, int32_t pixelX, int32_t pixelY, int32_t textWidth, int32_t textHeight,
	                int32_t scrollPos = 0, int32_t endX = OLED_MAIN_WIDTH_PIXELS, bool useTextWidth = false);

	/// Draw a string, centered at the provided location.
	///
	/// @param string A null-terminated C string
	/// @param pixelY Y coordinate of the top side of the string
	/// @param textWidth Base width in pixels of each character
	/// @param textHeight Height in pixels of each character
	/// @param scrollPos Offset in pixels representing how far the text has scrolled from the left.
	/// @param endX Maximum X coordinate after which we bail out. N.B. this means the *actual* maximum X coordinate
	///             rendered is endX + textWidth, as the individual character rendering work can overshoot.
	void drawStringCentred(char const* string, int32_t pixelY, int32_t textWidth, int32_t textHeight,
	                       int32_t centrePos = OLED_MAIN_WIDTH_PIXELS / 2);

	/// Draw a string, centered between the provided startX and startX + totalWidth
	///
	/// @param string A null-terminated C string
	/// @param startX Beginning X coordinate for center calculation
	/// @param startY Y coordinate of the top side of the string
	/// @param textSpacingX Base width in pixels of each character
	/// @param textSpacingY Height in pixels of each character
	/// @param totalWidth Total width for center calculation
	void drawStringCentered(char const* string, int32_t startX, int32_t startY, int32_t textSpacingX,
	                        int32_t textSpacingY, int32_t totalWidth);

	/// Draw a string, centered between the provided startX and startX + totalWidth
	///
	/// @param stringBuf A string buffer
	/// @param startX Beginning X coordinate for center calculation
	/// @param startY Y coordinate of the top side of the string
	/// @param textSpacingX Base width in pixels of each character
	/// @param textSpacingY Height in pixels of each character
	/// @param totalWidth Total width for center calculation
	void drawStringCentered(StringBuf& stringBuf, int32_t startX, int32_t startY, int32_t textSpacingX,
	                        int32_t textSpacingY, int32_t totalWidth);

	/// Draw a string, reducing its height so the string fits within the specified width
	///
	/// @param string A null-terminated C string
	/// @param pixelY The Y coordinate of the top of the string
	/// @param textWidth Requested width for each character in the string
	/// @param textHeight Requested height for each character in the string
	void drawStringCentredShrinkIfNecessary(char const* string, int32_t pixelY, int32_t textWidth, int32_t textHeight);

	/// Draw a string, aligned to the right.
	///
	/// @param string A null-terminated C string
	/// @param pixelY The Y coordinate of the top of the string
	/// @param textWidth The width for each character in the string
	/// @param textHeight The height for each character in the string
	/// @param rightPos X coordinate, exclusive, of the rightmost pixel to use for the string
	void drawStringAlignRight(char const* string, int32_t pixelY, int32_t textWidth, int32_t textHeight,
	                          int32_t rightPos = OLED_MAIN_WIDTH_PIXELS);

	/// Draw a single character
	void drawChar(uint8_t theChar, int32_t pixelX, int32_t pixelY, int32_t textWidth, int32_t textHeight,
	              int32_t scrollPos = 0, int32_t endX = OLED_MAIN_WIDTH_PIXELS);

	/// Returns index for character so it can be looked up
	///
	/// @param theChar A single character
	int32_t getCharIndex(uint8_t theChar);

	/// Return width of a single character with a given character height
	///
	/// @param theChar A single character
	/// @param textHeight The height of the character
	int32_t getCharWidthInPixels(uint8_t theChar, int32_t textHeight);

	/// Returns spacing in pixels between characters drawn in a string
	///
	/// @param theChar A single character
	/// @param textHeight The height of the character (to distinguish between non-bold and bold characters)
	/// @param isLastChar a boolean to specify whether any char's follow this char
	int32_t getCharSpacingInPixels(uint8_t theChar, int32_t textHeight, bool isLastChar);

	/// Returns width of a string in pixels
	///
	/// @param string A null-terminated C string
	/// @param textHeight The height for each character in the string
	int32_t getStringWidthInPixels(char const* string, int32_t textHeight);

	/// Draw a "graphic".
	///
	/// The provided \ref graphic array is used as a bit mask and added to the existing content.
	///
	/// @param graphic Pointer to the start of an array representing the graphic.
	/// @param startX X coodinate of the left edge of the graphic
	/// @param startY Y coordinate of the top of the graphic
	/// @param width Width of the graphic in pixels
	/// @param height Height of the graphic in pixels
	/// @param numBytesTall Number of bytes in the Y direction, determines the stride in the graphic array
	void drawGraphicMultiLine(uint8_t const* graphic, int32_t startX, int32_t startY, int32_t width, int32_t height = 8,
	                          int32_t numBytesTall = 1, bool reversed = false);
	/// Draw an icon.
	///
	/// @param icon Reference to the icon
	/// @param x X coodinate of the left edge of the icon
	/// @param y Y coordinate of the top of the icon
	/// @param reversed Should reverse the icon horizontally
	void drawIcon(const Icon& icon, int32_t x, int32_t y, bool reversed = false);

	/// Draw an icon, centered between the provided startX and startX + totalWidth
	///
	/// @param icon Reference to the icon
	/// @param startX Beginning X coordinate for center calculation
	/// @param totalWidth Total width for center calculation
	/// @param y Y coordinate of the top of the icon
	/// @param reversed Should reverse the icon horizontally
	void drawIconCentered(const Icon& icon, int32_t startX, int32_t totalWidth, int32_t y, bool reversed = false);

	/// Draw a screen title and underline it.
	///
	/// @param text Title text
	void drawScreenTitle(std::string_view text, bool drawSeparator = true);

	/// Invert an area of the canvas.
	///
	/// @param xMin Minimum X coordinate, inclusive
	/// @param width Width of the region to invert. End coordinate is excluded.
	/// @param startY Minimum Y coordinate, inclusive
	/// @param endY Maximum Y coordinate, inclusive
	void invertArea(int32_t xMin, int32_t width, int32_t startY, int32_t endY);

	/// Invert an area of the canvas with rounded corners
	///
	/// @param xMin Minimum X coordinate, inclusive
	/// @param width Width of the region to invert. End coordinate is excluded.
	/// @param startY Minimum Y coordinate, inclusive
	/// @param endY Maximum Y coordinate, inclusive
	void invertAreaRounded(int32_t xMin, int32_t width, int32_t startY, int32_t endY, BorderRadius radius = SMALL);

	/// Invert just the left edge of the canvas.
	///
	/// @param xMin Minimum X coordinate, inclusive
	/// @param width Width of the region to invert. End coordinate is excluded.
	/// @param startY Minimum Y coordinate, inclusive
	/// @param endY Maximum Y coordinate, inclusive
	void invertLeftEdgeForMenuHighlighting(int32_t xMin, int32_t width, int32_t startY, int32_t endY);

	/// @}

	/// Height of the image in bytes
	static constexpr uint32_t kImageHeight = OLED_MAIN_HEIGHT_PIXELS >> 3;
	/// Width of the image in pixels
	static constexpr uint32_t kImageWidth = OLED_MAIN_WIDTH_PIXELS;

	using ImageStore = uint8_t[kImageHeight][kImageWidth];

	/// XXX: DO NOT USE THIS OUTSIDE OF THE CORE OLED CODE
	ImageStore& hackGetImageStore() { return image_; }

private:
	[[gnu::aligned(CACHE_LINE_SIZE)]] uint8_t image_[kImageHeight][kImageWidth];
};
} // namespace oled_canvas
} // namespace deluge::hid::display
