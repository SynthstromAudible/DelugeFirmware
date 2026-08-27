#include "ui/core/text_wrap.h"

#include "ui/core/metrics.h"

#include <cstddef>

namespace deluge::ui {

namespace {

using gfx::oled::char_spacing_px;
using gfx::oled::char_width_px;

/// Legacy walked a NUL-terminated `char const*` and tested `*space == 0` for the end of the
/// string. A `string_view` has no terminator, so "one past the end" reads as `'\0'` here and
/// every legacy `== 0` test keeps working unchanged. An embedded NUL inside the view therefore
/// ends the wrap early, exactly as it would have ended legacy's C string.
char at(std::string_view text, size_t i) {
	return i < text.size() ? text[i] : '\0';
}

/// `addCharacterToLine` (hid/display/oled.cpp:404-430), verbatim. Returns false when the
/// character just added pushed the line past \p max_width_px -- note that the character is
/// counted into \p line_width / \p line_length either way, and the caller unwinds.
bool add_character_to_line(char c, int32_t max_width_px, int32_t text_height, int32_t& line_width, int32_t& line_length,
                           int32_t& char_spacing) {
	line_width += char_width_px(static_cast<uint8_t>(c), text_height);
	// Kept even though the popup font is monospaced, as legacy's comment says (oled.cpp:414-415).
	char_spacing = char_spacing_px(static_cast<uint8_t>(c), text_height, false);
	line_width += char_spacing;
	line_length++;
	return line_width <= max_width_px;
}

/// `findLineSplitPoint` (oled.cpp:441-463), verbatim, returning an index instead of a pointer.
size_t find_line_split_point(std::string_view text, size_t word_start, int32_t max_width_px, int32_t text_height,
                             int32_t& line_width, int32_t& line_length, int32_t& char_spacing) {
	size_t space = word_start;
	while (true) {
		const char c = at(text, space);
		if (c == '\n' || c == ' ' || c == '\0' || c == '_') {
			break; // end of a word
		}
		if (!add_character_to_line(c, max_width_px, text_height, line_width, line_length, char_spacing)) {
			return space; // this character took us over the line width
		}
		space++;
	}

	// The separator that ended the word joins the line it ended (oled.cpp:459-461).
	const char c = at(text, space);
	if (c == '_' || c == ' ') {
		add_character_to_line(c, max_width_px, text_height, line_width, line_length, char_spacing);
	}
	return space;
}

/// `addLine` (oled.cpp:465-484). The cap is checked by the caller *after* this, exactly as
/// legacy does, which is why `count` can reach `kWrapMaxLines` but never exceed it.
void add_line(WrappedLines& out, std::string_view text, size_t line_start, int32_t line_width, int32_t line_length) {
	const size_t start = line_start < text.size() ? line_start : text.size();
	const size_t len =
	    static_cast<size_t>(line_length) < text.size() - start ? static_cast<size_t>(line_length) : text.size() - start;
	out.lines[out.count] = text.substr(start, len);
	out.widths[out.count] = line_width;
	if (line_width > out.longest) {
		out.longest = line_width;
	}
	out.count++;
}

} // namespace

WrappedLines wrap_lines(std::string_view text, int32_t max_width_px, [[maybe_unused]] int32_t max_chars_per_line,
                        gfx::oled::FontId font) {
	// max_chars_per_line is deliberately unused; see the note in text_wrap.h.
	const int32_t text_height = text_height_of(font);

	WrappedLines out{};

	size_t line_start = 0;
	size_t word_start = 0;

	int32_t line_width = 0;  // width in pixels of the line so far
	int32_t line_length = 0; // characters in the line so far
	int32_t line_width_before_this_word = 0;
	int32_t line_length_before_this_word = 0;
	int32_t char_spacing = 0; // pixels added after the last character

	// `breakStringIntoLines` (oled.cpp:494-568); legacy's `goto findNextStringSplitPoint` is this
	// loop, and its two `return`s are the two `break`s.
	while (true) {
		line_width_before_this_word = line_width;
		line_length_before_this_word = line_length;

		const size_t space =
		    find_line_split_point(text, word_start, max_width_px, text_height, line_width, line_length, char_spacing);

		if (line_width <= max_width_px) {
			const char c = at(text, space);
			if (c == '\n' || c == '\0') {
				line_width -= char_spacing; // no gap after the final glyph (oled.cpp:519-521)
				add_line(out, text, line_start, line_width, line_length);
				if (c == '\0' || out.count == kWrapMaxLines) {
					break;
				}
				line_start = space + 1;
				line_width = 0;
				line_length = 0;
				char_spacing = 0;
			}
			word_start = space + 1;
		}
		else {
			line_width_before_this_word -= char_spacing;
			add_line(out, text, line_start, line_width_before_this_word, line_length_before_this_word);
			if (out.count == kWrapMaxLines) {
				break;
			}
			if (at(text, space) == ' ') {
				// A space at the break point is dropped rather than starting the next line.
				word_start = line_start = space + 1;
			}
			else {
				line_start = word_start; // the whole word moves down
			}
			line_width = 0;
			line_length = 0;
			char_spacing = 0;
		}
	}

	return out;
}

} // namespace deluge::ui
