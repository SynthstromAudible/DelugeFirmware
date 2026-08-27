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

#include "gfx/oled/canvas.h"
#include "gfx/oled/font.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <utility>

using deluge::gfx::oled::Canvas;

bool Canvas::clip_rect(int32_t& min_x, int32_t& min_y, int32_t& max_x, int32_t& max_y) const {
	const int32_t limit_x = geometry_.width - 1;
	const int32_t limit_y = geometry_.height - 1;

	if (max_x < min_x || max_y < min_y || max_x < 0 || min_x > limit_x || max_y < 0 || min_y > limit_y) {
		return false;
	}

	min_x = std::clamp<int32_t>(min_x, 0, limit_x);
	max_x = std::clamp<int32_t>(max_x, 0, limit_x);
	min_y = std::clamp<int32_t>(min_y, 0, limit_y);
	max_y = std::clamp<int32_t>(max_y, 0, limit_y);
	return true;
}

void Canvas::clear_area_exact(int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y) {
	if (!clip_rect(min_x, min_y, max_x, max_y)) {
		return;
	}

	int32_t first_row = min_y >> 3;
	int32_t last_row = max_y >> 3;

	int32_t first_complete_row = first_row;
	int32_t last_complete_row = last_row;

	int32_t last_row_pixel_within = max_y & 7;
	bool will_do_last_row = (last_row_pixel_within != 7);
	uint8_t last_row_mask = 255 << (last_row_pixel_within + 1);

	// First row
	int32_t first_row_pixel_within = min_y & 7;
	if (first_row_pixel_within) {
		first_complete_row++;
		uint8_t first_row_mask = ~(255 << first_row_pixel_within);
		if (will_do_last_row && first_row == last_row) {
			first_row_mask &= last_row_mask;
		}
		for (int32_t x = min_x; x <= max_x; x++) {
			bytes_[first_row * geometry_.width + x] &= first_row_mask;
		}

		if (first_row == last_row) {
			return;
		}
	}

	// Last row
	if (will_do_last_row) {
		last_complete_row--;
		for (int32_t x = min_x; x <= max_x; x++) {
			bytes_[last_row * geometry_.width + x] &= last_row_mask;
		}
	}

	for (int32_t row = first_complete_row; row <= last_complete_row; row++) {
		std::fill_n(&bytes_[row * geometry_.width + min_x], max_x - min_x + 1, uint8_t{0});
	}
}

void Canvas::draw_pixel(int32_t x, int32_t y) {
	if (x < 0 || x > last_x() || y < 0 || y > last_y()) {
		return;
	}

	int32_t y_row = y >> 3;
	bytes_[y_row * geometry_.width + x] |= 1 << (y & 0x7);
}

void Canvas::clear_pixel(int32_t x, int32_t y) {
	if (x < 0 || x > last_x() || y < 0 || y > last_y()) {
		return;
	}

	int32_t y_row = y >> 3;
	bytes_[y_row * geometry_.width + x] &= ~(1 << (y & 0x7));
}

void Canvas::invert_pixel(int32_t x, int32_t y) {
	if (x < 0 || x > last_x() || y < 0 || y > last_y()) {
		return;
	}

	int32_t y_row = y >> 3;
	bytes_[y_row * geometry_.width + x] ^= 1 << (y & 0x7);
}

// NB: end_x here is an ordinary inclusive coordinate, not a kEdge-defaulted parameter — a
// negative end_x means "entirely off the left of the canvas" and must clip away, as callers
// like draw_rectangle (max_x - 1) and draw_circle (cx + x) rely on.
void Canvas::draw_horizontal_line(int32_t pixel_y, int32_t start_x, int32_t end_x) {
	if (pixel_y < 0 || pixel_y > last_y() || end_x < start_x || end_x < 0 || start_x > last_x()) {
		return;
	}

	start_x = std::clamp<int32_t>(start_x, 0, last_x());
	end_x = std::clamp<int32_t>(end_x, 0, last_x());
	if (end_x < start_x) {
		return;
	}

	uint8_t mask = 1 << (pixel_y & 7);

	uint8_t* __restrict__ current_pos = &bytes_[(pixel_y >> 3) * geometry_.width + start_x];
	uint8_t* const last_pos = current_pos + (end_x - start_x);
	do {
		*current_pos |= mask;
		current_pos++;
	} while (current_pos <= last_pos);
}

void Canvas::draw_vertical_line(int32_t pixel_x, int32_t start_y, int32_t end_y) {
	if (pixel_x < 0 || pixel_x > last_x() || end_y < start_y || end_y < 0 || start_y > last_y()) {
		return;
	}

	start_y = std::clamp<int32_t>(start_y, 0, last_y());
	end_y = std::clamp<int32_t>(end_y, 0, last_y());
	if (end_y < start_y) {
		return;
	}

	int32_t first_row_y = start_y >> 3;
	int32_t last_row_y = end_y >> 3;

	uint8_t first_row_mask = (255 << (start_y & 7));
	uint8_t last_row_mask = (255 >> (7 - (end_y & 7)));

	uint8_t* __restrict__ current_pos = &bytes_[first_row_y * geometry_.width + pixel_x];

	if (first_row_y == last_row_y) {
		uint8_t mask = first_row_mask & last_row_mask;
		*current_pos |= mask;
	}

	else {
		uint8_t* const final_pos = &bytes_[last_row_y * geometry_.width + pixel_x];

		// First row
		*current_pos |= first_row_mask;

		// Intermediate rows
		goto draw_solid;
		do {
			*current_pos = 255;
draw_solid:
			current_pos += geometry_.width;
		} while (current_pos != final_pos);

		// Last row
		*current_pos |= last_row_mask;
	}
}

void Canvas::draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const DrawLineOptions& options) {
	const bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
	if (steep) {
		std::swap(x0, y0);
		std::swap(x1, y1);
	}
	if (x0 > x1) {
		std::swap(x0, x1);
		std::swap(y0, y1);
	}

	int32_t dx = x1 - x0;
	int32_t dy = std::abs(y1 - y0);
	int32_t error = dx / 2;
	int32_t y = y0;
	int32_t y_step = y0 < y1 ? 1 : -1;

	for (int32_t x = x0; x <= x1; x++) {
		int32_t actual_x = steep ? y : x;
		int32_t actual_y = steep ? x : y;

		if (options.max_x.has_value() && actual_x > options.max_x.value()) {
			return;
		}
		if (!options.min_x.has_value() || actual_x >= options.min_x.value()) {
			draw_pixel(actual_x, actual_y);

			if (options.thick) {
				draw_pixel(steep ? actual_x + 1 : actual_x, steep ? actual_y : actual_y - 1);
			}

			if (options.point_callback.has_value()) {
				options.point_callback.value()({actual_x, actual_y});
			}
		}

		error -= dy;
		if (error < 0) {
			y += y_step;
			error += dx;
		}
	}
}

void Canvas::draw_rectangle(int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y) {
	draw_vertical_line(min_x, min_y, max_y);
	draw_vertical_line(max_x, min_y, max_y);

	draw_horizontal_line(min_y, min_x + 1, max_x - 1);
	draw_horizontal_line(max_y, min_x + 1, max_x - 1);
}

void Canvas::draw_rectangle_rounded(int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y, BorderRadius radius,
                                    bool rounded) {
	if (!rounded) {
		draw_rectangle(min_x, min_y, max_x, max_y);
		return;
	}

	const int32_t radius_pixels = radius == SMALL ? 1 : 2;

	draw_vertical_line(min_x, min_y + radius_pixels, max_y - radius_pixels);
	draw_vertical_line(max_x, min_y + radius_pixels, max_y - radius_pixels);
	draw_horizontal_line(min_y, min_x + radius_pixels, max_x - radius_pixels);
	draw_horizontal_line(max_y, min_x + radius_pixels, max_x - radius_pixels);

	if (radius_pixels == 2) {
		draw_pixel(min_x + 1, min_y + 1); //< Top-left corner
		draw_pixel(max_x - 1, min_y + 1); //< Top-right corner
		draw_pixel(min_x + 1, max_y - 1); //< Bottom-left corner
		draw_pixel(max_x - 1, max_y - 1); //< Bottom-right corner
	}
}

void Canvas::draw_circle(int32_t center_x, int32_t center_y, int32_t radius, bool fill) {
	int32_t x = 0;
	int32_t y = radius;
	// Add a small bias for small radii — helps round out edges
	int32_t d = 1 - radius + (radius <= 6 ? 1 : 0);

	auto plot_circle_points = [&](int32_t cx, int32_t cy, int32_t x, int32_t y) {
		if (fill) {
			// Fill horizontally between symmetric points
			draw_horizontal_line(cy + y, cx - x, cx + x);
			draw_horizontal_line(cy - y, cx - x, cx + x);
			draw_horizontal_line(cy + x, cx - y, cx + y);
			draw_horizontal_line(cy - x, cx - y, cx + y);
		}
		else {
			// Just outline
			draw_pixel(cx + x, cy + y);
			draw_pixel(cx - x, cy + y);
			draw_pixel(cx + x, cy - y);
			draw_pixel(cx - x, cy - y);
			draw_pixel(cx + y, cy + x);
			draw_pixel(cx - y, cy + x);
			draw_pixel(cx + y, cy - x);
			draw_pixel(cx - y, cy - x);
		}
	};

	while (x <= y) {
		plot_circle_points(center_x, center_y, x, y);

		// normal midpoint update
		if (d < 0) {
			d += 2 * x + 3;
		}
		else {
			d += 2 * (x - y) + 5;
			y--;
		}
		x++;

		// Small tweak: for very small circles, gradually adjust d to round diagonals
		if (radius <= 5) {
			d += x % 2 == 0 ? 1 : 0;
		}
	}
}

void Canvas::draw_string(std::string_view string, int32_t pixel_x, int32_t pixel_y, int32_t text_width,
                         int32_t text_height, int32_t scroll_pos, int32_t end_x, bool use_text_width) {
	if (end_x == kEdge) {
		end_x = geometry_.width;
	}

	int32_t last_index = string.length() - 1;
	int32_t char_idx = 0;
	int32_t char_width = text_width;
	// if the string is currently scrolling we want to identify the number of characters
	// that should be visible on the screen based on the current scroll position
	// to do iterate through each character in the string, based on its size in pixels
	// and compare that to the scroll position (which is also in pixels)
	// any characters before the scroll position are chopped off;
	if (scroll_pos) {
		int32_t num_chars_to_chop_off = 0;
		int32_t width_of_chars_to_chop_off = 0;
		int32_t char_start_x = 0;
		for (char const c : string) {
			if (!use_text_width) {
				int32_t char_spacing = get_char_spacing_in_pixels(c, text_height, char_idx == last_index);
				char_width = get_char_width_in_pixels(c, text_height) + char_spacing;
			}
			char_start_x += char_width;
			// are we past the scroll position?
			// if so no more characters to chop off
			if (scroll_pos < char_start_x) {
				break;
			}
			// we haven't reached scroll position yet, so chop off these characters
			else {
				num_chars_to_chop_off++;
				width_of_chars_to_chop_off += char_width;
			}
			char_idx++;
		}

		// chop off the characters before the scroll position
		string = string.substr(num_chars_to_chop_off);
		// adjust scroll position to indicate how far we've scrolled
		scroll_pos -= width_of_chars_to_chop_off;
		// calculate new last index
		last_index = string.length() - 1;
		// reset index
		char_idx = 0;
	}

	// if we scrolled above, then the string, scroll_pos and last_index will have been adjusted
	// here we're going to draw the remaining characters in the string
	for (char const c : string) {
		if (!use_text_width) {
			int32_t char_spacing = get_char_spacing_in_pixels(c, text_height, char_idx == last_index);
			char_width = get_char_width_in_pixels(c, text_height) + char_spacing;
		}
		draw_char(c, pixel_x, pixel_y, char_width, text_height, scroll_pos, end_x);

		// calculate the X coordinate to draw the next character
		pixel_x += (char_width - scroll_pos);

		// if we've reached the end_x coordinate then we won't draw anymore characters
		if (pixel_x >= end_x) {
			break;
		}

		// no more scrolling
		scroll_pos = 0;
		char_idx++;
	}
}

void Canvas::draw_string_centred(char const* string, int32_t pixel_y, int32_t text_width, int32_t text_height,
                                 int32_t centre_pos) {
	if (centre_pos == kCentre) {
		centre_pos = geometry_.width / 2;
	}

	std::string_view str{string};
	int32_t string_width = get_string_width_in_pixels(string, text_height);
	int32_t pixel_x = centre_pos - (string_width >> 1);
	draw_string(str, pixel_x, pixel_y, text_width, text_height);
}

void Canvas::draw_string_centered(char const* string, int32_t pixel_x, int32_t pixel_y, int32_t text_spacing_x,
                                  int32_t text_spacing_y, int32_t total_width) {
	// Was an etl::string<24> in the legacy canvas (which trimmed via resize()); a fixed stack
	// buffer of the same capacity keeps the behaviour without pulling etl into gfx.
	constexpr size_t kMaxChars = 24;
	char buf[kMaxChars + 1];
	size_t length = 0;
	while (length < kMaxChars && string[length] != '\0') {
		buf[length] = string[length];
		length++;
	}
	buf[length] = '\0';

	int32_t string_width;

	// Trim characters from the end until it fits.
	while ((string_width = get_string_width_in_pixels(buf, text_spacing_y)) >= total_width - 3) {
		if (length == 0) {
			break; // nothing left to trim (the legacy resize() would have underflowed here)
		}
		buf[--length] = '\0';
	}

	// Padding to center the string
	const float padding = (total_width - string_width) / 2.0f;
	int32_t padding_as_int = static_cast<int32_t>(padding);

	if (padding != padding_as_int) {
		// If we can't center exactly, 1px right is better than 1px left.
		padding_as_int++;
	}

	draw_string(buf, pixel_x + padding_as_int, pixel_y, string_width, text_spacing_y);
}

/// Draw a string, reducing its height so the string fits within the specified width
///
/// @param string A null-terminated C string
/// @param text_width Requested width for each character in the string
/// @param text_height Requested height for each character in the string
void Canvas::draw_string_centred_shrink_if_necessary(char const* string, int32_t pixel_y, int32_t text_width,
                                                     int32_t text_height) {
	bool shrink = false;
	std::string_view str{string};
	int32_t max_text_width = (uint32_t)geometry_.width / (uint32_t)str.length();
	if (text_width > max_text_width) {
		int32_t new_height = (uint32_t)(text_height * max_text_width) / (uint32_t)text_width;
		if (new_height >= 20) {
			new_height = 20;
		}
		else if (new_height >= 13) {
			new_height = 13;
		}
		else if (new_height >= 10) {
			new_height = 10;
		}
		else if (new_height >= 7) {
			new_height = 7;
		}
		else {
			new_height = 5;
		}

		text_width = max_text_width;

		int32_t height_diff = text_height - new_height;
		pixel_y += height_diff >> 1;
		text_height = new_height;

		shrink = true;
	}
	int32_t pixel_x = ((uint32_t)geometry_.width - text_width * str.length()) >> 1;
	draw_string(str, pixel_x, pixel_y, text_width, text_height, 0, geometry_.width, shrink);
}

void Canvas::draw_string_align_right(char const* string, int32_t pixel_y, int32_t text_width, int32_t text_height,
                                     int32_t right_pos) {
	if (right_pos == kEdge) {
		right_pos = geometry_.width;
	}

	std::string_view str{string};
	int32_t string_width = get_string_width_in_pixels(string, text_height);
	int32_t pixel_x = right_pos - (string_width);
	draw_string(str, pixel_x, pixel_y, text_width, text_height);
}

#define DO_CHARACTER_SCALING 0

void Canvas::draw_char(uint8_t the_char, int32_t pixel_x, int32_t pixel_y, int32_t spacing_x, int32_t text_height,
                       int32_t scroll_pos, int32_t end_x) {
	if (end_x == kEdge) {
		end_x = geometry_.width;
	}

	int32_t char_index = glyph_index(the_char);
	if (char_index <= 0) {
		return;
	}

	const auto& f = font_for_text_height(text_height);
	if (f.render_height != 0) {
		text_height = f.render_height;
	}
	pixel_y += f.y_adjust;
	lv_font_glyph_dsc_t const* descriptor = f.glyphs;
	uint8_t const* font = f.bitmap;
	[[maybe_unused]] int32_t font_native_height = f.native_height;

	descriptor += char_index;

#if DO_CHARACTER_SCALING
	int32_t scaled_font_width =
	    (uint16_t)(descriptor->w_px * text_height + (font_native_height >> 1) - 1) / (uint8_t)font_native_height;
#else
	int32_t scaled_font_width = descriptor->w_px;
#endif
	pixel_x += (spacing_x - scaled_font_width) >> 1;

	if (pixel_x < 0) {
		scroll_pos += -pixel_x;
		pixel_x = 0;
	}

	int32_t bytes_per_col = ((text_height - 1) >> 3) + 1;

	int32_t text_width = descriptor->w_px - scroll_pos;
	draw_graphic_multi_line(&font[descriptor->glyph_index + scroll_pos * bytes_per_col], pixel_x, pixel_y, text_width,
	                        text_height, bytes_per_col);
}

// The three metric routines below only read the font tables; their bodies live in font.cpp
// (`char_width_px` / `char_spacing_px` / `string_width_px`) so callers that just want to
// measure text do not need a canvas. These members stay as the established call-site spelling.
int32_t Canvas::get_char_width_in_pixels(uint8_t the_char, int32_t text_height) {
	return char_width_px(the_char, text_height);
}

int32_t Canvas::get_char_spacing_in_pixels(uint8_t the_char, int32_t text_height, bool is_last_char) {
	return char_spacing_px(the_char, text_height, is_last_char);
}

int32_t Canvas::get_string_width_in_pixels(char const* string, int32_t text_height) {
	return string_width_px(std::string_view{string}, text_height);
}

void Canvas::draw_graphic_multi_line(uint8_t const* graphic, int32_t start_x, int32_t start_y, int32_t width,
                                     int32_t height, int32_t num_bytes_tall, bool reversed) {
	if (reversed) {
		// Was a std::vector; gfx does not allocate in drawing paths. The largest reversed
		// graphic actually drawn is an icon (<= 32 columns), so a stack buffer is plenty.
		constexpr size_t kMaxReversed = 512;
		uint8_t reversed_graphic[kMaxReversed];
		const size_t n = static_cast<size_t>(width) * static_cast<size_t>(num_bytes_tall);
		if (n > kMaxReversed) {
			return; // larger reversed graphics do not exist; drawing nothing beats overflowing
		}
		for (int32_t col = 0; col < width; ++col) {
			int32_t input_index = col * num_bytes_tall;
			int32_t reversed_col = width - 1 - col;
			int32_t output_index = reversed_col * num_bytes_tall;
			for (int32_t byte = 0; byte < num_bytes_tall; ++byte) {
				reversed_graphic[output_index + byte] = graphic[input_index + byte];
			}
		}
		return draw_graphic_multi_line(reversed_graphic, start_x, start_y, width, height, num_bytes_tall);
	}

	int32_t row_on_display = start_y >> 3;
	int32_t y_offset = start_y & 7;
	int32_t row_on_graphic = 0;

	// The legacy code only bounds-checked the *middle* rows, so a first row off the top or
	// bottom of the panel, or a negative start_x, wrote outside the framebuffer. Now that the
	// storage belongs to the caller, refuse those outright. No on-canvas draw can reach this,
	// so the rendered output is unchanged.
	if (row_on_display < 0 || row_on_display >= static_cast<int32_t>(geometry_.pages()) || start_x < 0
	    || start_x >= geometry_.width || width <= 0) {
		return;
	}

	if (width > geometry_.width - start_x) {
		width = geometry_.width - start_x;
	}

	// First row
	uint8_t* __restrict__ current_pos = &bytes_[row_on_display * geometry_.width + start_x];
	uint8_t const* end_pos = current_pos + width;
	uint8_t const* __restrict__ graphic_pos = graphic;

	while (current_pos < end_pos) {
		*current_pos |= (*graphic_pos << y_offset);
		current_pos++;
		graphic_pos += num_bytes_tall;
	}

	int32_t y_offset_negative = 8 - y_offset;

	// Do middle rows
	while (true) {
		row_on_display++;
		if (row_on_display >= static_cast<int32_t>(geometry_.pages())) {
			return;
		}

		row_on_graphic++;
		if (height <= ((row_on_graphic << 3) - y_offset)) {
			return; // If no more of graphic to draw...
		}

		current_pos = &bytes_[row_on_display * geometry_.width + start_x];
		end_pos = current_pos + width;
		graphic_pos = graphic++; // Takes value before we increment

		if (row_on_graphic >= num_bytes_tall) {
			break; // If only drawing that remains is final row of graphic...
		}

		while (current_pos < end_pos) {
			// Cleverly read 2 bytes in one go. Doesn't really speed things up. I should try addressing display
			// vertically, so I can do 32 bits in one go on both the graphic and the display...
			uint32_t data = *(uint16_t*)graphic_pos;
			*current_pos |= data >> y_offset_negative;

			//*current_pos |= (*graphic_pos >> y_offset_negative) | (*(graphic_pos + 1) << y_offset);
			current_pos++;
			graphic_pos += num_bytes_tall;
		}
	}

	// Final row
	while (current_pos < end_pos) {
		*current_pos |= (*graphic_pos >> y_offset_negative);
		current_pos++;
		graphic_pos += num_bytes_tall;
	}
}

void Canvas::draw_icon(const Icon& icon, int32_t x, int32_t y, bool reversed) {
	draw_graphic_multi_line(icon.data, x, y, icon.width, icon.height, icon.num_bytes_tall, reversed);
}

void Canvas::draw_icon_centered(const Icon& icon, int32_t start_x, int32_t total_width, int32_t y, bool reversed) {
	int32_t padding = (total_width - icon.width) >> 1;
	draw_icon(icon, start_x + padding, y, reversed);
}

/// Draw a screen title and underline it.
///
/// @param text Title text
void Canvas::draw_screen_title(std::string_view title, bool draw_separator) {
	constexpr int32_t extra_y = 1;
	const int32_t start_y = extra_y + geometry_.visible_top;
	// Was kScreenTitleSeparatorY (= 12 + OLED_MAIN_TOPMOST_PIXEL), which is geometry, not a constant.
	const int32_t separator_y = 12 + geometry_.visible_top;

	draw_string(title, 0, start_y, kTextTitleSpacingX, kTextTitleSizeY);

	if (draw_separator) {
		draw_horizontal_line(separator_y, 0, geometry_.width - 1);
	}
}

void Canvas::invert_area(int32_t x_min, int32_t width, int32_t start_y, int32_t end_y) {
	int32_t x_max = x_min + width - 1;
	if (!clip_rect(x_min, start_y, x_max, end_y)) {
		return;
	}
	width = x_max - x_min + 1;

	int32_t first_row_y = start_y >> 3;
	int32_t last_row_y = end_y >> 3;

	uint8_t current_row_mask = (255 << (start_y & 7));
	uint8_t last_row_mask = (255 >> (7 - (end_y & 7)));

	// For each row
	for (int32_t row_y = first_row_y; row_y <= last_row_y; row_y++) {

		if (row_y == last_row_y) {
			current_row_mask &= last_row_mask;
		}

		uint8_t* __restrict__ current_pos = &bytes_[row_y * geometry_.width + x_min];
		uint8_t* const end_pos = current_pos + width;

		while (current_pos < end_pos) {
			*current_pos ^= current_row_mask;
			current_pos++;
		}

		current_row_mask = 0xFF;
	}
}

void Canvas::invert_area_rounded(int32_t x_min, int32_t width, int32_t start_y, int32_t end_y, BorderRadius radius,
                                 bool rounded) {
	invert_area(x_min, width, start_y, end_y);

	if (!rounded) {
		return;
	}

	const int32_t radius_pixels = radius == SMALL ? 1 : 2;

	// restore corners back
	const int32_t x_max = x_min + width - 1;

	if (radius_pixels == 1) {
		// For 1px radius, clear just the corner pixels
		clear_pixel(x_min, start_y); // Top-left corner
		clear_pixel(x_max, start_y); // Top-right corner
		clear_pixel(x_min, end_y);   // Bottom-left corner
		clear_pixel(x_max, end_y);   // Bottom-right corner
	}
	else if (radius_pixels == 2) {
		// For 2px radius, clear 3 pixels per corner
		// Top-left corner
		clear_pixel(x_min, start_y);
		clear_pixel(x_min + 1, start_y);
		clear_pixel(x_min, start_y + 1);

		// Top-right corner
		clear_pixel(x_max, start_y);
		clear_pixel(x_max - 1, start_y);
		clear_pixel(x_max, start_y + 1);

		// Bottom-left corner
		clear_pixel(x_min, end_y);
		clear_pixel(x_min + 1, end_y);
		clear_pixel(x_min, end_y - 1);

		// Bottom-right corner
		clear_pixel(x_max, end_y);
		clear_pixel(x_max - 1, end_y);
		clear_pixel(x_max, end_y - 1);
	}
}
