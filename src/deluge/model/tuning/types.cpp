#include "types.h"

double TuningSysex::cents(cents_t c) {
	int32_t steps = c.lsb & 0x007f;
	steps |= (c.msb & 0x007f) << 7;
	return (double)(100 * steps / 16384.0);
}

double TuningSysex::cents(frequency_t f) {
	return cents(f.cents);
}

double TuningSysex::cents(cents_1_t c1) {
	return c1.sval - 64.0;
}

double TuningSysex::cents(cents_2_t c2) {
	int32_t steps = c2.lsb & 0x007f;
	steps |= (c2.msb & 0x007f) << 7;
	double denom = c2.msb < 0x40 ? 8192.0 : 8191.0; // documentation is inconclusive
	return double(200 * steps / denom) - 100.0;
}
