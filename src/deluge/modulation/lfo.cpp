#include "modulation/lfo.h"

uint32_t getLFOInitialPhaseForNegativeExtreme(LFOType waveType) {
	switch (waveType) {
	case LFOType::SAW:
		return 2147483648u;

	case LFOType::SINE:
		return 3221225472u;

	default:
		return 0;
	}
}

uint32_t getLFOInitialPhaseForZero(LFOType waveType) {
	switch (waveType) {
	case LFOType::TRIANGLE:
		return 1073741824;

	default:
		return 0;
	}
}
