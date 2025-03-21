/**
 * Copyright (c) 2025 Katherine Whitlock
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

#include "dsp/core/generator.h"
#include "dsp/core/periodic.h"
#include <cstdint>

class ClassicOscillator : public SIMDGenerator<int32_t> {
	Periodic<Argon<uint32_t>> periodic_component_; ///< The periodic component of the oscillator
	/// @note this can't be inherited due to being a SIMDGenerator<uint32_t>, which makes the return types for derived
	/// class' render() functions non-matching
public:
	constexpr ClassicOscillator() = default; ///< Default constructor
	constexpr ClassicOscillator(uint32_t phase, uint32_t increment) { setPhaseAndIncrement(phase, increment); }

	/// @brief Set the phase of the oscillator.
	/// @param phase The new phase value to set.
	constexpr void setPhaseAndIncrement(uint32_t phase, uint32_t increment) {
		periodic_component_.setPhase(Argon{phase}.MultiplyAdd(Argon<uint32_t>{1U, 2U, 3U, 4U}, increment));
		periodic_component_.setPhaseIncrement(increment * 4);
	};

	/// @brief Advance the phase of the oscillator by one step.
	constexpr void advance() { periodic_component_.advance(); }

	/// @brief Get the current phase of the oscillator.
	/// @return The current phase value.
	[[nodiscard]] constexpr Argon<uint32_t> getPhase() const { return periodic_component_.getPhase(); }
};

class SimpleOscillatorFor final : public ClassicOscillator {
	using FunctionType = Argon<q31_t> (*)(Argon<uint32_t>);
	FunctionType func_;

public:
	SimpleOscillatorFor(FunctionType func) : func_(func) {}
	Argon<q31_t> render() override {
		Argon<q31_t> output = func_(getPhase());
		this->advance();
		return output;
	}
};

class PWMOscillator : public SIMDGenerator<int32_t> {
	uint32_t pulse_width_ = 0x80000000; ///< The width of the pulse in the PWM waveform
public:
	/// @brief Get the current phase width of the PWM oscillator.
	[[nodiscard]] constexpr uint32_t getPulseWidth() const { return pulse_width_; }

	/// @brief Set the phase width of the PWM oscillator.
	void setPulseWidth(uint32_t width) { pulse_width_ = width; }
};
