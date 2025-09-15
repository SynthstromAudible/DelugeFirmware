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

// BulkDumpRequest 00        preset
// should reply with BulkDump for the given preset
// BulkDump 01               preset name[16] {xx yy zz}[128]
struct bulk_dump_request_t {
	uint8_t preset;
};

// BulkDump 01               preset name[16] {xx yy zz}[128]
// should retune all notes to the absolute frequencies in given preset
struct bulk_dump_t {
	uint8_t preset;
	char name[16];
	frequency_t freq[128];
	uint8_t chksum;
};

// NoteChange 02             preset len {key xx yy zz}[len]
// should retune notes to absolute frequencies in given preset
struct single_note_tuning_change_t {
	uint8_t preset;
	uint8_t len;
	key_freq_t key_freq[128];
};

// BankDump 03               bank preset
// should reply with one of the following messages for the given bank and preset
// KeyBasedDump 04           bank preset name[16] {xx yy zz}[128] csum
// ScaleOctaveDump1 05       bank preset name[16] ss[12] csum
// ScaleOctaveDump2 06       bank preset name[16] {ss tt}[12] csum
struct bank_dump_request_t {
	uint8_t bank;
	uint8_t preset;
};

// KeyBasedDump 04           bank preset name[16] {xx yy zz}[128] csum
// should retune all notes to the absolute frequencies in given bank and preset
struct key_based_dump_t {
	uint8_t bank;
	uint8_t preset;
	char name[16];
	frequency_t freq[128];
	uint8_t chksum;
};

// ScaleOctaveDump1 05       bank preset name[16] {ss}[12]
// should retune the octave for the specified channels with 7-bit precision cents in given bank and preset
struct scale_octave_dump_1_t {
	uint8_t bank;
	uint8_t preset;
	char name[16];
	cents_1_t cents[12];
	uint8_t chksum;
};

// ScaleOctaveDump2 06       bank preset name[16] {ss tt}[12]
// should retune the octave for the specified channels with 14-bit precision cents in given bank and preset
struct scale_octave_dump_2_t {
	uint8_t bank;
	uint8_t preset;
	char name[16];
	cents_2_t cents[12];
	uint8_t chksum;
};

// BankNoteChange            bank preset len {key xx yy zz}[len]
// should retune notes to absolute frequencies in given bank and preset
struct bank_note_change_t {
	uint8_t bank;
	uint8_t preset;
	uint8_t len;
	key_freq_t key_freq[128];
};

// ScaleOctave1 08           ff gg hh ss[12]
// should retune the octave for the specified channels with 7-bit precision cents
struct scale_octave_1_t {
	uint8_t ch_mask[3];
	cents_1_t cents[12];
};

// ScaleOctave2 09           ff gg hh {ss tt}[12]
// should retune the octave for the specified channels with 14-bit precision cents
struct scale_octave_2_t {
	uint8_t ch_mask[3];
	cents_2_t cents[12];
};

union midi_tuning_t {
	bulk_dump_request_t bulk_dump_request;
	bulk_dump_t bulk_dump;
	single_note_tuning_change_t single_note_tuning_change;
	bank_dump_request_t bank_dump_request;
	key_based_dump_t key_based_dump;
	scale_octave_dump_1_t scale_octave_dump_1;
	scale_octave_dump_2_t scale_octave_dump_2;
	bank_note_change_t bank_note_change;
	scale_octave_1_t scale_octave_1;
	scale_octave_2_t scale_octave_2;
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
