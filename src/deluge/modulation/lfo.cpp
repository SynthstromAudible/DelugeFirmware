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

void LFO::setInitialPhase(const LFOConfig& config) {
	// Historically LFO1 didn't do much initial phase setting unless it was sync'ed.
	// LFO2 on the other hand didn't support sync, and always went for the negative
	// extreme regardless of wave type.
	//
	// This logic matches the historical behaviour for synced LFO1 and unsynced LFO2,
	// and makes synced LFO2 work the same as synced LFO1.
	if (config.syncLevel == SYNC_LEVEL_NONE) {
		phase = getLFOInitialPhaseForNegativeExtreme(config.waveType);
	}
	else if (config.waveType == LFOType::SINE || config.waveType == LFOType::TRIANGLE) {
		phase = getLFOInitialPhaseForZero(config.waveType);
	}
	else {
		phase = getLFOInitialPhaseForNegativeExtreme(config.waveType);
	}
}
