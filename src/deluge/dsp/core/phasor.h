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
template <typename T>
struct PhasorState {
	T phase = 0; ///< Current phase of the oscillator
	T step = 0;  ///< Increment value for the phase, typically (1 / sample_rate) * frequency

	constexpr PhasorState(T phase, T step) : phase{phase}, step{step} {} ///< Constructor to initialize phase and step
	constexpr PhasorState(T step) : step{step} {} ///< Constructor to initialize only the step, phase defaults to 0
	constexpr PhasorState(Frequency frequency)
	    : step{T((1.f / kSampleRate) * frequency)} {} ///< Constructor to initialize step based on frequency
	constexpr PhasorState() = default;                ///< Default constructor
	constexpr virtual ~PhasorState() = default;       ///< Default destructor
};
} // namespace impl

template <typename T>
struct Phasor : impl::PhasorState<T>, Generator<T> {
	using impl::PhasorState<T>::PhasorState;

	T render() override {
		this->phase += this->step;
		this->phase = (this->phase >= T(1.f)) ? this->phase - T(1.f) : this->phase;
		return this->phase;
	}
};

template <>
struct Phasor<uint32_t> : impl::PhasorState<uint32_t>, Generator<uint32_t> {
	using impl::PhasorState<uint32_t>::PhasorState;

	constexpr Phasor(Frequency frequency)
	    : impl::PhasorState<uint32_t>{std::bit_cast<uint32_t>(FixedPoint<31>((1.f / kSampleRate) * frequency).raw())
	                                  << 1} {}
	uint32_t render() override { return phase += step; }
};

template <>
struct Phasor<FixedPoint<31>> : impl::PhasorState<int32_t>, Generator<int32_t> {
	using impl::PhasorState<int32_t>::PhasorState;

	constexpr Phasor(Frequency frequency)
	    : impl::PhasorState<int32_t>{std::bit_cast<int32_t>(FixedPoint<31>((1.f / kSampleRate) * frequency).raw())} {}

	int32_t render() override {
		phase += step;
		phase = (phase >= FixedPoint<31>{1.f}.raw()) ? phase - FixedPoint<31>{1.f}.raw() : phase;
		return phase;
	}
};

template <typename T>
struct SIMDPhasor : impl::PhasorState<Argon<T>>, SIMDGenerator<T> {
	using impl::PhasorState<Argon<T>>::PhasorState;

	Argon<T> render() override {
		this->phase = this->phase + this->step;
		this->phase = argon::ternary(this->phase >= T{1}, this->phase - T{1}, this->phase);
		return this->phase;
	}
};

template <>
struct SIMDPhasor<uint32_t> : impl::PhasorState<Argon<uint32_t>>, SIMDGenerator<uint32_t> {
	using impl::PhasorState<Argon<uint32_t>>::PhasorState;

	Argon<uint32_t> render() override { return phase = phase + step; }
};

template <>
struct SIMDPhasor<FixedPoint<31>> : impl::PhasorState<Argon<int32_t>>, SIMDGenerator<int32_t> {
	using impl::PhasorState<Argon<int32_t>>::PhasorState;

	Argon<q31_t> render() override {
		phase = phase + step;
		phase = argon::ternary(phase >= FixedPoint<31>{1.f}.raw(), phase - FixedPoint<31>{1.f}.raw(), phase);
		return phase;
	}
};
