#include "hid/encoder.h"
#include "hid/encoders.h"
#include "mock_defines.h"
namespace Encoders {

Encoder encoders[NUM_ENCODERS] = {};
uint32_t timeModEncoderLastTurned[2];
int8_t modEncoderInitialTurnDirection[2];

uint32_t timeNextSDTestAction = 0;
int32_t nextSDTestDirection = 1;

uint32_t encodersWaitingForCardRoutineEnd;
} // namespace Encoders
