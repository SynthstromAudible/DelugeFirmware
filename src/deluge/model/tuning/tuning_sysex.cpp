#include "tuning_sysex.h"
#include "io/midi/midi_engine.h"
#include "io/midi/sysex.h"

void TuningSysex::sysexReceived(MIDICable& cable, uint8_t* data, int32_t len) {
	if (len < 3) {
		return;
	}
	switch (data[0]) {
	case SysEx::TuningCommands::BulkDump:
		bulkDump(cable, data[1]);
		break;

	case SysEx::TuningCommands::NoteChange:
		noteChange(cable, data + 1, len - 1);
		break;

	// -------- TODO --------
	case SysEx::TuningCommands::BankDump:
		bankDump(cable, data, len);
		break;

	case SysEx::TuningCommands::BankNoteChange:
		bankNoteChange(cable, data, len);
		break;

	case SysEx::TuningCommands::ScaleOctave1:
		scaleOctave1(cable, data, len);
		break;

	case SysEx::TuningCommands::ScaleOctave2:
		scaleOctave2(cable, data, len);
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

void TuningSysex::bulkDump(MIDICable& cable, uint8_t preset) {
	// BulkDump                00 preset
	// should reply with BulkDumpReply for the given preset
	// BulkDumpReply           01 preset name[16] {xx yy zz}[128]

	uint8_t* reply = midiEngine.sysex_fmt_buffer;
	uint8_t header[5] = {0xF0, 0x7E, 0x00, SysEx::SYSEX_MIDI_TUNING_STANDARD, SysEx::TuningCommands::BulkDumpReply};
	memcpy(reply, header, sizeof(header));
	auto* msg = reinterpret_cast<bulk_dump_reply_t*>(reply + sizeof(header));

	msg->preset = preset;
	strcpy(msg->name, "USER");
	for (int i = 0; i < 128; i++) {
		frequency_t* data = &(msg->frequency_data[i]);
		data->semitone = i;
		data->cents.msb = 0; // TODO: populate with TuningSystem data
		data->cents.lsb = 0;
	}
	uint8_t len = sizeof(header) + sizeof(msg);
	msg->chksum = calculateChecksum(len);
	reply[len] = 0xF7;
	cable.sendSysex(reply, len + 1);
}

void TuningSysex::noteChange(MIDICable& cable, uint8_t* data, int32_t len) {
	// NoteChange 02             preset len {key xx yy zz}[len]
	// should retune notes to absolute frequencies in given preset

	if (len < 7) {
		return;
	}
	auto* msg = reinterpret_cast<single_note_tuning_change_t*>(data);
	if (len < 2 + msg->len * (sizeof(frequency_t) + 1)) {
		return;
	}
}

void TuningSysex::bankDump(MIDICable& cable, uint8_t* data, int32_t len) {
	// BankDump 03                bank preset
	// should reply with one of the following messages for the given bank and preset
	// KeyBasedDumpReply 04       bank preset name[16] {xx yy zz}[128] csum
	// ScaleOctaveDumpReply1 05   bank preset name[16] ss[12] csum
	// ScaleOctaveDumpReply2 06   bank preset name[16] {ss tt}[12] csum

	// TODO
}

void TuningSysex::bankNoteChange(MIDICable& cable, uint8_t* data, int32_t len) {
	// BankNoteChange          07 bank preset len {key xx yy zz}[len]
	// should retune notes to absolute frequencies in given bank and preset
}

void TuningSysex::scaleOctave1(MIDICable& cable, uint8_t* data, int32_t len) {
	// ScaleOctave1            08 ff gg hh ss[12]
	// should retune the octave for the specified channels with 7-bit precision cents

	// TODO
}

void TuningSysex::scaleOctave2(MIDICable& cable, uint8_t* data, int32_t len) {
	// ScaleOctave2            09 ff gg hh {ss tt}[12]
	// should retune the octave for the specified channels with 14-bit precision cents

	// TODO
}
