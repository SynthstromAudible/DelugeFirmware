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

#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/pack_f32.h"
#include "dsp-next/core/poly_kernel.h"

#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>

/// @file
/// @brief detail::Copies<K, N>: the per-lane coefficients, state and mode that Poly and Bank share.
namespace deluge::dsp::detail {

/// @brief Calls @p body.template operator()<M>() once, at the mode of @p c for a moded K, else at NoMode::NONE.
/// @param c K's packed coefficients, whose `mode` a moded kernel dispatches on.
template <class K, class C, class Body>
[[gnu::always_inline]] inline void dispatch_lane_mode([[maybe_unused]] const C& c, Body&& body) {
	if constexpr (has_modes<K>) {
		with_mode<typename K::Modes>(c.mode, std::forward<Body>(body));
	}
	else {
		std::forward<Body>(body).template operator()<kernel_ops::NoMode::NONE>();
	}
}

/// @brief One `step` of K over packed coefficients and state: `K::step<M>` for a moded K, else K's `step`.
template <class K, auto M, class... Args>
[[gnu::always_inline]] inline auto lane_step(Args&&... args) {
	if constexpr (has_modes<K>) {
		return K::template step<M>(std::forward<Args>(args)...);
	}
	else {
		return K::step(std::forward<Args>(args)...);
	}
}

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
		if constexpr (has_modes<K>) {
			assert(!configured_ || c.mode == coefficients_.mode);
			coefficients_.mode = c.mode;
		}
		set_lane(coefficients_, lane, c);
		configured_ = true;
	}
	/// @brief Every lane's mode, from the next block. A moded kernel only.
	void set_mode(const auto& mode)
	requires has_modes<K>
	{
		coefficients_.mode = mode;
	}
	/// @brief Lane @p lane's state to K's reset state.
	void reset(std::size_t lane) {
		assert(lane < N);
		set_lane(state_, lane, typename K::State{});
	}

	/// @brief Runs the event @p Event on lane @p lane alone: that lane's scalar state (and coefficients, when the event
	/// takes them) are read out, the event runs on them as it does on a Mono, and the state is written back.
	/// @tparam Event A function as Mono::on accepts.
	/// @param lane The lane the event addresses.
	/// @param args The event's own arguments.
	/// @return What @p Event returns, by value.
	template <auto Event, typename... A>
	decltype(auto) on(std::size_t lane, A&&... args) {
		assert(lane < N);
		typename K::State s = get_lane<typename K::State>(this->packed_state(), lane);
		typename K::Coefficients c = get_lane<typename K::Coefficients>(this->coefficients(), lane);
		if constexpr (detail::has_modes<K>) {
			c.mode = this->coefficients().mode;
		}
		using Result = decltype(detail::run_event<K, Event>(c, s, std::forward<A>(args)...));
		PackedState packed = this->packed_state();
		if constexpr (std::is_void_v<Result>) {
			detail::run_event<K, Event>(c, s, std::forward<A>(args)...);
			set_lane(packed, lane, s);
			this->set_packed_state(packed);
		}
		else {
			std::remove_cvref_t<Result> result = detail::run_event<K, Event>(c, s, std::forward<A>(args)...);
			set_lane(packed, lane, s);
			this->set_packed_state(packed);
			return result;
		}
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
	bool configured_ = false;           ///< Whether any lane has been configured since construction.
};

} // namespace deluge::dsp::detail
