#include "hid/encoder.h"

namespace deluge::hid::encoders {

Encoder::Encoder() {
	encPos = 0;
	detentPos = 0;
	edgeAccumulator = 0;
	doDetents = true;
}

} // namespace deluge::hid::encoders
