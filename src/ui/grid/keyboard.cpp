#include "ui/grid/keyboard.h"

#include <algorithm>

namespace deluge::ui::grid {

namespace {

/// `keyboardChars` (gui/ui/qwerty_ui.cpp:180-203), transcribed verbatim -- three layouts, five
/// rows, eleven columns. Row 0 is the digit row (`home_row + 2`) and row 4 the space bar
/// (`home_row - 2`), because legacy indexes it as `kQwertyHomeRow - y + 2`.
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

/// How many columns of \p f the keyboard owns: legacy clears and draws only the main pads
/// (qwerty_ui.cpp:76 -> pad_leds.cpp:236-240).
int32_t keyboard_cols(const GridFrame& f) {
	return std::min<int32_t>(f.bounds().w, kTextKeyboardCols);
}

} // namespace

bool text_keyboard_is_shift_pad(int32_t x, int32_t y, int32_t home_row) {
	// qwerty_ui.cpp:284-287 (read) / :117-118 (painted).
	return y == home_row - 1 && ((x >= 1 && x < 3) || (x >= 13 && x < 15));
}

char text_keyboard_char_at(KeyboardLayoutKind layout, int32_t x, int32_t y, int32_t home_row, bool shift) {
	if (y == home_row + 2 && x >= 14 && x < 16) {
		return kTextKeyboardBackspace; // :205
	}
	if (y == home_row && x >= 14 && x < 16) {
		return kTextKeyboardEnter; // :232
	}
	// :255 -- outside this rectangle legacy's padAction does nothing at all.
	if (!(x >= 3 && x < 14 && y >= home_row - 2 && y <= home_row + 2)) {
		return 0;
	}

	const auto index = static_cast<uint8_t>(layout);
	if (index >= kNumKeyboardLayoutKinds) {
		return 0;
	}
	char newChar = kKeyboardChars[index][home_row - y + 2][x - 3]; // :267-268
	if (newChar == 0) {
		return 0; // :269
	}

	// :283-302 -- shift only rewrites four keys, and only on the digit row.
	if (shift && y == home_row + 2) {
		switch (newChar) {
		case '-':
			return '_'; // :290-291
		case '1':
			return '!'; // :293-294
		case '3':
			return '#'; // :296-297
		case '6':
			return '^'; // :299-300
		default:
			break;
		}
	}
	return newChar;
}

std::optional<RGB> text_keyboard_key_colour(int32_t x, int32_t y, int32_t home_row) {
	// Ordered last-write-first: `drawKeys` overwrites some pads, and the last write wins.
	if (text_keyboard_is_shift_pad(x, y, home_row)) {
		return gui::colours::blue; // :117-118
	}
	if (y == home_row && x >= 14 && x < 16) {
		return gui::colours::green; // :114
	}
	if (y == home_row + 2 && x >= 14 && x < 16) {
		return gui::colours::red; // :111
	}
	if (y == home_row - 2 && x >= 5 && x < 11) {
		return RGB::monochrome(160); // :105-107
	}
	if (y == home_row) {
		if (x == 6 || x == 9) {
			return RGB::monochrome(160); // :98-99
		}
		if ((x >= 3 && x <= 5) || (x >= 10 && x <= 12)) {
			return RGB::monochrome(64); // :95-97, :100-102
		}
		if (x >= 3 && x < 14) {
			return RGB::monochrome(10); // :86 -- x = 7, 8, 13, left as the loop drew them
		}
		return std::nullopt;
	}
	if (y == home_row + 2) {
		if (x == 13) {
			return RGB::monochrome(10); // :92 -- the '-' key, dimmer than the digits
		}
		if (x >= 3 && x < 13) {
			return RGB::monochrome(64); // :83
		}
		return std::nullopt;
	}
	if (y == home_row + 1 && x >= 3 && x < 13) {
		return RGB::monochrome(10); // :84
	}
	if (y == home_row - 1 && x >= 3 && x < 12) {
		return RGB::monochrome(10); // :88
	}
	return std::nullopt;
}

Response text_keyboard(GridFrame& f, TextKeyboardState& state, KeyboardLayoutKind layout, int32_t home_row) {
	const int32_t w = keyboard_cols(f);
	const int32_t h = f.bounds().h;

	Response out;
	int32_t pressed_x = -1;
	int32_t pressed_y = -1;
	bool shift_held = false;

	for (int32_t y = 0; y < h; y++) {
		for (int32_t x = 0; x < w; x++) {
			// One pass: `clearMainPadsWithoutSending` (:76) plus every `drawKeys` write, since a
			// pad the map has no colour for is exactly a pad legacy leaves cleared.
			const Response r = f.pad(x, y, text_keyboard_key_colour(x, y, home_row).value_or(gui::colours::black));

			if ((r.held || r.pressed) && text_keyboard_is_shift_pad(x, y, home_row)) {
				shift_held = true; // :284-287
			}
			if (r.pressed && pressed_x < 0) {
				pressed_x = x;
				pressed_y = y;
				out.pressed = true;
			}
			out.released = out.released || r.released;
			out.held = out.held || r.held;
		}
	}

	state.shift = shift_held;
	if (pressed_x >= 0) {
		out.velocity = static_cast<uint8_t>(text_keyboard_char_at(layout, pressed_x, pressed_y, home_row, state.shift));
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
