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
#include "definitions_cxx.hpp"
#include "dsp/core/generator.h"
#include "types.h"

namespace impl {
template <typename PhaseType, typename IncrementType = PhaseType>
class PeriodicState {
	PhaseType phase_ = 0;               ///< Current phase of the oscillator
	IncrementType phase_increment_ = 0; ///< Increment value for the phase, typically (1 / sample_rate) * frequency

public:
	constexpr PeriodicState(PhaseType phase, IncrementType phase_increment)
	    : phase_{phase}, phase_increment_{phase_increment} {} ///< Constructor to initialize phase and step
	constexpr PeriodicState(IncrementType phase_increment)
	    : phase_increment_{phase_increment} {} ///< Constructor to initialize only the step, phase defaults to 0
	constexpr PeriodicState(Frequency frequency)
	    : phase_increment_{IncrementType((1.f / kSampleRate) * frequency)} {
	} ///< Constructor to initialize step based on frequency
	constexpr PeriodicState() = default;          ///< Default constructor
	constexpr virtual ~PeriodicState() = default; ///< Default destructor

	[[nodiscard]] constexpr PhaseType getPhase() const { return phase_; } ///< Getter for the current phase
	[[nodiscard]] constexpr IncrementType getPhaseIncrement() const {
		return phase_increment_;
	} ///< Getter for the current step

	/// @brief Set the phase and step values
	constexpr void setPhase(PhaseType new_phase) { phase_ = new_phase; }
	constexpr void setPhaseIncrement(IncrementType new_phase_increment) { phase_increment_ = new_phase_increment; }
};
} // namespace impl

template <typename T>
struct Periodic : impl::PeriodicState<T>, Generator<T> {
	using impl::PeriodicState<T>::PeriodicState;

	[[nodiscard]] T render() override {
		auto new_phase = this->phase_ + this->phase_increment_;
		new_phase = (new_phase >= T(1)) ? new_phase - T(1) : new_phase;
		return new_phase;
	}
	void advance() { this->setPhase(Periodic::render()); }
};

template <>
struct Periodic<uint32_t> : impl::PeriodicState<uint32_t>, Generator<uint32_t> {
	using impl::PeriodicState<uint32_t>::PeriodicState;

	constexpr Periodic(Frequency frequency)
	    : impl::PeriodicState<uint32_t>{std::bit_cast<uint32_t>(FixedPoint<31>((1.f / kSampleRate) * frequency).raw())
	                                    << 1} {}
	[[nodiscard]] uint32_t render() override { return getPhase() + getPhaseIncrement(); }
	void advance() { setPhase(Periodic::render()); }
};

template <typename T>
struct Periodic<Argon<T>> : impl::PeriodicState<Argon<T>, T>, SIMDGenerator<T> {
	using impl::PeriodicState<Argon<T>, T>::PeriodicState;

	[[nodiscard]] Argon<T> render() override {
		auto new_phase = this->phase_ + (this->phase_increment_ * Argon<T>::lanes);
		new_phase = argon::ternary(new_phase >= T(1), new_phase - T(1), new_phase);
		return new_phase;
	}
	void advance() { setPhase(Periodic::render()); }
};

template <>
struct Periodic<Argon<uint32_t>> : impl::PeriodicState<Argon<uint32_t>, uint32_t>, SIMDGenerator<uint32_t> {
	using impl::PeriodicState<Argon<uint32_t>, uint32_t>::PeriodicState;

	[[nodiscard]] Argon<uint32_t> render() override {
		return getPhase() + (getPhaseIncrement() * Argon<uint32_t>::lanes);
	}
	void advance() { setPhase(Periodic::render()); }
};
