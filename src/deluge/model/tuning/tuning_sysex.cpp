#include "tuning_sysex.h"
#include "io/midi/midi_engine.h"
#include "io/midi/sysex.h"
#include "tuning.h"

void TuningSysex::sysexReceived(MIDICable& cable, uint8_t* data, int32_t len) {
	if (len < 3)
		return;

	uint8_t cmd = data[0];
	data++;
	len--;

	switch (data[0]) {
	case SysEx::TuningCommands::BulkDumpRequest:
		if (len < sizeof(bulk_dump_request_t))
			return;
		bulkDumpRequest(cable, *reinterpret_cast<bulk_dump_request_t*>(data));
		break;

	case SysEx::TuningCommands::BulkDump:
		if (len < sizeof(bulk_dump_t))
			return;
		bulkDump(cable, *reinterpret_cast<bulk_dump_t*>(data));
		break;

	case SysEx::TuningCommands::NoteChange: {
		auto& msg = *reinterpret_cast<single_note_tuning_change_t*>(data);
		if (msg.len > 0x7f || len < 2 + msg.len * (sizeof(key_freq_t)))
			return; // if the message is truncated, ignore
		noteChange(cable, msg);
	} break;

	case SysEx::TuningCommands::BankDumpRequest:
		if (len < sizeof(bulk_dump_request_t))
			return;
		bankDumpRequest(cable, *reinterpret_cast<bank_dump_request_t*>(data));
		break;

	case SysEx::TuningCommands::KeyBasedDump:
		if (len < sizeof(key_based_dump_t))
			return;
		keyBasedDump(cable, *reinterpret_cast<key_based_dump_t*>(data));
		break;

	case SysEx::TuningCommands::ScaleOctaveDump1:
		if (len < sizeof(scale_octave_dump_1_t))
			return;
		scaleOctaveDump1(cable, *reinterpret_cast<scale_octave_dump_1_t*>(data));
		break;

	case SysEx::TuningCommands::ScaleOctaveDump2:
		if (len < sizeof(scale_octave_dump_2_t))
			return;
		scaleOctaveDump2(cable, *reinterpret_cast<scale_octave_dump_2_t*>(data));
		break;

	case SysEx::TuningCommands::BankNoteChange:
		if (len < sizeof(bank_note_change_t))
			return;
		bankNoteChange(cable, *reinterpret_cast<bank_note_change_t*>(data));
		break;

	case SysEx::TuningCommands::ScaleOctave1:
		if (len < sizeof(scale_octave_1_t))
			return;
		scaleOctave1(cable, *reinterpret_cast<scale_octave_1_t*>(data));
		break;

	case SysEx::TuningCommands::ScaleOctave2:
		if (len < sizeof(scale_octave_2_t))
			return;
		scaleOctave2(cable, *reinterpret_cast<scale_octave_2_t*>(data));
		break;

	default:
		break;
	}
}

uint8_t TuningSysex::calculateChecksum(int32_t len) {
	uint8_t chksum = 0;
	auto* buf = midiEngine.sysex_fmt_buffer;
	for (int i = 1; i < len; i++) {
		chksum ^= buf[i];
	}
	chksum &= 0x7F;
	return chksum;
}

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

void TuningSysex::bulkDumpRequest(MIDICable& cable, bulk_dump_request_t& msg) {
	// BulkDumpRequest         00 preset
	// should reply with BulkDump for the given preset
	// BulkDump                01 preset name[16] {xx yy zz}[128]

	uint8_t* buf = midiEngine.sysex_fmt_buffer;
	uint8_t header[5] = {0xF0, 0x7E, 0x00, SysEx::SYSEX_MIDI_TUNING_STANDARD, SysEx::TuningCommands::BulkDump};
	memcpy(buf, header, sizeof(header));
	auto& reply = *reinterpret_cast<bulk_dump_t*>(buf + sizeof(header));

	// auto previous_preset = TuningSystem::selectedTuning;
	TuningSystem::select(msg.preset);
	reply.preset = TuningSystem::selectedTuning;

	strncpy(reply.name, TuningSystem::tuning->name, 16);

	for (int i = 0; i < 128; i++) {
		TuningSystem::tuning->getSysexFrequency(i, reply.freq[i]);
	}
	auto len = sizeof(header) + sizeof(reply);
	reply.chksum = calculateChecksum(len);
	buf[len] = 0xF7;
	cable.sendSysex(buf, len + 1);
	// TuningSystem::select(previous_preset);
}

void TuningSysex::bulkDump(MIDICable& cable, bulk_dump_t& msg) {
	// BulkDump 01               preset name[16] {xx yy zz}[128]
	// should retune all notes to the absolute frequencies in given preset

	if (!TuningSystem::selectForWrite(msg.preset))
		return;

	msg.chksum; // best to avoid direct eye contact with it
	for (int i = 0; i < 128; i++) {
		TuningSystem::tuning->setFrequency(i, msg.freq[i]);
	}
}

void TuningSysex::noteChange(MIDICable& cable, single_note_tuning_change_t& msg) {
	// NoteChange 02             preset len {key xx yy zz}[len]
	// should retune notes to absolute frequencies in given preset

	if (!TuningSystem::selectForWrite(msg.preset))
		return;

	for (int i = 0; i < msg.len; i++) {
		TuningSystem::tuning->setFrequency(msg.key_freq[i].key, msg.key_freq[i].freq);
	}
}

void TuningSysex::bankDumpRequest(MIDICable& cable, bank_dump_request_t& msg) {
	// BankDump 03                bank preset
	// should reply with one of the following messages for the given bank and preset
	// KeyBasedDump 04            bank preset name[16] {xx yy zz}[128] csum
	// ScaleOctaveDump 05         bank preset name[16] ss[12] csum
	// ScaleOctaveDump 06         bank preset name[16] {ss tt}[12] csum

	// reply with KeyBasedDump
	uint8_t* buf = midiEngine.sysex_fmt_buffer;
	uint8_t header[5] = {0xF0, 0x7E, 0x00, SysEx::SYSEX_MIDI_TUNING_STANDARD, SysEx::TuningCommands::KeyBasedDump};
	memcpy(buf, header, sizeof(header));
	auto& reply = *reinterpret_cast<key_based_dump_t*>(buf + sizeof(header));

	// auto previous_preset = TuningSystem::selectedTuning;
	TuningSystem::select(msg.preset);
	reply.preset = TuningSystem::selectedTuning;
	reply.bank = msg.bank; // echo back since songs have one bank of many presets

	strncpy(reply.name, TuningSystem::tuning->name, 16);

	for (int i = 0; i < 128; i++) {
		TuningSystem::tuning->getSysexFrequency(i, reply.freq[i]);
	}
	auto len = sizeof(header) + sizeof(reply);
	reply.chksum = calculateChecksum(len);
	buf[len] = 0xF7;
	cable.sendSysex(buf, len + 1);
	// TuningSystem::select(previous_preset);
}

void TuningSysex::keyBasedDump(MIDICable& cable, key_based_dump_t& msg) {
	// BulkDump 04               bank preset name[16] {xx yy zz}[128] csum
	// should retune all notes to the absolute frequencies in given bank and preset

	if (!TuningSystem::selectForWrite(msg.preset))
		return;

	for (int i = 0; i < 128; i++) {
		TuningSystem::tuning->setFrequency(i, msg.freq[i]);
	}
}

void TuningSysex::scaleOctaveDump1(MIDICable& cable, scale_octave_dump_1_t& msg) {
	// ScaleOctaveDump1          05 bank preset name[16] {ss}[12]
	// should retune the octave for the specified channels with 7-bit precision cents in given bank and preset

	if (!TuningSystem::selectForWrite(msg.preset))
		return;

	TuningSystem::tuning->setup(msg.name);
	for (int i = 0; i < 12; i++) {
		TuningSystem::tuning->setCents(i, cents(msg.cents[i]));
	}
}

void TuningSysex::scaleOctaveDump2(MIDICable& cable, scale_octave_dump_2_t& msg) {
	// ScaleOctaveDump2          06 bank preset name[16] {ss tt}[12]
	// should retune the octave for the specified channels with 14-bit precision cents in given bank and preset

	if (!TuningSystem::selectForWrite(msg.preset))
		return;

	TuningSystem::tuning->setup(msg.name);
	for (int i = 0; i < 12; i++) {
		TuningSystem::tuning->setCents(i, cents(msg.cents[i]));
	}
}

void TuningSysex::bankNoteChange(MIDICable& cable, bank_note_change_t& msg) {
	// BankNoteChange          07 bank preset len {key xx yy zz}[len]
	// should retune notes to absolute frequencies in given bank and preset

	if (!TuningSystem::selectForWrite(msg.preset))
		return;

	for (int i = 0; i < msg.len; i++) {
		TuningSystem::tuning->setFrequency(msg.key_freq[i].key, msg.key_freq[i].freq);
	}
}

void TuningSysex::scaleOctave1(MIDICable& cable, scale_octave_1_t& msg) {
	// ScaleOctave1            08 ff gg hh ss[12]
	// should retune the octave for the specified channels with 7-bit precision cents

	for (int i = 0; i < 12; i++) {
		TuningSystem::tuning->setCents(i, cents(msg.cents[i]));
	}
}

void TuningSysex::scaleOctave2(MIDICable& cable, scale_octave_2_t& msg) {
	// ScaleOctave2            09 ff gg hh {ss tt}[12]
	// should retune the octave for the specified channels with 14-bit precision cents

	for (int i = 0; i < 12; i++) {
		TuningSystem::tuning->setCents(i, cents(msg.cents[i]));
	}
}
