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
#include "dsp/core/units.h"

namespace deluge::dsp {
namespace impl {
/// @brief Represents the internal state of a periodic generator, such as an oscillator.
/// @tparam PhaseType The type used to represent the phase of the oscillator.
/// @tparam IncrementType The type used to represent the phase increment (default is PhaseType).
template <typename PhaseType, typename IncrementType = PhaseType>
class PeriodicState {
	PhaseType phase_ = 0;               ///< Current phase of the oscillator
	IncrementType phase_increment_ = 0; ///< Increment value for the phase, typically (1 / sample_rate) * frequency

public:
	/// @brief Constructor for the PeriodicState class.
	/// @param phase The initial phase of the oscillator.
	/// @param phase_increment The increment value for the phase.
	constexpr PeriodicState(PhaseType phase, IncrementType phase_increment)
	    : phase_{phase}, phase_increment_{phase_increment} {}

	/// @brief Constructor for the PeriodicState class.
	/// @param phase_increment The increment value for the phase.
	constexpr PeriodicState(IncrementType phase_increment) : phase_increment_{phase_increment} {}

	/// @brief Constructor for the PeriodicState class.
	/// @param frequency The frequency of the oscillator.
	/// @details This constructor calculates the phase increment based on the frequency and sample rate.
	constexpr PeriodicState(Frequency frequency) : phase_increment_{IncrementType((1.f / kSampleRate) * frequency)} {}

	/// @brief Default constructor for the PeriodicState class.
	constexpr PeriodicState() = default;
	constexpr virtual ~PeriodicState() = default;

	[[nodiscard]] constexpr PhaseType getPhase() const { return phase_; }
	constexpr void setPhase(PhaseType new_phase) { phase_ = new_phase; }

	[[nodiscard]] constexpr IncrementType getPhaseIncrement() const { return phase_increment_; }
	constexpr void setPhaseIncrement(IncrementType new_phase_increment) { phase_increment_ = new_phase_increment; }
};
} // namespace impl

/// @brief A periodic signal generator for generic types.
/// @tparam T The type used to represent the phase and phase increment.
template <typename T>
struct Periodic : impl::PeriodicState<T>, Generator<T> {
	using impl::PeriodicState<T>::PeriodicState;

	[[nodiscard]] T render() final {
		auto new_phase = this->phase_ + this->phase_increment_;
		new_phase = (new_phase >= T(1)) ? new_phase - T(1) : new_phase;
		return new_phase;
	}
	void advance() { this->setPhase(Periodic::render()); }
};

/// @brief A specialized periodic signal generator for 32-bit unsigned integers.
template <>
struct Periodic<uint32_t> : impl::PeriodicState<uint32_t>, Generator<uint32_t> {
	using impl::PeriodicState<uint32_t>::PeriodicState;

	constexpr Periodic(Frequency frequency)
	    : impl::PeriodicState<uint32_t>{std::bit_cast<uint32_t>(FixedPoint<31>((1.f / kSampleRate) * frequency).raw())
	                                    << 1} {}
	[[nodiscard]] uint32_t render() final { return getPhase() + getPhaseIncrement(); }
	void advance() { setPhase(Periodic::render()); }
};
/// @brief A SIMD-optimized periodic signal generator for vector types.
/// @tparam T The scalar type used within the vector type.
template <typename T>
struct Periodic<Argon<T>> : impl::PeriodicState<Argon<T>, T>, SIMDGenerator<T> {
	using impl::PeriodicState<Argon<T>, T>::PeriodicState;

	[[nodiscard]] Argon<T> render() final {
		auto new_phase = this->getPhase() + (this->getPhaseIncrement() * Argon<T>::lanes);
		return argon::ternary(new_phase >= T(1), new_phase - T(1), new_phase);
	}
	void advance() { setPhase(Periodic::render()); }
};

/// @brief A SIMD-optimized periodic signal generator for 32-bit unsigned integers.
template <>
struct Periodic<Argon<uint32_t>> : impl::PeriodicState<Argon<uint32_t>, uint32_t>, SIMDGenerator<uint32_t> {
	using impl::PeriodicState<Argon<uint32_t>, uint32_t>::PeriodicState;

	[[nodiscard]] Argon<uint32_t> render() final { return getPhase() + (getPhaseIncrement() * Argon<uint32_t>::lanes); }
	void advance() { setPhase(Periodic::render()); }
};
} // namespace deluge::dsp
