#include "synth/modulation/lfo.h"

namespace {
using deluge::dsp::UQ;

UQ<32> get_lfo_initial_phase_for_negative_extreme(LFOType wave_type) {
	switch (wave_type) {
	case LFOType::SAW:
		return deluge::dsp::uq<32>(0.5); // half a cycle

	case LFOType::SINE:
		return deluge::dsp::uq<32>(0.75); // three quarters of a cycle

	default:
		// This is actually positive extreme for square wave.
		// It is not entirely clear if that should be changed.
		return UQ<32>{};
	}
}

UQ<32> get_lfo_initial_phase_for_zero(LFOType wave_type) {
	switch (wave_type) {
	case LFOType::TRIANGLE:
		return deluge::dsp::uq<32>(0.25); // a quarter of a cycle

	default:
		return UQ<32>{};
	}
}
} // namespace

void LFO::emplace_wave(LFOType type) {
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

void LFO::set_local_initial_phase(const LFOConfig& config) {
	emplace_wave(config.wave_type); // fresh wave → accumulators zeroed (matches a per-note-on reset)
	phase_ = get_lfo_initial_phase_for_negative_extreme(config.wave_type);
}

void LFO::set_global_initial_phase(const LFOConfig& config, bool reset_state) {
	// Clock re-syncs call this repeatedly during playback, so they only realign the phase: the random waveforms
	// keep their held value and filter state. Playback start resets that state too.
	if (reset_state) {
		emplace_wave(config.wave_type);
	}
	else {
		select_wave(config.wave_type);
	}
	if (config.wave_type == LFOType::SINE || config.wave_type == LFOType::TRIANGLE) {
		phase_ = get_lfo_initial_phase_for_zero(config.wave_type);
	}
	else {
		phase_ = get_lfo_initial_phase_for_negative_extreme(config.wave_type);
	}
}
