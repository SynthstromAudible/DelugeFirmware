#pragma once
#include "definitions_cxx.hpp"
#include "util/cfunctions.h"
#include <cstdint>

// Glossary
//
// xx yy zz : absolute frequency in Hz. xx=semitone, yyzz=(100/2^14) cents
// key      : MIDI key number
// len      : length / number of changes
// name     : 7-bit ASCII bytes
// ff gg hh : channel mask as 00000ff 0ggggggg 0hhhhhhh 16-15, 14-8, 7-1
// ss       : relative cents.  -64 to +63, integer step, 0x40 represents equal temperament
// ss tt    : relative cents. -100 to +100, fractional step (100/2^13), 0x40 0x00 represents equal temperament
// csum     : checksum. can be ignored by receiver

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

struct NoteWithinOctave {
	int16_t octave;
	int16_t noteWithin;
	void toString(char* buffer, int32_t* getLengthWithoutDot, bool appendOctaveNo);
};
