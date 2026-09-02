#include "ui/oled/sample_position.h"

#include "gfx/oled/font.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace deluge::ui::oled {

namespace {

/// @brief `intToString(number, buffer, min_digits)` (`foundation/string_format.c:35`), locally.
///
/// `src/ui` may include neither `foundation/` nor `util/` (`ui-includes-outside`), and this is the
/// only thing either was wanted for. Non-negative only -- every value on this screen is a sample
/// position or a unit count.
/// @param number     Value to write.
/// @param buffer     At least 12 bytes.
/// @param min_digits Pad with leading zeros to at least this many digits.
/// @return The number of characters written, not counting the terminator.
size_t int_to_chars(uint32_t number, char* buffer, int32_t min_digits) {
	size_t digits = 1;
	for (uint32_t probe = number; probe >= 10; probe /= 10) {
		digits++;
	}
	if (digits < static_cast<size_t>(min_digits)) {
		digits = static_cast<size_t>(min_digits);
	}
	buffer[digits] = 0;
	for (size_t pos = digits; pos-- > 0;) {
		buffer[pos] = static_cast<char>('0' + (number % 10));
		number /= 10;
	}
	return digits;
}

} // namespace

void sample_position(OledFrame& f, gfx::oled::Rect rect, uint32_t samples, uint32_t sample_rate) {
	const int32_t sx = gfx::oled::kTextSpacingX;
	const int32_t sy = gfx::oled::kTextSpacingY;

	int32_t x = rect.x;
	const int32_t y = rect.y;

	if (sample_rate != 0) {
		// Legacy's variable name, kept because the unit is genuinely surprising: this is
		// hundred-thousandths of a second (10 microseconds), not hundreds of milliseconds
		// (`sample_marker_editor.cpp:1142`).
		uint64_t hundredmilliseconds = static_cast<uint64_t>(samples) * 100000 / sample_rate;
		uint32_t hours = 0;
		uint32_t minutes = 0;

		// `hundredmilliseconds < 6000000` is legacy's `goto printSeconds`, which skips the
		// `if (hundredmilliseconds)` test below -- so a position under a minute always draws a
		// seconds figure, sample 0 included ("0.00 msec").
		bool print_seconds = true;

		if (hundredmilliseconds >= 6000000) {
			minutes = static_cast<uint32_t>(hundredmilliseconds / 6000000);
			hundredmilliseconds -= static_cast<uint64_t>(minutes) * 6000000;

			char buffer[12];

			if (minutes >= 60) {
				hours = minutes / 60;
				minutes -= hours * 60;

				const size_t length = int_to_chars(hours, buffer, 1);
				f.glyphs(std::string_view{buffer, length}, x, y, sx, sy);
				x += static_cast<int32_t>(length) * sx;

				// DIVERGENCE FROM LEGACY, deliberate: `sample_marker_editor.cpp:1158` passes
				// `smallTextSpacingX` as the *x coordinate* of this glyph, not `xPixel` -- so on
				// every position past an hour legacy draws the 'h' back at x = 6, on top of the
				// hours digits, and the same at `:1167` for the 'm'. It is a transcription slip
				// (the argument that should be `xPixel` happens to have a plausible name), not a
				// behaviour anything depends on, and reproducing it would mean shipping a
				// knowingly-corrupt readout into a new UI. Positions under a minute -- which is
				// every marker on a typical sample -- are unaffected, and the port and legacy are
				// pixel-identical there.
				f.glyph('h', x, y, sx, sy);
				x += sx * 2;
			}

			const size_t length = int_to_chars(minutes, buffer, 1);
			f.glyphs(std::string_view{buffer, length}, x, y, sx, sy);
			x += static_cast<int32_t>(length) * sx;

			f.glyph('m', x, y, sx, sy); // Same divergence as the 'h' above.
			x += sx * 2;

			print_seconds = hundredmilliseconds != 0;
		}

		if (print_seconds) {
			int32_t decimal_places;

			// Maybe we just want millisecond resolution (that's S with 3 decimal places)...
			if (hours != 0 || minutes != 0 || hundredmilliseconds >= 100000) {
				hundredmilliseconds /= 100;
				decimal_places = 3;
			}
			// Or, display milliseconds with 2 decimal places - very fine resolution.
			else {
				decimal_places = 2;
			}

			// `intToString` to at least `decimal_places + 1` digits, then push the last
			// `decimal_places` of them one place right and drop a '.' into the gap
			// (`sample_marker_editor.cpp:1190-1193`).
			char buffer[14];
			const size_t length = int_to_chars(static_cast<uint32_t>(hundredmilliseconds), buffer, decimal_places + 1);
			const size_t point = length - static_cast<size_t>(decimal_places);
			for (size_t i = length + 1; i > point; i--) {
				buffer[i] = buffer[i - 1];
			}
			buffer[point] = '.';

			f.glyphs(std::string_view{buffer, length + 1}, x, y, sx, sy);
			x += static_cast<int32_t>(length + 1) * sx;

			if (hours != 0 || minutes != 0) {
				f.glyph('s', x, y, sx, sy);
			}
			else {
				x += sx;
				f.glyphs(decimal_places == 2 ? std::string_view{"msec"} : std::string_view{"sec"}, x, y, sx, sy);
			}
		}
	}

	// Sample count. Legacy resets `xPixel` to its initial 1 here (`:1211`), which is `rect.x`.
	const int32_t count_y = y + kSamplePositionLineGap;
	x = rect.x;

	f.glyph('(', x, count_y, sx, sy);
	x += sx;

	char buffer[12];
	const size_t length = int_to_chars(samples, buffer, 1);
	f.glyphs(std::string_view{buffer, length}, x, count_y, sx, sy);
	x += sx * static_cast<int32_t>(length + 1);

	f.glyphs(std::string_view{"smpl)"}, x, count_y, sx, sy);
}

} // namespace deluge::ui::oled
