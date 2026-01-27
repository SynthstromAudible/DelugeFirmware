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

#include "dsp_ng/components/periodic.hpp"
#include "dsp_ng/core/generator.hpp"
#include "dsp_ng/core/types.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>

namespace deluge::dsp::oscillator {
class LegacyOscillator : public Periodic<Argon<uint32_t>> {
protected:
public:
	constexpr LegacyOscillator() = default; ///< Default constructor
	constexpr LegacyOscillator(uint32_t phase, uint32_t increment) {
		setPhaseIncrement(increment);
		setPhase(phase);
	}

	constexpr void setPhase(uint32_t phase) {
		Periodic::setPhase(Argon{phase}.MultiplyAdd(Argon<uint32_t>::Iota(1), this->getPhaseIncrement()));
	}

	/// @brief Get the number of samples remaining until the next reset of the oscillator.
	/// @param offset An optional offset to include in the calculation, commonly used for phase adjustments.
	/// @return An Argon object containing the number of samples remaining until the next reset.
	[[nodiscard]] constexpr Argon<uint32_t> samplesRemaining() const {
		uint32_t increment = this->getPhaseIncrement();
		Argon<uint32_t> phase = std::numeric_limits<uint32_t>::max() - this->getPhase(); // Remaining phase
		return phase * Argon{increment}.ReciprocalEstimate();
	}
};

class SimpleOscillatorFor final : public LegacyOscillator, public Generator<Argon<q31_t>> {
public:
	using function_type = Argon<q31_t> (*)(Argon<uint32_t>);

	SimpleOscillatorFor(function_type func) : func_(func) {}
	Argon<q31_t> render() final {
		Argon<q31_t> output = func_(Periodic::advance());
		return output;
	}

private:
	function_type func_;
};

class PWMOscillator {
	uint32_t pulse_width_ = 0x80000000; ///< The width of the pulse in the PWM waveform
public:
	/// @brief Get the current phase width of the PWM oscillator.
	[[nodiscard]] constexpr uint32_t getPulseWidth() const { return pulse_width_; }

	/// @brief Set the phase width of the PWM oscillator.
	void setPulseWidth(uint32_t width) { pulse_width_ = width; }
};
} // namespace deluge::dsp::oscillator
