#include "definitions_cxx.hpp"
#include "io/midi/midi_device_manager.h"

namespace TuningSysex {
void sysexReceived(MIDICable& cable, uint8_t* data, int32_t len);
uint8_t calculateChecksum(int32_t len);
void bulkDump(MIDICable& cable, uint8_t preset);
void noteChange(MIDICable& cable, uint8_t* data, int32_t len);
void bankDump(MIDICable& cable, uint8_t* data, int32_t len);
void bankNoteChange(MIDICable& cable, uint8_t* data, int32_t len);
void scaleOctave1(MIDICable& cable, uint8_t* data, int32_t len);
void scaleOctave2(MIDICable& cable, uint8_t* data, int32_t len);

typedef struct {
	uint8_t semitone;
	struct {
		uint8_t msb;
		uint8_t lsb;
	} cents;
} frequency_t;

typedef struct {
	uint8_t preset;
	char name[16];
	frequency_t frequency_data[128];
	uint8_t chksum;
} bulk_dump_reply_t;

typedef struct {
	uint8_t preset;
	uint8_t len;
	struct {
		uint8_t key;
		frequency_t frequency_data;
	} data[128];
} single_note_tuning_change_t;

} // namespace TuningSysex
