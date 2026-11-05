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
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/bandlimited/waves.h" // square, triangle
#include "dsp-next/fixed/noise/cong.h"        // the noise stream the random waves draw from
#include "dsp-next/fixed/osc/sine_table.h"    // kSineTable
#include "plugin-api/types/sync.h"            // SyncType, SyncLevel
#include <cstdint>
#include <variant>

/// The preset-level configuration of one LFO: its waveform plus, optionally, how it locks to the transport.
/// Owned by the app (one per LFO slot on a Sound) and passed into LFO::render — the LFO itself is otherwise
/// configuration-free.
class LFOConfig {
public:
	LFOConfig()
	    : wave_type(LFOType::TRIANGLE), sync_type(deluge::plugin::SyncType::EVEN),
	      sync_level(deluge::plugin::SyncLevel::NONE) {}
	LFOConfig(LFOType type)
	    : wave_type(type), sync_type(deluge::plugin::SyncType::EVEN), sync_level(deluge::plugin::SyncLevel::NONE) {}
	LFOType wave_type;
	deluge::plugin::SyncType sync_type;
	deluge::plugin::SyncLevel sync_level;

	bool operator==(const LFOConfig&) const = default;
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
/// @param phase_increment per-sample phase step (freq·2^32/sampleRate).
/// @param num_samples     samples in this render block.
[[gnu::always_inline]] inline void advance(deluge::dsp::UQ<32>& phase, deluge::dsp::UQ<32> phase_increment,
                                           int32_t num_samples) {
	phase = deluge::dsp::advanced(phase, phase_increment, static_cast<uint32_t>(num_samples));
}

/// @brief Whether a block of @p num_samples from @p phase crosses a period boundary: the phase wraps past 2^32.
[[gnu::always_inline]] inline bool wraps(deluge::dsp::UQ<32> phase, deluge::dsp::UQ<32> phase_increment,
                                         int32_t num_samples) {
	return deluge::dsp::advanced(phase, phase_increment, static_cast<uint32_t>(num_samples)) < phase;
}

/// @brief A random word from @p noise as a full-scale value.
[[gnu::always_inline]] inline deluge::dsp::Q<31> random_value(deluge::dsp::fixed::Cong& noise) {
	return deluge::dsp::Q<31>::from_raw(static_cast<int32_t>(noise.next()));
}

/// Rising ramp: the phase itself, reinterpreted as a signed sweep from −1 to +1.
struct Saw {
	[[gnu::always_inline]] static deluge::dsp::Q<31> render(deluge::dsp::UQ<32>& phase,
	                                                        deluge::dsp::UQ<32> phase_increment, int32_t num_samples,
	                                                        [[maybe_unused]] deluge::dsp::fixed::Cong& noise) {
		const auto value = deluge::dsp::reinterpret_bits<deluge::dsp::Q<31>>(phase); // the phase read as signed
		advance(phase, phase_increment, num_samples);
		return value;
	}
};

struct Square {
	[[gnu::always_inline]] static deluge::dsp::Q<31> render(deluge::dsp::UQ<32>& phase,
	                                                        deluge::dsp::UQ<32> phase_increment, int32_t num_samples,
	                                                        [[maybe_unused]] deluge::dsp::fixed::Cong& noise) {
		// The square's table steps the phase's word
		const auto value = deluge::dsp::Q<31>::from_raw(deluge::dsp::fixed::square(phase.raw(), 2147483648u));
		advance(phase, phase_increment, num_samples);
		return value;
	}
};

struct Sine {
	[[gnu::always_inline]] static deluge::dsp::Q<31> render(deluge::dsp::UQ<32>& phase,
	                                                        deluge::dsp::UQ<32> phase_increment, int32_t num_samples,
	                                                        [[maybe_unused]] deluge::dsp::fixed::Cong& noise) {
		// The sine table reads the phase's word
		const auto value = deluge::dsp::Q<31>::from_raw(deluge::dsp::fixed::kSineTable.value(phase.raw()));
		advance(phase, phase_increment, num_samples);
		return value;
	}
};

struct Triangle {
	[[gnu::always_inline]] static deluge::dsp::Q<31> render(deluge::dsp::UQ<32>& phase,
	                                                        deluge::dsp::UQ<32> phase_increment, int32_t num_samples,
	                                                        [[maybe_unused]] deluge::dsp::fixed::Cong& noise) {
		// The triangle folds the phase's word
		const auto value = deluge::dsp::Q<31>::from_raw(deluge::dsp::fixed::triangle(phase.raw()));
		advance(phase, phase_increment, num_samples);
		return value;
	}
};

/// Sample-and-hold: latch a fresh random value at each period boundary, then hold it steady until the next.
struct SampleHold {
	deluge::dsp::Q<31> held_value{};
	[[gnu::always_inline]] deluge::dsp::Q<31> render(deluge::dsp::UQ<32>& phase, deluge::dsp::UQ<32> phase_increment,
	                                                 int32_t num_samples,
	                                                 [[maybe_unused]] deluge::dsp::fixed::Cong& noise) {
		bool new_period = (phase == deluge::dsp::UQ<32>{}) || wraps(phase, phase_increment, num_samples);
		const deluge::dsp::Q<31> value = new_period ? (held_value = random_value(noise)) : held_value;
		advance(phase, phase_increment, num_samples);
		return value;
	}
};

/// Random walk: at each period boundary, step the held value by a random amount that is biased back towards
/// zero modulation — the further it has wandered from zero, the stronger the pull home — then hold.
struct RandomWalk {
	deluge::dsp::Q<31> held_value{};
	[[gnu::always_inline]] deluge::dsp::Q<31> render(deluge::dsp::UQ<32>& phase, deluge::dsp::UQ<32> phase_increment,
	                                                 int32_t num_samples,
	                                                 [[maybe_unused]] deluge::dsp::fixed::Cong& noise) {
		// The walk spans ±(range/2) per step; full modulation depth is reached after several steps.
		uint32_t range = 4294967295u / 20;
		if (phase == deluge::dsp::UQ<32>{}) {
			// First period: a plain centred random value, no previous value to bias against. Raw: an integer draw
			held_value = deluge::dsp::Q<31>::from_raw(static_cast<int32_t>((range / 2) - noise.next() % range));
		}
		else if (wraps(phase, phase_increment, num_samples)) {
			// (held_value / -16) is the homing bias. Its effect at the extremes:
			//   held_value ==  8*range => bias + range/2 == 0       => next value <= current (forced down)
			//   held_value ==  0       => bias + range/2 == range/2 => equal chance up or down
			//   held_value == -8*range => bias + range/2 == range   => next value >= current (forced up)
			// Raw: the step is an integer draw in the held value's units; the sum saturates.
			const auto step = static_cast<int32_t>((held_value.raw() / -16) + (range / 2) - noise.next() % range);
			held_value = add_sat(deluge::dsp::Q<31>::from_raw(step), held_value);
		}
		advance(phase, phase_increment, num_samples);
		return held_value;
	}
};

/// Warbler: a smooth random curve. It runs at *double* phase rate and, at each (doubled) period boundary,
/// picks a new random target. Rather than filtering the distance to the target, it filters the *rate* of
/// approach (a second-order response), so the curve eases out — zero derivative — at both ends.
struct Warbler {
	deluge::dsp::Q<31> held_value{};
	deluge::dsp::Q<31> target{};
	int32_t speed = 0;
	[[gnu::always_inline]] deluge::dsp::Q<31> render(deluge::dsp::UQ<32>& phase, deluge::dsp::UQ<32> phase_increment,
	                                                 int32_t num_samples,
	                                                 [[maybe_unused]] deluge::dsp::fixed::Cong& noise) {
		phase_increment = scale_shl_wrap<1>(phase_increment); // the warbler sweeps at twice the configured rate
		if (wraps(phase, phase_increment, num_samples)) {
			target = random_value(noise);
			speed = 0;
		}
		// Saturating: a target more than full scale away is still the way to go
		const deluge::dsp::Q<31> distance_to_target = sub_sat(target, held_value);
		// The distance scaled by the increment's top 24 bits, read as a UQ<32> fraction: the speed's change per
		// sample, in the held value's units
		const int32_t pull = mul(scale_shr_floor<8>(phase_increment), distance_to_target).raw();
		// Raw: the speed is an integer product of the block's length and the pull, wrapping
		speed = speed + (num_samples * pull);
		held_value = add_sat(held_value, deluge::dsp::Q<31>::from_raw(speed));
		advance(phase, phase_increment, num_samples);
		return held_value;
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
	/// extreme), resetting the waveform's accumulators.
	void set_local_initial_phase(const LFOConfig& config);
	/// Set the phase for a transport re-sync (global LFOs). The waveform's accumulators are kept, so a
	/// sample-and-hold keeps holding across re-syncs, unless @p reset_state (playback start / song swap).
	void set_global_initial_phase(const LFOConfig& config, bool reset_state = false);

	/// Render one block of @p config's wave (see the LFOType overload).
	[[gnu::always_inline]] deluge::dsp::Q<31> render(int32_t num_samples, const LFOConfig& config,
	                                                 deluge::dsp::UQ<32> phase_increment,
	                                                 deluge::dsp::fixed::Cong& noise) {
		return render(num_samples, config.wave_type, phase_increment, noise);
	}

	/// Render one block, returning the LFO's value for it.
	/// @param phase_increment per-sample phase step (freq·2^32/sampleRate). The phase accumulates
	///        `phase_increment * num_samples` across the block.
	/// @param noise The noise stream the random waves (sample-and-hold, random walk, warbler) draw from.
	[[gnu::always_inline]] deluge::dsp::Q<31> render(int32_t num_samples, LFOType wave_type,
	                                                 deluge::dsp::UQ<32> phase_increment,
	                                                 deluge::dsp::fixed::Cong& noise) {
		select_wave(wave_type);
		return std::visit([&](auto& wave) { return wave.render(phase_, phase_increment, num_samples, noise); }, wave_);
	}

	/// Advance the phase without producing output (used while rendering is skipped). Waveform-agnostic.
	void tick(int32_t num_samples, deluge::dsp::UQ<32> phase_increment) {
		// Note: if this overflows and we're using SAMPLE_AND_HOLD or RANDOM_WALK, the next render() won't know
		// it has overflown. Probably not an issue.
		lfo_wave::advance(phase_, phase_increment, num_samples);
	}

	[[nodiscard]] deluge::dsp::UQ<32> phase() const { return phase_; }
	void set_phase(deluge::dsp::UQ<32> phase) { phase_ = phase; }
	/// Nudge the phase forward (e.g. to align a synced global LFO to the transport position).
	void advance_phase(deluge::dsp::UQ<32> delta) { phase_ = add_wrap(phase_, delta); }

private:
	// Force a fresh waveform of `type`, resetting its accumulators (note-on / re-sync).
	void emplace_wave(LFOType type);
	// Reseat the waveform only on an actual type change, so SAMPLE_AND_HOLD / RANDOM_WALK / WARBLER state
	// persists across same-type render blocks.
	void select_wave(LFOType type) {
		if (wave_.index() != static_cast<std::size_t>(type)) {
			emplace_wave(type);
		}
	}

	deluge::dsp::UQ<32> phase_{};
	// Alternatives are ordered to match the LFOType enum, so wave_.index() == static_cast<size_t>(LFOType).
	std::variant<lfo_wave::Sine, lfo_wave::Triangle, lfo_wave::Square, lfo_wave::Saw, lfo_wave::SampleHold,
	             lfo_wave::RandomWalk, lfo_wave::Warbler>
	    wave_{};
};
