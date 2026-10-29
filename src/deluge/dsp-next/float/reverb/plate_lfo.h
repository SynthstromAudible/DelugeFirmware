// Cosine oscillator after Emilie Gillet (Copyright 2014 Emilie Gillet, MIT License): a cosine between 0 and 1 with
// minimal CPU use.

/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "argon.hpp"
#include "definitions_cxx.hpp"
#include "dsp-next/core/kernel.h"

#include <array>
#include <cstddef>
#include <type_traits>

/// @file
/// @brief The plate reverb's LFO pair, as a control kernel.
namespace deluge::dsp::fp {

/// @brief The plate's two LFOs, as a control kernel: an approximate cosine pair on one ArgonHalf<float>, each lane
/// stepped by a two-term recurrence once per kStride frames.
/// @details The owner steps it every kStride frames and reads value() every frame. A recurrence from
/// `start`: y_0 = c.initial_amplitude, y_1 = 0.5; each step y_1' = c × y_1 − y_0, y_0' = y_1.
struct PlateLfo {
	using value_type = ArgonHalf<float>; ///< Both LFOs' values, lane 0 then lane 1, each 0 to 1.
	using step_type = ArgonHalf<float>;  ///< A control kernel's block is its step.
	/// The LFOs step once every kStride frames, so their frequencies are scaled up by it: without that, the
	/// approximate cosine's 2 − 32f² coefficient rounds to exactly 2 and both lanes fuse into one slow oscillation.
	static constexpr std::size_t kStride = 32;

	/// @brief The two frequencies in cycles per frame.
	struct Settings {
		float frequency_1 = 0.5f / static_cast<float>(kSampleRate); ///< 0.5 Hz
		float frequency_2 = 0.3f / static_cast<float>(kSampleRate); ///< 0.3 Hz
	};
	/// @brief The recurrence's coefficient and starting amplitude, per lane.
	struct Coefficients {
		ArgonHalf<float> iir_coefficient{};
		ArgonHalf<float> initial_amplitude{};
	};
	/// @brief The recurrence's two last values, per lane.
	struct State {
		ArgonHalf<float> y_0{};
		ArgonHalf<float> y_1{};
	};

	/// @return The approximate cosine's coefficient for each strided frequency: the reflection into the first
	/// quarter-cycle and the ±16 sign, then sign × f × (1 − 2f), the arithmetic of DualCosineOscillator's
	/// approximation.
	[[nodiscard]] static Coefficients coefficients(const Settings& s) {
		// The frequencies strided, then the approximate cosine's coefficient for each
		const std::array<float, 2> strided{s.frequency_1 * static_cast<float>(kStride),
		                                   s.frequency_2 * static_cast<float>(kStride)};
		ArgonHalf<float> sign = 16.0f;
		ArgonHalf<float> frequencies = ArgonHalf<float>::Load(strided.data());
		frequencies = frequencies.each_lane_with_index([&](float& frequency, int i) {
			frequency -= 0.25f;
			if (frequency < 0.0f) {
				frequency = -frequency;
			}
			else if (frequency > 0.5f) {
				frequency -= 0.5f;
			}
			else {
				sign[i] = -16.0f;
			}
		});
		Coefficients c;
		c.iir_coefficient = sign * frequencies * (1.0f - (2.0f * frequencies));
		c.initial_amplitude = c.iir_coefficient * 0.25f;
		return c;
	}
	/// @brief Steps the recurrence once per kStride frames of @p n (the owner passes kStride).
	/// @return The values after the step.
	[[gnu::always_inline]] static ArgonHalf<float> advance(const Coefficients& c, State& s, std::size_t n) {
		for (std::size_t i = 0; i < n / kStride; ++i) {
			const ArgonHalf<float> temp = s.y_1;
			s.y_1 = c.iir_coefficient * s.y_1 - s.y_0;
			s.y_0 = temp;
		}
		return value(s);
	}
	/// @return Both LFOs' current values, y_0 + 0.5 on the vector, each lane 0 to 1.
	[[nodiscard]] [[gnu::always_inline]] static ArgonHalf<float> value(const State& s) { return s.y_0 + 0.5f; }
	/// @brief Event: the recurrence at its start, y_0 at the starting amplitude and y_1 at 0.5.
	static void start(const Coefficients& c, State& s) {
		s.y_0 = c.initial_amplitude;
		s.y_1 = 0.5f;
	}
};

static_assert(control_kernel<PlateLfo>);
static_assert(!std::is_polymorphic_v<PlateLfo>);

} // namespace deluge::dsp::fp
