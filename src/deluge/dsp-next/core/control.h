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

namespace detail {

/// @brief Whether @p Event is an event `Control<K>::on` passes the coefficients to: callable as
/// `Event(const Coefficients&, State&, A...)`, and taking the state by non-const reference.
/// @details An event callable with a const State takes it by value or by const reference, so it could not change the
/// wrapper's state; it is not an event.
template <typename K, auto Event, typename... A>
concept event_takes_coefficients =
    std::is_invocable_v<decltype(Event), const typename K::Coefficients&, typename K::State&, A...>
    && !std::is_invocable_v<decltype(Event), const typename K::Coefficients&, const typename K::State&, A...>;

/// @brief Whether @p Event is an event `Control<K>::on` passes the state alone to: callable as
/// `Event(State&, A...)`, and taking the state by non-const reference.
/// @details An event callable with a const State takes it by value or by const reference, so it could not change the
/// wrapper's state; it is not an event.
template <typename K, auto Event, typename... A>
concept event_takes_state = std::is_invocable_v<decltype(Event), typename K::State&, A...>
                            && !std::is_invocable_v<decltype(Event), const typename K::State&, A...>;

/// @brief False for every @p Event, so that a `static_assert` on it fails only when instantiated.
template <auto Event>
inline constexpr bool unsupported_event = false;

} // namespace detail

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
/// **Events.** `on<&K::event>(args…)` runs one of K's static functions on the wrapper's own state, for what happens
/// between blocks (a hit, a stop). An event takes the state by non-const reference and is one of two shapes:
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
	[[gnu::always_inline]] void reset() {
		if constexpr (detail::declares_reset<K>) {
			K::reset(state_);
		}
		else {
			state_ = State{};
		}
	}

	/// @brief The kernel's state between blocks.
	[[gnu::always_inline]] [[nodiscard]] const State& state() const { return state_; }

	/// @brief Move the signal on by one block: one call to K's `advance`, at the configured mode when K has modes.
	/// @param n The block's length in samples; may be 0.
	/// @return The signal's value after the block.
	[[gnu::always_inline]] value_type advance(std::size_t n) {
		value_type out{};
		kernel_ops::dispatch_mode<K>(coefficients_, [&]<auto M> [[gnu::always_inline]] () {
			out = kernel_ops::advance_at<K, M>(coefficients_, state_, n);
		});
		return out;
	}

	/// @brief Run the event @p Event on the state: with the coefficients when it takes them, else without.
	/// @tparam Event A static function of K, of either shape in the class documentation.
	/// @param args The event's own arguments, after the coefficients and state.
	/// @return What @p Event returns.
	template <auto Event, typename... A>
	[[gnu::always_inline]] decltype(auto) on(A&&... args) {
		if constexpr (detail::event_takes_coefficients<K, Event, A...>) {
			return Event(coefficients_, state_, std::forward<A>(args)...);
		}
		else if constexpr (detail::event_takes_state<K, Event, A...>) {
			return Event(state_, std::forward<A>(args)...);
		}
		else {
			static_assert(detail::unsupported_event<Event>,
			              "a control event is event(const Coefficients&, State&, args...) or event(State&, args...)");
		}
	}

private:
	Coefficients coefficients_{}; ///< K's coefficients for the configured settings.
	State state_{};               ///< K's state, carried from block to block.
};

} // namespace deluge::dsp
