#pragma once

#include "definitions_cxx.hpp"
#include "io/midi/midi_device_manager.h"

namespace TuningSysex {
void sysexReceived(MIDICable& cable, uint8_t* data, int32_t len);
uint8_t calculateChecksum(int32_t len);
void bulkDumpRequest(MIDICable& cable, uint8_t preset);
void bulkDump(MIDICable& cable, uint8_t* data, int32_t len);
void noteChange(MIDICable& cable, uint8_t* data, int32_t len);
void bankDumpRequest(MIDICable& cable, uint8_t* data, int32_t len);
void bankNoteChange(MIDICable& cable, uint8_t* data, int32_t len);
void scaleOctave1(MIDICable& cable, uint8_t* data, int32_t len);
void scaleOctave2(MIDICable& cable, uint8_t* data, int32_t len);

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

struct bulk_dump_t {
	uint8_t preset;
	char name[16];
	frequency_t freq[128];
	uint8_t chksum;
};

struct single_note_tuning_change_t {
	uint8_t preset;
	uint8_t len;
	key_freq_t key_freq[128];
};

struct cents_1_t {
	uint8_t sval;
};
struct cents_2_t {
	uint8_t msb;
	uint8_t lsb;
};

struct scale_octave_1_t {
	uint8_t ch_mask[3];
	cents_1_t cents[12];
};

struct scale_octave_2_t {
	uint8_t ch_mask[3];
	cents_2_t cents[12];
};

double cents(frequency_t f);
double cents(cents_t c);
double cents(cents_1_t c);
double cents(cents_2_t c);

} // namespace TuningSysex
