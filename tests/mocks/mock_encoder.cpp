#include "hid/encoder.h"
#include "processing/engines/audio_engine.h"

Encoder::Encoder() {
	encPos = 0;
	detentPos = 0;
	encLastChange = 0;
	pinALastSwitch = 1;
	pinBLastSwitch = 1;
	pinALastRead = 1;
	pinBLastRead = 1;
	doDetents = true;
	valuesNow[0] = true;
	valuesNow[1] = true;
}
