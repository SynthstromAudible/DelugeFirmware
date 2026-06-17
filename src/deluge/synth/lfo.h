/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp-next/fixed/bandlimited/waves.h" // square, triangle
#include "dsp-next/fixed/osc/sine_table.h"    // sine_table
#include "modulation/sync.h"
#include "util/fixedpoint.h"
#include "util/waves.h" // the jcong/CONG RNG (random waveforms)
#include <cstdint>
#include <variant>

/// The preset-level configuration of one LFO: its waveform plus, optionally, how it locks to the transport.
/// Owned by the app (one per LFO slot on a Sound) and passed into LFO::render — the LFO itself is otherwise
/// configuration-free.
class LFOConfig {
public:
	LFOConfig() : waveType(LFOType::TRIANGLE), syncType(SYNC_TYPE_EVEN), syncLevel(SYNC_LEVEL_NONE) {}
	LFOConfig(LFOType type) : waveType(type), syncType(SYNC_TYPE_EVEN), syncLevel(SYNC_LEVEL_NONE) {}
	LFOType waveType;
	SyncType syncType;
	SyncLevel syncLevel;
};

/// The individual LFO waveform generators.
///
/// Each generator's @ref render reads the *pre-advance* phase, produces its q31 value, then advances the
/// phase itself — so per-waveform quirks (notably the warbler's double-rate sweep) stay self-contained. The
/// deterministic waves are stateless; the random ones own exactly the accumulator state they carry between
/// periods. The owning @ref LFO holds one of these in a `std::variant` and dispatches with `std::visit`.
///
/// Phase is unsigned UQ0.32: one full waveform period spans the whole `uint32_t` range, and a period boundary
/// is the wrap from near-max back past zero.
namespace lfo_wave {

/// Advance the shared phase accumulator.
/// @param phase_increment per-sample phase step, UQ0.32 (freq·2^32/sampleRate).
/// @param num_samples     samples in this render block.
[[gnu::always_inline]] inline void advance(uint32_t& phase, uint32_t phase_increment, int32_t num_samples) {
	phase += phase_increment * num_samples;
}

/// Rising ramp: the phase itself, reinterpreted as a signed q31 sweep from −1 to +1.
struct Saw {
	[[gnu::always_inline]] int32_t render(uint32_t& phase, uint32_t phase_increment, int32_t num_samples) {
		int32_t value = static_cast<int32_t>(phase);
		advance(phase, phase_increment, num_samples);
		return value;
	}
};

struct Square {
	[[gnu::always_inline]] int32_t render(uint32_t& phase, uint32_t phase_increment, int32_t num_samples) {
		int32_t value = deluge::dsp::fixed::square(phase, 2147483648u);
		advance(phase, phase_increment, num_samples);
		return value;
	}
};

struct Sine {
	[[gnu::always_inline]] int32_t render(uint32_t& phase, uint32_t phase_increment, int32_t num_samples) {
		int32_t value = deluge::dsp::fixed::sine_table.value(phase);
		advance(phase, phase_increment, num_samples);
		return value;
	}
};

struct Triangle {
	[[gnu::always_inline]] int32_t render(uint32_t& phase, uint32_t phase_increment, int32_t num_samples) {
		int32_t value = deluge::dsp::fixed::triangle(phase);
		advance(phase, phase_increment, num_samples);
		return value;
	}
};

/// Sample-and-hold: latch a fresh random value at each period boundary, then hold it steady until the next.
struct SampleHold {
	int32_t held_value_ = 0;
	[[gnu::always_inline]] int32_t render(uint32_t& phase, uint32_t phase_increment, int32_t num_samples) {
		bool new_period = (phase == 0) || (phase + phase_increment * num_samples < phase);
		int32_t value = new_period ? (held_value_ = CONG) : held_value_;
		advance(phase, phase_increment, num_samples);
		return value;
	}
};

/// Random walk: at each period boundary, step the held value by a random amount that is biased back towards
/// zero modulation — the further it has wandered from zero, the stronger the pull home — then hold.
struct RandomWalk {
	int32_t held_value_ = 0;
	[[gnu::always_inline]] int32_t render(uint32_t& phase, uint32_t phase_increment, int32_t num_samples) {
		// The walk spans ±(range/2) per step; full modulation depth is reached after several steps.
		uint32_t range = 4294967295u / 20;
		if (phase == 0) {
			// First period: a plain centred random value, no previous value to bias against.
			held_value_ = (range / 2) - CONG % range;
		}
		else if (phase + phase_increment * num_samples < phase) {
			// (held_value_ / -16) is the homing bias. Its effect at the extremes:
			//   held_value_ ==  8*range => bias + range/2 == 0       => next value <= current (forced down)
			//   held_value_ ==  0       => bias + range/2 == range/2 => equal chance up or down
			//   held_value_ == -8*range => bias + range/2 == range   => next value >= current (forced up)
			held_value_ = add_saturate((held_value_ / -16) + (range / 2) - CONG % range, held_value_);
		}
		advance(phase, phase_increment, num_samples);
		return held_value_;
	}
};

/// Warbler: a smooth random curve. It runs at *double* phase rate and, at each (doubled) period boundary,
/// picks a new random target. Rather than filtering the distance to the target, it filters the *rate* of
/// approach (a second-order response), so the curve eases out — zero derivative — at both ends.
struct Warbler {
	int32_t held_value_ = 0;
	int32_t target_ = 0;
	int32_t speed_ = 0;
	[[gnu::always_inline]] int32_t render(uint32_t& phase, uint32_t phase_increment, int32_t num_samples) {
		phase_increment *= 2; // the warbler sweeps at twice the configured rate
		if (phase + phase_increment * num_samples < phase) {
			target_ = CONG;
			speed_ = 0;
		}
		int32_t distance_to_target = target_ - held_value_;
		speed_ = speed_ + num_samples * (multiply_32x32_rshift32(distance_to_target, phase_increment >> 8));
		held_value_ = add_saturate(held_value_, speed_);
		advance(phase, phase_increment, num_samples);
		return held_value_;
	}
};

} // namespace lfo_wave

/// A low-frequency oscillator: a free-running phase accumulator driving one of the @ref lfo_wave generators,
/// producing a q31 modulation value per render block.
///
/// Decoupled by design — render() takes the phase increment as a plain value, so the LFO knows nothing of
/// Sound/Voice/ParamManager/tempo. The caller derives the increment (from a rate parameter, or from the
/// transport for a synced LFO). The oscillator owns only its UQ0.32 @ref phase_; each random waveform owns
/// its own inter-period state inside the variant.
class LFO {
public:
	LFO() = default;

	/// Re-seat the waveform and set the phase for a fresh note (local LFOs always start at the negative
	/// extreme) or a transport re-sync (global LFOs). Both reset the active waveform's accumulators.
	void setLocalInitialPhase(const LFOConfig& config);
	void setGlobalInitialPhase(const LFOConfig& config);

	[[gnu::always_inline]] int32_t render(int32_t num_samples, const LFOConfig& config, uint32_t phase_increment) {
		return render(num_samples, config.waveType, phase_increment);
	}

	/// Render one block, returning the LFO's q31 value for it.
	/// @param phase_increment per-sample phase step, UQ0.32 (freq·2^32/sampleRate). The phase accumulates
	///        `phase_increment * num_samples` across the block.
	[[gnu::always_inline]] int32_t render(int32_t num_samples, LFOType wave_type, uint32_t phase_increment) {
		selectWave(wave_type);
		return std::visit([&](auto& wave) { return wave.render(phase_, phase_increment, num_samples); }, wave_);
	}

	/// Advance the phase without producing output (used while rendering is skipped). Waveform-agnostic.
	void tick(int32_t num_samples, uint32_t phase_increment) {
		// Note: if this overflows and we're using SAMPLE_AND_HOLD or RANDOM_WALK, the next render() won't know
		// it has overflown. Probably not an issue.
		phase_ += phase_increment * num_samples;
	}

	uint32_t phase() const { return phase_; }
	void setPhase(uint32_t phase) { phase_ = phase; }
	/// Nudge the phase forward (e.g. to align a synced global LFO to the transport position).
	void advancePhase(uint32_t delta) { phase_ += delta; }

private:
	// Force a fresh waveform of `type`, resetting its accumulators (note-on / re-sync).
	void emplaceWave(LFOType type);
	// Reseat the waveform only on an actual type change, so SAMPLE_AND_HOLD / RANDOM_WALK / WARBLER state
	// persists across same-type render blocks.
	void selectWave(LFOType type) {
		if (wave_.index() != static_cast<std::size_t>(type)) {
			emplaceWave(type);
		}
	}

	uint32_t phase_ = 0;
	// Alternatives are ordered to match the LFOType enum, so wave_.index() == static_cast<size_t>(LFOType).
	std::variant<lfo_wave::Sine, lfo_wave::Triangle, lfo_wave::Square, lfo_wave::Saw, lfo_wave::SampleHold,
	             lfo_wave::RandomWalk, lfo_wave::Warbler>
	    wave_{};
};
