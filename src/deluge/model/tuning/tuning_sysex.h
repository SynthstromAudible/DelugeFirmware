#pragma once

#include "definitions_cxx.hpp"

#ifndef IN_UNIT_TESTS
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#else
#include "midi_engine_mocks.h"
#endif

#include "types.h"

namespace TuningSysex {

struct bulk_dump_request_t {
	uint8_t preset;
};
struct bank_dump_request_t {
	uint8_t bank;
	uint8_t preset;
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

struct key_based_dump_t {
	uint8_t bank;
	uint8_t preset;
	char name[16];
	frequency_t freq[128];
	uint8_t chksum;
};

struct scale_octave_dump_1_t {
	uint8_t bank;
	uint8_t preset;
	char name[16];
	cents_1_t cents[12];
	uint8_t chksum;
};

struct scale_octave_dump_2_t {
	uint8_t bank;
	uint8_t preset;
	char name[16];
	cents_2_t cents[12];
	uint8_t chksum;
};

struct bank_note_change_t {
	uint8_t bank;
	uint8_t preset;
	uint8_t len;
	key_freq_t key_freq[128];
};

struct scale_octave_1_t {
	uint8_t ch_mask[3];
	cents_1_t cents[12];
};

struct scale_octave_2_t {
	uint8_t ch_mask[3];
	cents_2_t cents[12];
};

void sysexReceived(MIDICable& cable, uint8_t* data, int32_t len);

uint8_t calculateChecksum(int32_t len);
void bulkDumpRequest(MIDICable& cable, bulk_dump_request_t&);
void bulkDump(MIDICable& cable, bulk_dump_t&);
void noteChange(MIDICable& cable, single_note_tuning_change_t& msg);
void bankDumpRequest(MIDICable& cable, bank_dump_request_t& msg);
void keyBasedDump(MIDICable& cable, key_based_dump_t& msg);
void scaleOctaveDump1(MIDICable& cable, scale_octave_dump_1_t& msg);
void scaleOctaveDump2(MIDICable& cable, scale_octave_dump_2_t& msg);
void bankNoteChange(MIDICable& cable, bank_note_change_t& msg);
void scaleOctave1(MIDICable& cable, scale_octave_1_t& msg);
void scaleOctave2(MIDICable& cable, scale_octave_2_t& msg);

extern uint8_t* sysex_fmt_buffer;

} // namespace TuningSysex
