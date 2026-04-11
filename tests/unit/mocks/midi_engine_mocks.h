#pragma once

#include "definitions_cxx.hpp"
#include "io/midi/sysex.h"

class MIDICable {
public:
	uint8_t buffer[1024];
	void sendSysex(uint8_t* buf, uint32_t len);
};
