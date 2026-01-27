#pragma once

#include "periodic.hpp"
#include <type_traits>

namespace deluge::dsp {
/// @brief A class to represent a phasor generator.
/// @tparam T The type of the samples to generate.
template <typename T>
struct Phasor : Periodic<T>, Generator<T> {
	static_assert(std::is_floating_point_v<T>, "Phasor only supports floating point or q31_t.");
	T render() final { return Periodic<T>::advance(); }
};

template <>
struct Phasor<q31_t> : Periodic<uint32_t>, Generator<q31_t> {
	q31_t render() final {
		// Normalize the output to [0, 1) for q31_t types
		return std::bit_cast<q31_t>(Periodic::advance() >> 1);
	}
};

template <typename T>
struct Phasor<Argon<T>> : Periodic<Argon<T>>, Generator<Argon<T>> {
	static_assert(std::is_floating_point_v<T>, "Phasor only supports floating point or q31_t.");
	Argon<T> render() final {
		return Periodic<Argon<T>>::advance(); // No normalization needed for Argon types
	}
};

template <>
struct Phasor<Argon<q31_t>> : Periodic<Argon<uint32_t>>, Generator<Argon<q31_t>> {
	Argon<q31_t> render() final {
		// Normalize the output to [0, 1) for q31_t types
		return argon::bit_cast<q31_t>(Periodic::advance() >> 1);
	}
};
} // namespace deluge::dsp
