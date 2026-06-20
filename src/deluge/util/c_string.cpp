#include "util/c_string.h"

/**********************************************************************************************************************\
 * Hexadecimal formatting and parsing helpers
\**********************************************************************************************************************/

char halfByteToHexChar(uint8_t thisHalfByte) {
	if (thisHalfByte < 10) {
		return 48 + thisHalfByte;
	}
	else {
		return 55 + thisHalfByte;
	}
}

static char hexCharToHalfByte(unsigned char hexChar) {
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
