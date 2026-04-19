#include "midi_engine_mocks.h"
#include <cstdint>

void MIDICable::sendSysex(uint8_t* buf, uint32_t len) {
	memcpy(buffer, buf, len);
	printf("ok\n");
}
