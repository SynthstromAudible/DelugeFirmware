#include "modulation/lfo.h"

uint32_t getLFOInitialPhaseForNegativeExtreme(LFOType wave_type) {
	switch (wave_type) {
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

uint32_t getLFOInitialPhaseForZero(LFOType wave_type) {
	switch (wave_type) {
	case LFOType::TRIANGLE:
		return 1073741824;

	default:
		return 0;
	}
}

void LFO::emplaceWave(LFOType type) {
	// Force a fresh wave of the requested type — its accumulators (hold/target/speed) start zeroed.
	// Order mirrors the LFOType enum so wave_.index() == static_cast<size_t>(type).
	switch (type) {
	case LFOType::SINE:
		wave_.emplace<lfo_wave::Sine>();
		break;
	case LFOType::TRIANGLE:
		wave_.emplace<lfo_wave::Triangle>();
		break;
	case LFOType::SQUARE:
		wave_.emplace<lfo_wave::Square>();
		break;
	case LFOType::SAW:
		wave_.emplace<lfo_wave::Saw>();
		break;
	case LFOType::SAMPLE_AND_HOLD:
		wave_.emplace<lfo_wave::SampleHold>();
		break;
	case LFOType::RANDOM_WALK:
		wave_.emplace<lfo_wave::RandomWalk>();
		break;
	case LFOType::WARBLER:
		wave_.emplace<lfo_wave::Warbler>();
		break;
	}
}

// LFO2 always starts from the negative extreme (it triggers per note-on), but LFO1
// has historically waveform-specific behaviour when synced.

void LFO::setLocalInitialPhase(const LFOConfig& config) {
	emplaceWave(config.waveType); // fresh wave → accumulators zeroed (matches a per-note-on reset)
	phase_ = getLFOInitialPhaseForNegativeExtreme(config.waveType);
}

void LFO::setGlobalInitialPhase(const LFOConfig& config) {
	emplaceWave(config.waveType);
	if (config.waveType == LFOType::SINE || config.waveType == LFOType::TRIANGLE) {
		phase_ = getLFOInitialPhaseForZero(config.waveType);
	}
	else {
		phase_ = getLFOInitialPhaseForNegativeExtreme(config.waveType);
	}
}
