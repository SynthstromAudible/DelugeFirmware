#include "ui/grid/keyboard.h"

#include <algorithm>

namespace deluge::ui::grid {

namespace {

/// The keyboard character table -- three layouts, five rows, eleven columns. Row 0 is the digit
/// row (`home_row + 2`) and row 4 the space bar (`home_row - 2`); a lookup indexes it as
/// `home_row - y + 2`.
constexpr char kKeyboardChars[kNumKeyboardLayoutKinds][5][11] = {
    {
        // QWERTY
        {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-'},
        {'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0},
        {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 0, '\''},
        {'Z', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', 0, 0},
        {0, 0, ' ', ' ', ' ', ' ', ' ', ' ', 0, 0, 0},
    },
    {
        // AZERTY
        {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-'},
        {'A', 'Z', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0},
        {'Q', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'M', '\''},
        {'W', 'X', 'C', 'V', 'B', 'N', ',', '.', 0, 0, 0},
        {0, 0, ' ', ' ', ' ', ' ', ' ', ' ', 0, 0, 0},
    },
    {
        // QWERTZ
        {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-'},
        {'Q', 'W', 'E', 'R', 'T', 'Z', 'U', 'I', 'O', 'P', 'U'},
        {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'O', 'A'},
        {'Y', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', '\'', 0},
        {0, 0, ' ', ' ', ' ', ' ', ' ', ' ', 0, 0, 0},
    },
};

// How many columns of `f` the keyboard owns: only the main pads are cleared and drawn, never the
// sidebar.
int32_t keyboard_cols(const GridFrame& f) {
	return std::min<int32_t>(f.bounds().w, kTextKeyboardCols);
}

} // namespace

bool text_keyboard_is_shift_pad(int32_t x, int32_t y, int32_t home_row) {
	return y == home_row - 1 && ((x >= 1 && x < 3) || (x >= 13 && x < 15));
}

char text_keyboard_char_at(KeyboardLayoutKind layout, int32_t x, int32_t y, int32_t home_row, bool shift) {
	if (y == home_row + 2 && x >= 14 && x < 16) {
		return kTextKeyboardBackspace;
	}
	if (y == home_row && x >= 14 && x < 16) {
		return kTextKeyboardEnter;
	}
	// Outside this rectangle, no pad types anything.
	if (!(x >= 3 && x < 14 && y >= home_row - 2 && y <= home_row + 2)) {
		return 0;
	}

	const auto index = static_cast<uint8_t>(layout);
	if (index >= kNumKeyboardLayoutKinds) {
		return 0;
	}
	char newChar = kKeyboardChars[index][home_row - y + 2][x - 3];
	if (newChar == 0) {
		return 0;
	}

	// Shift only rewrites four keys, and only on the digit row.
	if (shift && y == home_row + 2) {
		switch (newChar) {
		case '-':
			return '_';
		case '1':
			return '!';
		case '3':
			return '#';
		case '6':
			return '^';
		default:
			break;
		}
	}
	return newChar;
}

std::optional<RGB> text_keyboard_key_colour(int32_t x, int32_t y, int32_t home_row) {
	// Ordered last-write-first: later checks would overwrite earlier ones if a pad matched both.
	if (text_keyboard_is_shift_pad(x, y, home_row)) {
		return gui::colours::blue;
	}
	if (y == home_row && x >= 14 && x < 16) {
		return gui::colours::green;
	}
	if (y == home_row + 2 && x >= 14 && x < 16) {
		return gui::colours::red;
	}
	if (y == home_row - 2 && x >= 5 && x < 11) {
		return RGB::monochrome(160);
	}
	if (y == home_row) {
		if (x == 6 || x == 9) {
			return RGB::monochrome(160);
		}
		if ((x >= 3 && x <= 5) || (x >= 10 && x <= 12)) {
			return RGB::monochrome(64);
		}
		if (x >= 3 && x < 14) {
			return RGB::monochrome(10); // x = 7, 8, 13
		}
		return std::nullopt;
	}
	if (y == home_row + 2) {
		if (x == 13) {
			return RGB::monochrome(10); // the '-' key, dimmer than the digits
		}
		if (x >= 3 && x < 13) {
			return RGB::monochrome(64);
		}
		return std::nullopt;
	}
	if (y == home_row + 1 && x >= 3 && x < 13) {
		return RGB::monochrome(10);
	}
	if (y == home_row - 1 && x >= 3 && x < 12) {
		return RGB::monochrome(10);
	}
	return std::nullopt;
}

Response text_keyboard(GridFrame& f, TextKeyboardState& state, KeyboardLayoutKind layout, int32_t home_row) {
	const int32_t w = keyboard_cols(f);
	const int32_t h = f.bounds().h;

	Response out;
	int32_t pressed_x = -1;
	int32_t pressed_y = -1;
	uint8_t pressed_velocity = 0;
	bool shift_held = false;

	for (int32_t y = 0; y < h; y++) {
		for (int32_t x = 0; x < w; x++) {
			// One pass: painting and clearing collapsed together, since a pad the map has no
			// colour for is exactly a pad that should be left cleared.
			const Response r = f.pad(x, y, text_keyboard_key_colour(x, y, home_row).value_or(gui::colours::black));

			if ((r.held || r.pressed) && text_keyboard_is_shift_pad(x, y, home_row)) {
				shift_held = true;
			}
			if (r.pressed) {
				out.pressed = true;
				// The *typing* press, not merely the first press in scan order: a pad outside the
				// key rectangle (a shift pad, say) simply does nothing and leaves the key pressed
				// in the same tick free to type. Testing with shift = false is safe here: the shift
				// rewrites map non-zero to non-zero and never produce or consume a 0, so "types a
				// character" does not depend on the shift state that is still being gathered by
				// this very sweep.
				if (pressed_x < 0 && text_keyboard_char_at(layout, x, y, home_row, false) != 0) {
					pressed_x = x;
					pressed_y = y;
					pressed_velocity = r.velocity;
				}
			}
			out.released = out.released || r.released;
			out.held = out.held || r.held;
		}
	}

	state.shift = shift_held;
	if (pressed_x >= 0) {
		// The typed character travels in `index` (ui/core/response.h); `velocity` stays the key
		// pad's own press velocity, so a caller can have both without decoding one from the other.
		out.index = static_cast<int16_t>(text_keyboard_char_at(layout, pressed_x, pressed_y, home_row, state.shift));
		out.velocity = pressed_velocity;
	}
	return out;
}

void copy_legacy_image(GridFrame& f, const LegacyImage& image) {
	const int32_t w = std::min<int32_t>(f.bounds().w, kLegacyImageCols);
	const int32_t h = std::min<int32_t>(f.bounds().h, kLegacyImageRows);
	for (int32_t y = 0; y < h; y++) {
		for (int32_t x = 0; x < w; x++) {
			f.pad(x, y, image[y][x]);
		}
	}
}

void copy_legacy_column(GridFrame& f, int32_t x, const LegacyImage& image) {
	if (x < 0 || x >= kLegacyImageCols) {
		return;
	}
	const int32_t h = std::min<int32_t>(f.bounds().h, kLegacyImageRows);
	for (int32_t y = 0; y < h; y++) {
		f.pad(x, y, image[y][x]);
	}
}

} // namespace deluge::ui::grid
