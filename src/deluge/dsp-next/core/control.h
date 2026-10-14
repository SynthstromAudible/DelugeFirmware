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
#include <cstddef>
#include <type_traits>
#include <utility>

/// @file
/// @brief Control<K>: a control kernel as a block-rate control signal.
namespace deluge::dsp {

/// @brief A control kernel as a block-rate control signal: one Coefficients, one State, one `advance` per block.
/// @tparam K The control kernel. Control<K> is a @ref control_generator.
/// @details
/// **Configure every block.** `configure` computes K's coefficients from the owner's settings and stores them,
/// leaving the state alone. It is cheap, and meant to be called before every block, so that a knob or tempo change
/// takes effect from the next block's first sample while the signal's position carries over.
///
/// **Advance once per block.** `advance(n)` moves the signal on by a whole block of @c n samples (0 included) in one
/// call to K's `advance` through @ref kernel_ops::advance_at, dispatching a moded kernel's mode once, through @ref
/// kernel_ops::dispatch_mode, on the configured coefficients. It returns the signal's value after the block, which the
/// owner applies to the block.
///
/// **Events.** `on<&K::event>(args…)` runs a function taking K's State (and optionally its Coefficients first) on the
/// wrapper's own state, for what happens between blocks (a hit, a stop). An event takes the state by non-const
/// reference and is one of two shapes:
/// - `event(const Coefficients&, State&, args…)`, given the configured coefficients;
/// - `event(State&, args…)`, given the state alone.
///
/// The first shape is tried first, so the coefficients are passed exactly when the event takes them. Any other shape
/// fails to compile: one taking the state by value, which would change a copy, and one taking it by const reference,
/// which could not change it. `on` returns what the event returns, a reference included.
///
/// **Inlining.** The app builds with `-fno-inline-functions`, so every member is `[[gnu::always_inline]]`: nothing
/// sits between the owner's block and the kernel's arithmetic.
template <control_kernel K>
class Control {
	using Coefficients = typename K::Coefficients; ///< K's coefficients, computed on configure.
	using State = typename K::State;               ///< K's state, carried from block to block.

public:
	using value_type = typename K::value_type; ///< The signal's value: what `advance` returns.

	/// @brief Value-initialised Coefficients and State{}.
	[[gnu::always_inline]] Control() = default;

	/// @brief Configured from @p settings, with State{}.
	[[gnu::always_inline]] explicit Control(const typename K::Settings& settings)
	    : coefficients_{K::coefficients(settings)} {}

	/// @brief Store the coefficients for @p settings; they apply from the next `advance`, State untouched.
	[[gnu::always_inline]] void configure(const typename K::Settings& settings) {
		coefficients_ = K::coefficients(settings);
	}

	/// @brief The coefficients from the last configure.
	[[gnu::always_inline]] [[nodiscard]] const Coefficients& coefficients() const { return coefficients_; }

	/// @brief Return to the reset State: State{}, or whatever K's `static void reset(State&)` quirk sets when K
	/// declares one. The coefficients are kept.
	[[gnu::always_inline]] void reset() { detail::reset_state<K>(state_); }

	/// @brief The kernel's state between blocks.
	[[gnu::always_inline]] [[nodiscard]] const State& state() const { return state_; }

	/// @brief Move the signal on by one block: one call to K's `advance`, at the configured mode when K has modes.
	/// @param n The block's length in samples; may be 0.
	/// @return The signal's value after the block.
	[[gnu::always_inline]] [[nodiscard]] value_type advance(std::size_t n) {
		value_type out{};
		kernel_ops::dispatch_mode<K>(coefficients_, [&]<auto M> [[gnu::always_inline]] () {
			out = kernel_ops::advance_at<K, M>(coefficients_, state_, n);
		});
		return out;
	}

	/// @brief Run the event @p Event on the state: with the coefficients when it takes them, else without.
	/// @tparam Event A function taking K's State (and optionally its Coefficients first), of either shape in the class
	/// documentation.
	/// @param args The event's own arguments, after the coefficients and state.
	/// @return What @p Event returns.
	template <auto Event, typename... A>
	[[gnu::always_inline]] decltype(auto) on(A&&... args) {
		return detail::run_event<K, Event>(coefficients_, state_, std::forward<A>(args)...);
	}

private:
	Coefficients coefficients_{}; ///< K's coefficients for the configured settings.
	State state_{};               ///< K's state, carried from block to block.
};

} // namespace deluge::dsp
