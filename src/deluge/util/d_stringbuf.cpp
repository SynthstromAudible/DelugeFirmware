#include "d_stringbuf.h"

// NOLINTBEGIN (*-suspicious-stringview-data-usage)
// this is fine, the second parameter in the min is the max we can safely copy and the first is to avoid using
// strcopy on a non null terminated string view
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow="
void StringBuf::append(std::string_view str) {

	::strncat(buf_, str.data(), std::min(capacity_ - size() - 1, str.length()));
}
#pragma GCC diagnostic pop
// NOLINTEND

/**********************************************************************************************************************\
 * String formatting and parsing functions
\**********************************************************************************************************************/

char halfByteToHexChar(uint8_t thisHalfByte) {
	if (thisHalfByte < 10) {
		return 48 + thisHalfByte;
	}
	else {
		return 55 + thisHalfByte;
	}
}

char hexCharToHalfByte(unsigned char hexChar) {
	if (hexChar >= 65) {
		return hexChar - 55;
	}
	else {
		return hexChar - 48;
	}
}

void intToHex(uint32_t number, char* output, int32_t numChars) {
	output[numChars] = 0;
	for (int32_t i = numChars - 1; i >= 0; i--) {
		output[i] = halfByteToHexChar(number & 15);
		number >>= 4;
	}
}

uint32_t hexToInt(char const* string) {
	int32_t output = 0;
	while (*string) {
		output <<= 4;
		output |= hexCharToHalfByte(*string);
		string++;
	}
	return output;
}

// length must be >0
uint32_t hexToIntFixedLength(char const* __restrict__ hexChars, int32_t length) {
	uint32_t output = 0;
	char const* const endChar = hexChars + length;
	do {
		output <<= 4;
		output |= hexCharToHalfByte(*hexChars);
		hexChars++;
	} while (hexChars != endChar);

	return output;
}

// May give wrong result for -2147483648
int32_t stringToInt(char const* __restrict__ string) {
	uint32_t number = 0;
	bool is_negative = (*string == '-');
	if (is_negative) {
		string++;
	}

	while (*string >= '0' && *string <= '9') {
		number *= 10;
		number += (*string - '0');
		string++;
	}

	if (is_negative) {
		if (number >= 2147483648) {
			return -2147483648;
		}
		return -(int32_t)number;
	}
	return static_cast<int32_t>(number);
}

// Scratch buffer shared across the firmware, e.g. for file paths and ordered-array searches. Defined here
// rather than in functions.cpp so host test targets which build the string sources get it without that file.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, readability-identifier-naming)
char miscStringBuffer[kFilenameBufferSize] __attribute__((aligned(CACHE_LINE_SIZE)));
