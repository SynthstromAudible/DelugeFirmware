#include "hid/encoder.h"
#include "hid/encoder_input.h"
#include "hid/encoders.h"
#include "mock_defines.h"
#include "util/misc.h"

namespace deluge::hid::encoders {

std::array<DetentedEncoder, NUM_FUNCTION_ENCODERS> functionEncoders = {};
std::array<ContinuousEncoder, 2> modEncoders = {};
uint32_t timeModEncoderLastTurned[2];

DetentedEncoder& getFunctionEncoder(EncoderName which) {
	return functionEncoders[util::to_underlying(which)];
}

ContinuousEncoder& getModEncoder(int index) {
	return modEncoders[index];
}
} // namespace deluge::hid::encoders
