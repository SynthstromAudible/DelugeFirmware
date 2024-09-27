#include "modulation/lfo.h"

uint32_t getLFOInitialPhaseForNegativeExtreme(LFOType waveType) {
	switch (waveType) {
	case LFOType::SAW:
		return 2147483648u;

	case LFOType::SINE:
		return 3221225472u;

	default:
		// This is actually positive extreme for square wave.
		// It is not entirely clear if that should be changed.
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

// LFO2 always starts from the negative extreme (it triggers per note-on), but LFO1
// has historically waveform-specific behaviour when synced.

void LFO::setLocalInitialPhase(const LFOConfig& config) {
	phase = getLFOInitialPhaseForNegativeExtreme(config.waveType);
	holdValue = 0;
	speed = 0;
	target = 0;
}

void LFO::setGlobalInitialPhase(const LFOConfig& config) {
	if (config.waveType == LFOType::SINE || config.waveType == LFOType::TRIANGLE) {
		phase = getLFOInitialPhaseForZero(config.waveType);
	}
	else {
		phase = getLFOInitialPhaseForNegativeExtreme(config.waveType);
	}
	holdValue = 0;
	speed = 0;
	target = 0;
}
