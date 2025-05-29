#pragma once
#include <cstdint>

namespace TuningSysex {

struct cents_t {
	uint8_t msb;
	uint8_t lsb;
};

struct frequency_t {
	uint8_t semitone;
	cents_t cents;
};

struct key_freq_t {
	uint8_t key;
	frequency_t freq;
};

struct cents_1_t {
	uint8_t sval;
};

struct cents_2_t {
	uint8_t msb;
	uint8_t lsb;
};

double cents(frequency_t f);
double cents(cents_t c);
double cents(cents_1_t c);
double cents(cents_2_t c);

} // namespace TuningSysex
