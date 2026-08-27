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

#include "gfx/oled/geometry.h"
#include "gfx/oled/icon.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace deluge::gfx::oled {

enum BorderRadius : uint8_t {
	SMALL = 0, //< 1px
	BIG = 1    //< 2px
};

struct DrawLineOptions {
	bool thick{false};
	std::optional<uint8_t> min_x{std::nullopt};
	std::optional<uint8_t> max_x{std::nullopt};
	std::optional<std::function<void(Point)>> point_callback{std::nullopt};
};

/// A non-owning 1-bpp page-major drawing surface over caller-provided storage.
///
/// The panel size arrives as a value (\ref Geometry), so the same code draws on any
/// OLED-like surface: `bytes()[page * width + x]`, bit `b` of that byte being row
/// `page * 8 + b`. Nothing here knows about the board, the settings or the display driver.
class Canvas {
public:
	/// @param storage At least `g.bytes()` bytes of caller-owned storage. Not copied; the
	///                caller keeps it alive for as long as the canvas is used.
	/// @param g Panel geometry.
	///
	/// Precondition: `storage.size() >= g.bytes()`. A shorter span is a programming error, not
	/// a runtime condition to recover from -- the canvas would index off the end of it on the
	/// first draw call. Asserted below.
	Canvas(std::span<uint8_t> storage, Geometry g) : geometry_(g) {
		// Body form rather than a member initialiser so the assert runs FIRST: `storage.first(n)`
		// with n > storage.size() is itself undefined, so it must not happen before the check.
		assert(storage.size() >= g.bytes());
		bytes_ = storage.first(g.bytes());
	}

	/// The geometry this canvas was built with.
	Geometry geometry() const { return geometry_; }

	/// The framebuffer, page-major.
	std::span<uint8_t> bytes() { return bytes_; }
	std::span<const uint8_t> bytes() const { return bytes_; }

	/// The first byte of page \p p (i.e. of pixel rows `p * 8` to `p * 8 + 7`).
	uint8_t* page(uint32_t p) { return &bytes_[p * geometry_.width]; }

	/// @{
	/// @name Rendering routines

	/// Clear the entire image
	void clear() {
		// Takes about 1 fast-timer tick (whereas entire rendering takes around 8 to 15).
		// So, not worth trying to use DMA here or anything.
		std::fill(bytes_.begin(), bytes_.end(), 0);
	};

	/// Clear only a subset of the image
	///
	/// @param min_x minimum X coordinate, inclusive
	/// @param min_y minimum Y coordinate, inclusive
	/// @param max_x maximum X coordinate, inclusive
	/// @param max_y maximum Y coordinate, inclusive
	void clear_area_exact(int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y);

	/// Set a single pixel
	void draw_pixel(int32_t x, int32_t y);

	/// Clear a single pixel
	void clear_pixel(int32_t x, int32_t y);

	/// Invert a single pixel
	void invert_pixel(int32_t x, int32_t y);

	/// Draw a horizontal line.
	///
	/// @param pixel_y Y coordinate of the line to draw
	/// @param start_x Starting X coordinate, inclusive
	/// @param end_x Ending X coordinate, inclusive
	void draw_horizontal_line(int32_t pixel_y, int32_t start_x, int32_t end_x);

	/// Draw a vertical line.
	///
	/// @param pixel_x X coordinate of the line
	/// @param start_y Y coordinate of the line, inclusive
	/// @param end_y Y coordinate of the line, inclusive
	void draw_vertical_line(int32_t pixel_x, int32_t start_y, int32_t end_y);

	/// Draw a line using Bresenham algorithm
	/// @param x0 Start X coordinate of the line
	/// @param y0 Start Y coordinate of the line
	/// @param x1 End X coordinate of the line
	/// @param y1 End Y coordinate of the line
	/// @param options Draw options
	void draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const DrawLineOptions& options = {});

	/// Draw a 1-px wide rectangle.
	///
	/// @param min_x Minimum X coordinate, inclusive
	/// @param min_y Minimum Y coordinate, inclusive
	/// @param max_x Maximum X coordinate, inclusive
	/// @param max_y Maximum Y coordinate, inclusive
	void draw_rectangle(int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y);

	/// Draw a 1-px wide rectangle with rounded corners
	///
	/// @param min_x Minimum X coordinate, inclusive
	/// @param min_y Minimum Y coordinate, inclusive
	/// @param max_x Maximum X coordinate, inclusive
	/// @param max_y Maximum Y coordinate, inclusive
	/// @param radius Corner radius
	/// @param rounded When false, the corners are square (the caller's policy decides)
	void draw_rectangle_rounded(int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y, BorderRadius radius = SMALL,
	                            bool rounded = true);

	/// Draw a circle using Bresenham algorithm
	///
	/// @param center_x Center X coordinate
	/// @param center_y Center Y coordinate
	/// @param radius Circle radius
	void draw_circle(int32_t center_x, int32_t center_y, int32_t radius, bool fill = false);

	/// Draw a string
	///
	/// @param str The string
	/// @param pixel_x X coordinate of the left side of the string
	/// @param pixel_y Y coordinate of the top side of the string
	/// @param text_width Base width in pixels of each character
	/// @param text_height Height in pixels of each character
	/// @param scroll_pos Offset in pixels representing how far the text has scrolled from the left.
	/// @param end_x Maximum X coordinate after which we bail out, or \ref kEdge for the canvas width.
	///              N.B. this means the *actual* maximum X coordinate rendered is end_x + text_width,
	///              as the individual character rendering work can overshoot.
	void draw_string(std::string_view str, int32_t pixel_x, int32_t pixel_y, int32_t text_width, int32_t text_height,
	                 int32_t scroll_pos = 0, int32_t end_x = kEdge, bool use_text_width = false);

	/// Draw a string, centered at the provided location.
	///
	/// @param string A null-terminated C string
	/// @param pixel_y Y coordinate of the top side of the string
	/// @param text_width Base width in pixels of each character
	/// @param text_height Height in pixels of each character
	/// @param centre_pos X coordinate to centre on, or \ref kCentre for the canvas centre
	void draw_string_centred(char const* string, int32_t pixel_y, int32_t text_width, int32_t text_height,
	                         int32_t centre_pos = kCentre);

	/// Draw a string, centered between the provided start_x and start_x + total_width
	///
	/// @param string A null-terminated C string
	/// @param start_x Beginning X coordinate for center calculation
	/// @param start_y Y coordinate of the top side of the string
	/// @param text_spacing_x Base width in pixels of each character
	/// @param text_spacing_y Height in pixels of each character
	/// @param total_width Total width for center calculation
	void draw_string_centered(char const* string, int32_t start_x, int32_t start_y, int32_t text_spacing_x,
	                          int32_t text_spacing_y, int32_t total_width);

	/// Draw a string, reducing its height so the string fits within the specified width
	///
	/// @param string A null-terminated C string
	/// @param pixel_y The Y coordinate of the top of the string
	/// @param text_width Requested width for each character in the string
	/// @param text_height Requested height for each character in the string
	void draw_string_centred_shrink_if_necessary(char const* string, int32_t pixel_y, int32_t text_width,
	                                             int32_t text_height);

	/// Draw a string, aligned to the right.
	///
	/// @param string A null-terminated C string
	/// @param pixel_y The Y coordinate of the top of the string
	/// @param text_width The width for each character in the string
	/// @param text_height The height for each character in the string
	/// @param right_pos X coordinate, exclusive, of the rightmost pixel to use for the string, or
	///                  \ref kEdge for the canvas width
	void draw_string_align_right(char const* string, int32_t pixel_y, int32_t text_width, int32_t text_height,
	                             int32_t right_pos = kEdge);

	/// Draw a single character
	void draw_char(uint8_t the_char, int32_t pixel_x, int32_t pixel_y, int32_t spacing_x, int32_t text_height,
	               int32_t scroll_pos = 0, int32_t end_x = kEdge);

	/// Return width of a single character with a given character height
	///
	/// @param the_char A single character
	/// @param text_height The height of the character
	int32_t get_char_width_in_pixels(uint8_t the_char, int32_t text_height);

	/// Returns spacing in pixels between characters drawn in a string
	///
	/// @param the_char A single character
	/// @param text_height The height of the character (to distinguish between non-bold and bold characters)
	/// @param is_last_char a boolean to specify whether any char's follow this char
	int32_t get_char_spacing_in_pixels(uint8_t the_char, int32_t text_height, bool is_last_char);

	/// Returns width of a string in pixels
	///
	/// @param string A null-terminated C string
	/// @param text_height The height for each character in the string
	int32_t get_string_width_in_pixels(char const* string, int32_t text_height);

	/// Draw a "graphic".
	///
	/// The provided \ref graphic array is used as a bit mask and added to the existing content.
	///
	/// @param graphic Pointer to the start of an array representing the graphic.
	/// @param start_x X coodinate of the left edge of the graphic
	/// @param start_y Y coordinate of the top of the graphic
	/// @param width Width of the graphic in pixels
	/// @param height Height of the graphic in pixels
	/// @param num_bytes_tall Number of bytes in the Y direction, determines the stride in the graphic array
	void draw_graphic_multi_line(uint8_t const* graphic, int32_t start_x, int32_t start_y, int32_t width,
	                             int32_t height = 8, int32_t num_bytes_tall = 1, bool reversed = false);

	/// Draw an icon.
	///
	/// @param icon Reference to the icon
	/// @param x X coodinate of the left edge of the icon
	/// @param y Y coordinate of the top of the icon
	/// @param reversed Should reverse the icon horizontally
	void draw_icon(const Icon& icon, int32_t x, int32_t y, bool reversed = false);

	/// Draw an icon, centered between the provided start_x and start_x + total_width
	///
	/// @param icon Reference to the icon
	/// @param start_x Beginning X coordinate for center calculation
	/// @param total_width Total width for center calculation
	/// @param y Y coordinate of the top of the icon
	/// @param reversed Should reverse the icon horizontally
	void draw_icon_centered(const Icon& icon, int32_t start_x, int32_t total_width, int32_t y, bool reversed = false);

	/// Draw a screen title and underline it.
	///
	/// @param text Title text
	void draw_screen_title(std::string_view text, bool draw_separator = true);

	/// Invert an area of the canvas.
	///
	/// @param x_min Minimum X coordinate, inclusive
	/// @param width Width of the region to invert. End coordinate is excluded.
	/// @param start_y Minimum Y coordinate, inclusive
	/// @param end_y Maximum Y coordinate, inclusive
	void invert_area(int32_t x_min, int32_t width, int32_t start_y, int32_t end_y);

	/// Invert an area of the canvas with rounded corners
	///
	/// @param x_min Minimum X coordinate, inclusive
	/// @param width Width of the region to invert. End coordinate is excluded.
	/// @param start_y Minimum Y coordinate, inclusive
	/// @param end_y Maximum Y coordinate, inclusive
	/// @param radius Corner radius
	/// @param rounded When false, the corners are left inverted (the caller's policy decides)
	void invert_area_rounded(int32_t x_min, int32_t width, int32_t start_y, int32_t end_y, BorderRadius radius = SMALL,
	                         bool rounded = true);

	/// @}

private:
	/// Largest in-bounds X coordinate.
	int32_t last_x() const { return geometry_.width - 1; }
	/// Largest in-bounds Y coordinate.
	int32_t last_y() const { return geometry_.height - 1; }
	/// Clamp a rectangle to the canvas. Returns false if nothing of it is on-canvas.
	bool clip_rect(int32_t& min_x, int32_t& min_y, int32_t& max_x, int32_t& max_y) const;

	std::span<uint8_t> bytes_;
	Geometry geometry_;
};

/// Framebuffers are DMA'd on the real hardware, so they want to start on a cache line.
/// == CACHE_LINE_SIZE in board_config.h (RZ/A1, 32-byte line); gfx must not include that header.
/// Revisit for a board with a 64-byte line.
constexpr size_t kFramebufferAlignment = 32;

/// A \ref Canvas that owns its (statically sized, aligned) storage.
template <Geometry G, size_t Align = kFramebufferAlignment>
class OwnedCanvas : public Canvas {
public:
	OwnedCanvas() : Canvas(store_, G) {}
	OwnedCanvas(const OwnedCanvas&) = delete;
	OwnedCanvas& operator=(const OwnedCanvas&) = delete;

private:
	alignas(Align) uint8_t store_[G.bytes()]{};
};

} // namespace deluge::gfx::oled
