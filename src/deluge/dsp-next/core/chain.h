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

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

/// @file
/// @brief Chain<K1, …, Kn>: a kernel made of kernels, every part stepped inline in one pass; and Pinned<K, M>: a moded
/// kernel with its mode fixed at compile time.
namespace deluge::dsp {

/// @brief One value per part, part 0 first: the aggregate a chain's Settings, Coefficients, State and Block are made
/// of.
/// @details A recursive aggregate, `head` then `tail`, rather than `std::tuple`: a tuple's assignment is user-provided
/// in libstdc++ and libc++, so no tuple is trivially copyable, on the host or on the ARM build, and a kernel's values
/// must be plain values. An aggregate's copy is a copy of its bytes, and its indexing (@ref get) is member access,
/// which the shipping build inlines. Every member is value-initialised, so `Parts{}` holds each part's `T{}`.
///
/// The members are `[[no_unique_address]]`, so an empty part (an EmptyBlock, a stateless kernel's State) takes no
/// space beside a non-empty one. A part is read and written only as a whole member, never by copying its bytes, so a
/// later part placed in an earlier one's tail padding is never overwritten.
///
/// Built from its parts by brace elision, part 0 first: `Parts<A, B, C>{a, b, c}`.
template <typename... Ts>
struct Parts {};

/// @brief One value per part: @p T first, then the rest.
template <typename T, typename... Ts>
struct Parts<T, Ts...> {
	[[no_unique_address]] T head{};            ///< This part's value.
	[[no_unique_address]] Parts<Ts...> tail{}; ///< The later parts' values.
};

/// @brief Part @p I of @p parts.
template <std::size_t I, typename T, typename... Ts>
requires(I <= sizeof...(Ts))
[[nodiscard]] [[gnu::always_inline]] constexpr auto& get(Parts<T, Ts...>& parts) {
	if constexpr (I == 0) {
		return parts.head;
	}
	else {
		return get<I - 1>(parts.tail);
	}
}

/// @brief Part @p I of @p parts.
template <std::size_t I, typename T, typename... Ts>
requires(I <= sizeof...(Ts))
[[nodiscard]] [[gnu::always_inline]] constexpr const auto& get(const Parts<T, Ts...>& parts) {
	if constexpr (I == 0) {
		return parts.head;
	}
	else {
		return get<I - 1>(parts.tail);
	}
}

namespace detail {

/// @brief Whether every part after the first is a processor kernel.
template <typename... Ks>
inline constexpr bool chain_later_parts_process = false;

/// @brief Whether every part after the first is a processor kernel.
template <typename K0, typename... Rest>
inline constexpr bool chain_later_parts_process<K0, Rest...> = (processor_kernel<Rest> && ...);

/// @brief Whether every part has the first part's value type and step type.
template <typename... Ks>
inline constexpr bool chain_shares_types = false;

/// @brief Whether every part has the first part's value type and step type.
template <typename K0, typename... Rest>
inline constexpr bool chain_shares_types<K0, Rest...> =
    ((std::same_as<typename Rest::value_type, typename K0::value_type>
      && std::same_as<typename Rest::step_type, typename K0::step_type>)
     && ...);

/// @brief Set @p s to K's reset State: K's `reset` quirk when it declares one, else `State{}`.
template <typename K>
[[gnu::always_inline]] inline void reset_state(typename K::State& s) {
	if constexpr (declares_reset<K>) {
		K::reset(s);
	}
	else {
		s = typename K::State{};
	}
}

} // namespace detail

/// @brief Whether @p Ks can be chained: at least two kernels, the first a generator, processor or mixer and every
/// later one a processor, all stepping the same step type over the same value type.
/// @details Rejected:
/// - fewer than two parts;
/// - a control kernel (its block is its step) or a staged kernel (it processes whole blocks), anywhere;
/// - a part after the first that is not a processor, since each later part takes the one value the part before it
///   returns;
/// - parts whose value or step types differ, so a chain never converts between domains;
/// - a part declaring kPartialStepAdvancesByCount: a chain steps whole vectors, so such a part would advance by a
///   whole vector at a block's end in a chain and by the active lanes alone.
template <typename... Ks>
concept chainable = sizeof...(Ks) >= 2 && (kernel<Ks> && ...) && (!control_kernel<Ks> && ...)
                    && (!staged_kernel<Ks> && ...) && detail::chain_later_parts_process<Ks...>
                    && detail::chain_shares_types<Ks...> && (!partial_step_advances_by_count<Ks> && ...);

namespace detail {

/// @brief A chain's `prepare` and `Block`, present only when some part declares a `prepare`: a base, so that a chain
/// without one declares no member named `prepare` for @ref declares_prepare to find.
template <bool Declares, typename... Ks>
struct ChainPrepare {};

/// @brief A chain's `prepare` and `Block`: each declaring part's `prepare`, in part order.
template <typename... Ks>
struct ChainPrepare<true, Ks...> {
	/// @brief Every part's per-block values, an EmptyBlock for a part without a `prepare`.
	using Block = Parts<kernel_block_t<Ks>...>;

	/// @brief Every part's Block for a block of @p n samples: each declaring part's `prepare` on its own Coefficients
	/// and State, part 0 first, once.
	/// @param n The block's length in samples (frames for a stereo kernel); may be 0.
	[[nodiscard]] [[gnu::always_inline]] static Block prepare(const Parts<typename Ks::Coefficients...>& c,
	                                                          Parts<typename Ks::State...>& s, std::size_t n) {
		return prepareParts(c, s, n, std::index_sequence_for<Ks...>{});
	}

private:
	/// @brief @ref prepare, with the parts' indices.
	template <std::size_t... I>
	[[nodiscard]] [[gnu::always_inline]] static Block prepareParts(const Parts<typename Ks::Coefficients...>& c,
	                                                               Parts<typename Ks::State...>& s, std::size_t n,
	                                                               std::index_sequence<I...> /*parts*/) {
		Block block;
		// A comma fold runs its operands left to right: part 0 prepares first.
		((get<I>(block) = kernel_ops::prepare_block<Ks>(get<I>(c), get<I>(s), n)), ...);
		return block;
	}
};

/// @brief A chain's `reset`, present only when some part has a reset quirk: a base, so that a chain without one
/// declares no member named `reset` for @ref declares_reset to find, and its wrappers assign `State{}`.
template <bool Declares, typename... Ks>
struct ChainReset {};

/// @brief A chain's `reset`: each part's quirk, or `State{}` for a part without one.
template <typename... Ks>
struct ChainReset<true, Ks...> {
	/// @brief Every part to its reset State, part 0 first.
	[[gnu::always_inline]] static void reset(Parts<typename Ks::State...>& s) {
		resetParts(s, std::index_sequence_for<Ks...>{});
	}

private:
	/// @brief @ref reset, with the parts' indices.
	template <std::size_t... I>
	[[gnu::always_inline]] static void resetParts(Parts<typename Ks::State...>& s,
	                                              std::index_sequence<I...> /*parts*/) {
		(reset_state<Ks>(get<I>(s)), ...);
	}
};

} // namespace detail

/// @brief A kernel made of kernels: each sample (or vector) goes through part 0's `step`, then part 1's, … in one
/// pass per block, every step inline.
/// @tparam Ks The parts, in signal order; @ref chainable says which. A chain is itself a kernel of its first part's
/// form, so every wrapper takes it unchanged (`Mono<Chain<…>>`, `Stereo<Chain<…>>`, `Lanes<Chain<…>, N>`), and a
/// chain may be a part of another chain.
/// @details
/// **Types.** `value_type` and `step_type` are the parts'. `Settings`, `Coefficients`, `State` and `Block` are
/// @ref Parts of the parts' own, part @c I at `get<I>`, each a plain value since each part's is. `coefficients` calls
/// each part's.
///
/// **Per block.** `prepare(c, s, n)` runs each declaring part's `prepare` on its own Coefficients and State, once, in
/// part order; its Block is every part's Block, an EmptyBlock for a part without one. A chain declares `prepare` and
/// `Block` only when some part declares a `prepare`, so a chain of parts without one costs its wrapper nothing per
/// block.
///
/// **Step.** `step` takes the first part's arity: none, one or two inputs. It steps part 0 on them, then each later
/// part on the value the part before it returned: `y = K1::step(c1, b1, s1, x…); y = K2::step(c2, b2, s2, y); …`.
/// Each part steps its own State in the order it would alone, so a chain is bit for bit its parts run as separate
/// passes. Each frame is read whole by its wrapper before any of it is written, so a chain renders in place as it
/// does out of place.
///
/// **Reset.** `reset` is declared only when some part has a reset quirk; it runs each part's quirk, or sets a part
/// without one to `State{}`. Without it the wrappers assign `State{}`, which is every part's `State{}`.
/// `reset_part<I>` resets part @c I alone, the same way, as an event through a wrapper's `on`:
/// `mono.on<&Chain<…>::reset_part<I>>()`.
///
/// **Modes.** Every part is unmoded: a moded kernel joins a chain at one mode, fixed by @ref Pinned.
///
/// **Op-major.** A chain declares no `kLanePackable`: `Lanes` steps it lane by lane.
///
/// **Inlining.** Every member is `[[gnu::always_inline]]`, so under the shipping `-fno-inline-functions` the parts'
/// steps land in the wrapper's loop with no call between them.
template <kernel... Ks>
requires chainable<Ks...>
struct Chain : detail::ChainPrepare<(detail::declares_prepare<Ks> || ...), Ks...>,
               detail::ChainReset<(detail::declares_reset<Ks> || ...), Ks...> {
	static_assert((!detail::has_modes<Ks> && ...),
	              "a chain's parts are unmoded: fix a moded part's mode at compile time with Pinned<K, M>");

	/// @brief Part @p I's kernel type.
	template <std::size_t I>
	using part_t = std::tuple_element_t<I, std::tuple<Ks...>>;

	using value_type = typename part_t<0>::value_type; ///< Every part's value type.
	using step_type = typename part_t<0>::step_type;   ///< Every part's step type.

	using Settings = Parts<typename Ks::Settings...>;         ///< Each part's settings, part 0 first.
	using Coefficients = Parts<typename Ks::Coefficients...>; ///< Each part's coefficients, part 0 first.
	using State = Parts<typename Ks::State...>;               ///< Each part's state, part 0 first.

private:
	/// @brief Whether some part declares a `prepare`, and so the chain does.
	static constexpr bool kPrepares = (detail::declares_prepare<Ks> || ...);
	/// @brief Every part's Block, part 0 first.
	using Blocks = Parts<kernel_block_t<Ks>...>;
	/// @brief The Block a step receives: every part's when the chain prepares, else an EmptyBlock.
	using StepBlock = std::conditional_t<kPrepares, Blocks, EmptyBlock>;
	/// @brief The first part, whose arity the chain takes.
	using First = part_t<0>;

public:
	/// @brief Each part's coefficients for its own settings.
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficients(const Settings& settings) {
		return coefficientsOf(settings, std::index_sequence_for<Ks...>{});
	}

	/// @brief Part @p I to its reset State, every other part untouched: part @p I's reset quirk, or `State{}`.
	/// @details An event: a wrapper runs it through `on`, on each lane's State in turn.
	template <std::size_t I>
	requires(I < sizeof...(Ks))
	[[gnu::always_inline]] static void reset_part(State& s) {
		detail::reset_state<part_t<I>>(get<I>(s));
	}

	/// @brief Generate one step: part 0 generates, each later part processes.
	[[nodiscard]] [[gnu::always_inline]] static step_type step(const Coefficients& c, const StepBlock& b, State& s)
	requires generator_kernel<First>
	{
		return stepAll(c, b, s);
	}

	/// @brief Process one step: part 0 takes @p x, each later part what the part before it returned.
	[[nodiscard]] [[gnu::always_inline]] static step_type step(const Coefficients& c, const StepBlock& b, State& s,
	                                                           step_type x)
	requires processor_kernel<First>
	{
		return stepAll(c, b, s, x);
	}

	/// @brief Mix one step: part 0 mixes @p a and @p b2, each later part processes.
	[[nodiscard]] [[gnu::always_inline]] static step_type step(const Coefficients& c, const StepBlock& b, State& s,
	                                                           step_type a, step_type b2)
	requires mixer_kernel<First>
	{
		return stepAll(c, b, s, a, b2);
	}

private:
	/// @brief @ref coefficients, with the parts' indices.
	template <std::size_t... I>
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficientsOf(const Settings& settings,
	                                                                        std::index_sequence<I...> /*parts*/) {
		Coefficients c;
		((get<I>(c) = Ks::coefficients(get<I>(settings))), ...);
		return c;
	}

	/// @brief One step of every part, with every part's Block: the chain's own, or EmptyBlocks when it prepares none.
	template <typename... X>
	[[nodiscard]] [[gnu::always_inline]] static step_type stepAll(const Coefficients& c, const StepBlock& b, State& s,
	                                                              X... x) {
		if constexpr (kPrepares) {
			return stepParts(c, b, s, std::make_index_sequence<sizeof...(Ks) - 1>{}, x...);
		}
		else {
			const Blocks none{};
			return stepParts(c, none, s, std::make_index_sequence<sizeof...(Ks) - 1>{}, x...);
		}
	}

	/// @brief Part 0's step on @p x, then part I + 1's on the value before it, for each I in order.
	template <std::size_t... I, typename... X>
	[[nodiscard]] [[gnu::always_inline]] static step_type stepParts(const Coefficients& c, const Blocks& b, State& s,
	                                                                std::index_sequence<I...> /*later*/, X... x) {
		step_type y = stepPart<0>(c, b, s, x...);
		// A comma fold runs its operands left to right: each part steps on the output of the one before it.
		((y = stepPart<I + 1>(c, b, s, y)), ...);
		return y;
	}

	/// @brief Part @p P's step on @p x, with its own Coefficients, Block and State.
	template <std::size_t P, typename... X>
	[[nodiscard]] [[gnu::always_inline]] static step_type stepPart(const Coefficients& c, const Blocks& b, State& s,
	                                                               X... x) {
		return kernel_ops::step_at<part_t<P>, kernel_ops::NoMode::NONE>(get<P>(c), get<P>(b), get<P>(s), x...);
	}
};

namespace detail {

/// @brief A pinned kernel's `prepare` and `Block`, present only when K declares a `prepare`.
template <bool Declares, typename K>
struct PinnedPrepare {};

/// @brief A pinned kernel's `prepare` and `Block`: K's.
template <typename K>
struct PinnedPrepare<true, K> {
	using Block = kernel_block_t<K>; ///< K's per-block values.

	/// @brief K's `prepare`.
	[[nodiscard]] [[gnu::always_inline]] static Block prepare(const typename K::Coefficients& c, typename K::State& s,
	                                                          std::size_t n) {
		return K::prepare(c, s, n);
	}
};

/// @brief A pinned kernel's `reset`, present only when K has a reset quirk.
template <bool Declares, typename K>
struct PinnedReset {};

/// @brief A pinned kernel's `reset`: K's quirk.
template <typename K>
struct PinnedReset<true, K> {
	/// @brief K's reset quirk.
	[[gnu::always_inline]] static void reset(typename K::State& s) { K::reset(s); }
};

} // namespace detail

/// @brief Moded kernel @p K with its mode fixed to @p M at compile time: an unmoded kernel whose `step` is
/// `K::step<M>`.
/// @tparam K A moded kernel.
/// @tparam M One of K's listed modes.
/// @details Its Settings, Coefficients, State and Block are K's, and its `coefficients`, `prepare` and `reset` are
/// K's, declared exactly when K declares them. The Coefficients keep K's `mode` member, which nothing reads: the step
/// is fixed at @p M whatever `coefficients` puts there. Pinned declares no `Modes`, so it is no moded kernel, and a
/// chain dispatches on nothing for it. It declares no `kLanePackable`, so `Lanes` steps it lane by lane. Its step
/// forwards whatever K's takes, the counted partial step included, and it declares K's
/// kPartialStepAdvancesByCount.
template <kernel K, auto M>
requires detail::has_modes<K> && std::same_as<decltype(M), std::remove_cv_t<decltype(K::Coefficients::mode)>>
             && (detail::lists_mode(typename K::Modes{}, M))
struct Pinned : detail::PinnedPrepare<detail::declares_prepare<K>, K>,
                detail::PinnedReset<detail::declares_reset<K>, K> {
	using value_type = typename K::value_type;     ///< K's value type.
	using step_type = typename K::step_type;       ///< K's step type.
	using Settings = typename K::Settings;         ///< K's settings.
	using Coefficients = typename K::Coefficients; ///< K's coefficients; their `mode` is never read.
	using State = typename K::State;               ///< K's state.

	/// @brief Whether K's partial last vector advances by the active lanes.
	static constexpr bool kPartialStepAdvancesByCount = partial_step_advances_by_count<K>;

	/// @brief K's coefficients for @p settings.
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficients(const Settings& settings) {
		return K::coefficients(settings);
	}

	/// @brief One step of K at mode @p M, with whatever inputs K's step at its form takes.
	template <typename... X>
	requires requires(const Coefficients& c, const kernel_block_t<K>& b, State& s, X... x) {
		K::template step<M>(c, b, s, x...);
	}
	[[nodiscard]] [[gnu::always_inline]] static step_type step(const Coefficients& c, const kernel_block_t<K>& b,
	                                                           State& s, X... x) {
		return K::template step<M>(c, b, s, x...);
	}
};

} // namespace deluge::dsp
