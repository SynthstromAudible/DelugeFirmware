#include "util/string.h"
#include "printf.h"
#include "util/lookuptables/lookuptables.h"
#include "util/try.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <string>
#include <system_error>

namespace deluge {
std::expected<char*, std::errc> to_chars(char* first, char* last, float value, int precision) {
	if (first >= last) {
		return std::unexpected{std::errc::no_buffer_space};
	}

	size_t buffer_size = last - first;

	std::array<char, 16> format;
	snprintf_(format.data(), format.size(), "%%.%df", precision);

	int written = snprintf_(first, buffer_size, format.data(), value);
	if (written < 0 || static_cast<size_t>(written) >= buffer_size) {
		std::unexpected{std::errc::no_buffer_space};
	}
	return first + written;
}
} // namespace deluge

namespace deluge::string {
std::string fromInt(int32_t number, size_t min_num_digits) {
	// Convert number to string
	std::string result = std::to_string(number);

	if (result.size() < min_num_digits) {
		result.reserve(min_num_digits);
	}

	// Add leading zeros if needed
	if (result[0] != '-' && result.size() < min_num_digits) {
		result.insert(result.begin(), min_num_digits - result.size(), '0');
	}
	else if (result[0] == '-' && result.size() - 1 < min_num_digits) {
		result.insert(result.begin() + 1, min_num_digits - (result.size() - 1), '0');
	}

	return result;
}

std::string fromFloat(float number, int32_t precision) {
	std::array<char, 64> buffer;
	char* ptr = D_TRY_CATCH(deluge::to_chars(buffer.data(), buffer.data() + buffer.size(), number, precision), error, {
		return std::string{}; // should never happen, 64 characters should be enough to render any float
	});

	return std::string{buffer.data(), ptr};
}

std::string fromSlot(int32_t slot, int32_t subSlot, size_t minNumDigits) {
	std::string buffer = fromInt(slot, minNumDigits);
	if (subSlot != -1) {
		buffer.push_back(static_cast<char>(subSlot + 'A'));
	}
	return buffer;
}

std::string fromNoteCode(int32_t noteCode, size_t* getLengthWithoutDot, bool appendOctaveNo) {
	std::string output; // Guaranteed to be 4 characters or less
	int32_t octave = (noteCode) / 12 - 2;
	int32_t noteCodeWithinOctave = (uint16_t)(noteCode + 120) % (uint8_t)12;

	output += noteCodeToNoteLetter[noteCodeWithinOctave];
	if (noteCodeIsSharp[noteCodeWithinOctave]) {
		output += display->haveOLED() ? '#' : '.';
	}
	if (appendOctaveNo) {
		output += std::to_string(octave);
	}

	if (getLengthWithoutDot != nullptr) {
		*getLengthWithoutDot = noteCodeIsSharp[noteCodeWithinOctave] ? output.length() - 1 : output.length();
	}
	return output;
}

} // namespace deluge::string
