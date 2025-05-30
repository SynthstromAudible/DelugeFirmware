#include "tuning_sysex.h"
#include "tuning.h"

#ifndef IN_UNIT_TESTS
#include "io/midi/sysex.h"
uint8_t* TuningSysex::sysex_fmt_buffer = midiEngine.sysex_fmt_buffer;
#else
uint8_t fmt_buf[1024];
uint8_t* TuningSysex::sysex_fmt_buffer = fmt_buf;
#endif

void TuningSysex::sysexReceived(MIDICable& cable, uint8_t* data, int32_t len) {

	// F0 7E 7F 08 ...
	uint8_t cmd = data[4];
	data += 4;
	len -= 4;

	if (len < 3)
		return;

	auto& msg = *reinterpret_cast<midi_tuning_t*>(data);

	switch (cmd) {
	case SysEx::TuningCommands::BulkDumpRequest:
		if (len >= sizeof(bulk_dump_request_t))
			bulkDumpRequest(cable, msg.bulk_dump_request);
		break;

	case SysEx::TuningCommands::BulkDump:
		if (len >= sizeof(bulk_dump_t))
			bulkDump(cable, msg.bulk_dump);
		break;

	case SysEx::TuningCommands::NoteChange: {
		auto ll = msg.single_note_tuning_change.len;
		if (ll <= 0x7f && len >= 2 + ll * (sizeof(key_freq_t)))
			noteChange(cable, msg.single_note_tuning_change);
	} break;

	case SysEx::TuningCommands::BankDumpRequest:
		if (len >= sizeof(bank_dump_request_t))
			bankDumpRequest(cable, msg.bank_dump_request);
		break;

	case SysEx::TuningCommands::KeyBasedDump:
		if (len >= sizeof(key_based_dump_t))
			keyBasedDump(cable, msg.key_based_dump);
		break;

	case SysEx::TuningCommands::ScaleOctaveDump1:
		if (len >= sizeof(scale_octave_dump_1_t))
			scaleOctaveDump1(cable, msg.scale_octave_dump_1);
		break;

	case SysEx::TuningCommands::ScaleOctaveDump2:
		if (len >= sizeof(scale_octave_dump_2_t))
			scaleOctaveDump2(cable, msg.scale_octave_dump_2);
		break;

	case SysEx::TuningCommands::BankNoteChange:
		if (len >= sizeof(bank_note_change_t))
			bankNoteChange(cable, msg.bank_note_change);
		break;

	case SysEx::TuningCommands::ScaleOctave1:
		if (len >= sizeof(scale_octave_1_t))
			scaleOctave1(cable, msg.scale_octave_1);
		break;

	case SysEx::TuningCommands::ScaleOctave2:
		if (len >= sizeof(scale_octave_2_t))
			scaleOctave2(cable, msg.scale_octave_2);
		break;

	default:
		break;
	}
}

uint8_t TuningSysex::calculateChecksum(int32_t len) {
	uint8_t chksum = 0;
	auto* buf = sysex_fmt_buffer;
	for (int i = 1; i < len; i++) {
		chksum ^= buf[i];
	}
	chksum &= 0x7F;
	return chksum;
}

void TuningSysex::bulkDumpRequest(MIDICable& cable, bulk_dump_request_t& msg) {

	uint8_t* buf = sysex_fmt_buffer;
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

	if (!TuningSystem::selectForWrite(msg.preset))
		return;

	msg.chksum; // best to avoid direct eye contact with it
	for (int i = 0; i < 128; i++) {
		TuningSystem::tuning->setFrequency(i, msg.freq[i]);
	}
}

void TuningSysex::noteChange(MIDICable& cable, single_note_tuning_change_t& msg) {

	if (!TuningSystem::selectForWrite(msg.preset))
		return;

	for (int i = 0; i < msg.len; i++) {
		TuningSystem::tuning->setFrequency(msg.key_freq[i].key, msg.key_freq[i].freq);
	}
}

void TuningSysex::bankDumpRequest(MIDICable& cable, bank_dump_request_t& msg) {

	// reply with KeyBasedDump
	uint8_t* buf = sysex_fmt_buffer;
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

	if (!TuningSystem::selectForWrite(msg.preset))
		return;

	for (int i = 0; i < 128; i++) {
		TuningSystem::tuning->setFrequency(i, msg.freq[i]);
	}
}

void TuningSysex::scaleOctaveDump1(MIDICable& cable, scale_octave_dump_1_t& msg) {

	if (!TuningSystem::selectForWrite(msg.preset))
		return;

	TuningSystem::tuning->setup(msg.name);
	for (int i = 0; i < 12; i++) {
		TuningSystem::tuning->setCents(i, cents(msg.cents[i]));
	}
}

void TuningSysex::scaleOctaveDump2(MIDICable& cable, scale_octave_dump_2_t& msg) {

	if (!TuningSystem::selectForWrite(msg.preset))
		return;

	TuningSystem::tuning->setup(msg.name);
	for (int i = 0; i < 12; i++) {
		TuningSystem::tuning->setCents(i, cents(msg.cents[i]));
	}
}

void TuningSysex::bankNoteChange(MIDICable& cable, bank_note_change_t& msg) {

	if (!TuningSystem::selectForWrite(msg.preset))
		return;

	for (int i = 0; i < msg.len; i++) {
		TuningSystem::tuning->setFrequency(msg.key_freq[i].key, msg.key_freq[i].freq);
	}
}

void TuningSysex::scaleOctave1(MIDICable& cable, scale_octave_1_t& msg) {

	for (int i = 0; i < 12; i++) {
		TuningSystem::tuning->setCents(i, cents(msg.cents[i]));
	}
}

void TuningSysex::scaleOctave2(MIDICable& cable, scale_octave_2_t& msg) {

	for (int i = 0; i < 12; i++) {
		TuningSystem::tuning->setCents(i, cents(msg.cents[i]));
	}
}
