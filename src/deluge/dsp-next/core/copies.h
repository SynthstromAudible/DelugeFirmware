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

#include "dsp-next/core/pack_f32.h"
#include "dsp-next/core/poly_kernel.h"

#include <bitset>
#include <cassert>
#include <cstddef>

/// @file
/// @brief detail::Copies<K, N>: the per-lane coefficients, state and mode that Poly and Bank share.
namespace deluge::dsp::detail {

/// @brief N lanes of @p K's packed coefficients and state, with the lane-wise configure, reset and state access.
/// @details Configure and set_mode set the one mode every lane runs; configure asserts that a lane's mode matches it
/// once any lane is configured. Poly re-declares configure to un-park the lane. `configured_` is never cleared, so the
/// mode stays the last one set.
template <poly_kernel K, std::size_t N>
class Copies {
public:
	/// @brief Sets lane @p lane from @p settings. Its mode must be the lanes' mode once any lane is configured.
	void configure(std::size_t lane, const K::Settings& settings) {
		assert(lane < N);
		const typename K::Coefficients c = K::coefficients(settings);
		assert(!configured_.any() || c.mode == coefficients_.mode);
		coefficients_.mode = c.mode;
		set_lane(coefficients_, lane, c);
		configured_.set(lane);
	}
	/// @brief Every lane's mode, from the next block.
	void set_mode(K::Mode mode) { coefficients_.mode = mode; }
	/// @brief Lane @p lane's state to K's reset state.
	void reset(std::size_t lane) {
		assert(lane < N);
		set_lane(state_, lane, typename K::State{});
	}
	/// @brief Lane @p lane's state.
	[[nodiscard]] K::State state(std::size_t lane) const {
		assert(lane < N);
		return get_lane<typename K::State>(state_, lane);
	}

protected:
	using PackedCoefficients = K::template CoefficientsT<Pack<f32, N>>;
	using PackedState = K::template StateT<Pack<f32, N>>;

	/// @brief Every lane's coefficients; their mode is the mode every lane runs.
	[[nodiscard]] const PackedCoefficients& coefficients() const { return coefficients_; }
	/// @brief Every lane's state.
	[[nodiscard]] const PackedState& packed_state() const { return state_; }
	/// @brief Stores @p s as every lane's state.
	void set_packed_state(const PackedState& s) { state_ = s; }

private:
	PackedCoefficients coefficients_{}; ///< Every lane's coefficients; the shared mode.
	PackedState state_{};               ///< Every lane's state.
	std::bitset<N> configured_{};       ///< The lanes configured since construction.
};

} // namespace deluge::dsp::detail
