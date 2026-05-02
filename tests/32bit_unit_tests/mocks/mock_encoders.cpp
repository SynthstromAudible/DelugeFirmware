#include "hid/encoder_input.h"
#include "hid/encoders.h"

namespace deluge::hid::encoders {

DetentedEncoder scrollY;
DetentedEncoder scrollX;
DetentedEncoder tempo;
DetentedEncoder select;
ContinuousEncoder mod0;
ContinuousEncoder mod1;

DetentedEncoder& functionEncoderAt(size_t i) {
	static DetentedEncoder* const table[] = {&scrollY, &scrollX, &tempo, &select};
	return *table[i];
}

ContinuousEncoder& modEncoderAt(size_t i) {
	static ContinuousEncoder* const table[] = {&mod0, &mod1};
	return *table[i];
}

uint32_t timeModEncoderLastTurned[2];

} // namespace deluge::hid::encoders
