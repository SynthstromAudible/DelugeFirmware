#include "hid/encoder.h"
#include "hid/encoders.h"
#include "mock_defines.h"
#include "util/misc.h"

namespace deluge::hid::encoders {

Encoder encoders[NUM_ENCODERS] = {};
uint32_t timeModEncoderLastTurned[2];
int8_t modEncoderInitialTurnDirection[2];

uint32_t timeNextSDTestAction = 0;
int32_t nextSDTestDirection = 1;

uint32_t encodersWaitingForCardRoutineEnd;

Encoder& getEncoder(EncoderName which) {
	return encoders[util::to_underlying(which)];
}
} // namespace deluge::hid::encoders
