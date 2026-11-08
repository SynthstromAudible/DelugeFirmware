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
#include "dsp-next/core/poly_kernel.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

/// @file
/// @brief Chain<K1, …, Kn>: a kernel made of kernels, every part stepped inline in one pass; and Pinned<K, M>: a moded
/// kernel with its mode fixed at compile time.
namespace deluge::dsp {

template <typename... Ts>
struct Parts;

namespace detail {

/// @brief Whether every one of @p Ts is an empty class: true for none.
template <typename... Ts>
inline constexpr bool all_empty_v = (std::is_empty_v<Ts> && ...);

/// @brief The members of a non-empty @ref Parts: @p T's value, then @p Tail, each `[[no_unique_address]]` exactly
/// when it is empty.
/// @tparam HeadEmpty Whether @p T is an empty class.
/// @tparam TailEmpty Whether every later part is an empty class, so @p Tail holds no bytes.
template <typename T, typename Tail, bool HeadEmpty, bool TailEmpty>
struct PartsNode {
	T head{};    ///< This part's value.
	Tail tail{}; ///< The later parts' values.
};

/// @brief An empty part, then later parts holding bytes.
template <typename T, typename Tail>
struct PartsNode<T, Tail, true, false> {
	[[no_unique_address]] T head{}; ///< This part's value.
	Tail tail{};                    ///< The later parts' values.
};

/// @brief A part holding bytes, then only empty parts.
template <typename T, typename Tail>
struct PartsNode<T, Tail, false, true> {
	T head{};                          ///< This part's value.
	[[no_unique_address]] Tail tail{}; ///< The later parts' values.
};

/// @brief An empty part, then only empty parts.
template <typename T, typename Tail>
struct PartsNode<T, Tail, true, true> {
	[[no_unique_address]] T head{};    ///< This part's value.
	[[no_unique_address]] Tail tail{}; ///< The later parts' values.
};

} // namespace detail

/// @brief One value per part, part 0 first: the aggregate a chain's Settings, Coefficients, State and Block are made
/// of.
/// @details A recursive aggregate, `head` then `tail`, rather than `std::tuple`: a tuple's assignment is user-provided
/// in libstdc++ and libc++, so no tuple is trivially copyable, on the host or on the ARM build, and a kernel's values
/// must be plain values. An aggregate's copy is a copy of its bytes, and its indexing (@ref get) is member access,
/// which the shipping build inlines. Every member is value-initialised, so `Parts{}` holds each part's `T{}`.
///
/// Only an empty member is `[[no_unique_address]]`: an empty part (an EmptyBlock, a stateless kernel's State), and a
/// tail of empty parts, take no space beside parts holding bytes, though two empty parts of one type keep distinct
/// addresses and so may take a byte. Every part holding bytes is a whole object of its own size. No part sits in
/// another's tail padding, so a part's kernel may treat its value as a complete object, bytes and all.
///
/// Built from its parts by brace elision, part 0 first: `Parts<A, B, C>{a, b, c}`.
///
/// Its `fields` are `head` and `tail`, so set_lane and get_lane move a lane of Parts of packed values part by part.
template <typename... Ts>
struct Parts {
	// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name set_lane reads.
	static constexpr std::tuple<> fields{}; ///< No part: no field.
};

/// @brief One value per part: @p T first, then the rest.
template <typename T, typename... Ts>
struct Parts<T, Ts...> : detail::PartsNode<T, Parts<Ts...>, std::is_empty_v<T>, detail::all_empty_v<Ts...>> {
	// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name set_lane reads.
	static constexpr auto fields = std::tuple{&Parts::head, &Parts::tail}; ///< This part, then the later parts.
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
inline constexpr bool chain_later_parts_process_v = false;

/// @brief Whether every part after the first is a processor kernel.
template <typename K0, typename... Rest>
inline constexpr bool chain_later_parts_process_v<K0, Rest...> = (processor_kernel<Rest> && ...);

/// @brief Whether every part has the first part's value type and step type.
template <typename... Ks>
inline constexpr bool chain_shares_types_v = false;

/// @brief Whether every part has the first part's value type and step type.
template <typename K0, typename... Rest>
inline constexpr bool chain_shares_types_v<K0, Rest...> =
    ((std::same_as<typename Rest::value_type, typename K0::value_type>
      && std::same_as<typename Rest::step_type, typename K0::step_type>)
     && ...);

} // namespace detail

namespace detail {

/// @brief The mode type of the ModeList @p List.
template <typename List>
using mode_type_of = std::remove_cv_t<decltype(first_mode_v<List>)>;

/// @brief A chain's mode: one mode per moded part, part order, each defaulting to its part's first listed mode.
/// @tparam Lists The moded parts' ModeLists, in part order.
/// @details A recursive aggregate, `head` then `tail`, as @ref Parts is: public members only, so it is structural
/// and can be a template argument, and its defaulted `==` compares every member, so with_mode matches the whole
/// combination. Built by brace elision, part order: `ChainMode<A, B>{a, b}`. Member @c J is `get<J>(mode)`.
template <typename... Lists>
struct ChainMode;

/// @brief The last moded part's mode.
template <typename List>
struct ChainMode<List> {
	mode_type_of<List> head = first_mode_v<List>; ///< This part's mode; its first listed by default.

	/// @brief Whether every member matches.
	[[nodiscard]] [[gnu::always_inline]] constexpr bool operator==(const ChainMode&) const = default;
};

/// @brief One moded part's mode, then the later moded parts'.
template <typename List, typename Next, typename... Rest>
struct ChainMode<List, Next, Rest...> {
	mode_type_of<List> head = first_mode_v<List>; ///< This part's mode; its first listed by default.
	ChainMode<Next, Rest...> tail{};              ///< The later moded parts' modes.

	/// @brief Whether every member matches.
	[[nodiscard]] [[gnu::always_inline]] constexpr bool operator==(const ChainMode&) const = default;
};

} // namespace detail

/// @brief Moded part @p J's mode in a chain's mode: the @p J th moded part's, counting moded parts only.
template <std::size_t J, typename... Lists>
requires(J < sizeof...(Lists))
[[nodiscard]] [[gnu::always_inline]] constexpr auto get(const detail::ChainMode<Lists...>& mode) {
	if constexpr (J == 0) {
		return mode.head;
	}
	else {
		return get<J - 1>(mode.tail);
	}
}

namespace detail {

/// @brief Two or more ModeLists joined, in order.
template <typename... Lists>
struct concat_modes;

/// @brief One ModeList, as it is.
template <auto... Ms>
struct concat_modes<ModeList<Ms...>> {
	using type = ModeList<Ms...>; ///< The list.
};

/// @brief The first two lists joined, then the rest.
template <auto... Ms, auto... Ns, typename... Rest>
struct concat_modes<ModeList<Ms...>, ModeList<Ns...>, Rest...> {
	using type = typename concat_modes<ModeList<Ms..., Ns...>, Rest...>::type; ///< Every list joined.
};

/// @brief Every listed mode of @p Tail with @p Head before it, as a @p Mode, in @p Tail's order.
template <typename Mode, auto Head, typename Tail>
struct prefix_modes;

/// @brief Every listed mode of the tail with @p Head before it, as a @p Mode, in the tail's order.
template <typename Mode, auto Head, auto... Ts>
struct prefix_modes<Mode, Head, ModeList<Ts...>> {
	using type = ModeList<Mode{Head, Ts}...>; ///< One mode per tail mode.
};

/// @brief The product of @p Lists as a ModeList of ChainMode<Lists...>, row-major: part 0's mode changes slowest, the
/// last part's fastest, so the first entry is every part's first listed mode.
template <typename... Lists>
struct mode_product;

/// @brief One part's modes, each as a one-member mode.
template <auto... Ms>
struct mode_product<ModeList<Ms...>> {
	using type = ModeList<ChainMode<ModeList<Ms...>>{Ms}...>; ///< One entry per mode.
};

/// @brief For each of the first part's modes in order, that mode before every entry of the later parts' product.
template <auto... Ms, typename Next, typename... Rest>
struct mode_product<ModeList<Ms...>, Next, Rest...> {
	using type = typename concat_modes<
	    typename prefix_modes<ChainMode<ModeList<Ms...>, Next, Rest...>, Ms,
	                          typename mode_product<Next, Rest...>::type>::type...>::type; ///< Row-major product.
};

/// @brief A chain's `Mode` and `Modes`, present only when some part is moded: a base, so that a chain of unmoded
/// parts declares no `Modes` for @ref has_modes to find.
/// @tparam Lists The moded parts' ModeLists, in part order.
template <typename... Lists>
struct ChainModes {
	/// @brief One mode per moded part, part order: @ref ChainMode.
	using Mode = ChainMode<Lists...>;
	/// @brief Every combination of the moded parts' modes, row-major in part order; the first is the default.
	using Modes = typename mode_product<Lists...>::type;
};

/// @brief No moded part: no `Mode`, no `Modes`.
template <>
struct ChainModes<> {};

/// @brief @p Acc with K's ModeList appended when K is moded.
template <typename Acc, typename K, bool Moded = has_modes<K>>
struct append_modes {
	using type = Acc; ///< K is unmoded: unchanged.
};

/// @brief @p Acc with K's ModeList appended.
template <typename... Lists, typename K>
struct append_modes<ChainModes<Lists...>, K, true> {
	using type = ChainModes<Lists..., typename K::Modes>; ///< K's modes last.
};

/// @brief The ChainModes of the moded parts among @p Ks, in part order.
template <typename Acc, typename... Ks>
struct chain_modes_of {
	using type = Acc; ///< Every part seen.
};

/// @brief The ChainModes of the moded parts among K and @p Ks, in part order.
template <typename Acc, typename K, typename... Ks>
struct chain_modes_of<Acc, K, Ks...> {
	using type = typename chain_modes_of<typename append_modes<Acc, K>::type, Ks...>::type; ///< K, then the rest.
};

/// @brief The mode a chain's step is templated on: its ChainMode, or NoMode::NONE's type when no part is moded.
template <typename Modes>
struct chain_step_mode {
	using type = typename Modes::Mode; ///< The chain's mode.
};

/// @brief A chain of unmoded parts steps at NoMode::NONE.
template <>
struct chain_step_mode<ChainModes<>> {
	using type = kernel_ops::NoMode; ///< The unmoded placeholder.
};

/// @brief A moded chain's Coefficients: every part's, then the mode assembled from the moded parts'.
/// @details The mode is a member beside the parts, never in a base's tail padding, so every part's Coefficients stays
/// a whole object of its own. Part @c I's Coefficients are `get<I>(c)`, as for a bare @ref Parts. The chain dispatches
/// on `mode` alone and never reads a part's own `mode` member, so Coefficients are rebuilt through
/// `Chain::coefficients(settings)`, which sets both, rather than by editing a part's.
template <typename Mode, typename... Ts>
struct ModedParts {
	Parts<Ts...> parts{}; ///< Each part's coefficients, part 0 first.
	Mode mode{};          ///< The combination the wrapper dispatches on once per block.
	/// @brief The parts: the mode is no field, so every lane of a Poly or Bank shares it.
	// NOLINTNEXTLINE(readability-identifier-naming): `fields` is the name set_lane reads.
	static constexpr auto fields = std::tuple{&ModedParts::parts};
};

/// @brief A chain's Coefficients from its parts' @p Ts: a bare Parts at NoMode, else the parts beside @p Mode.
template <typename Mode, typename... Ts>
using chain_coefficients_t =
    std::conditional_t<std::same_as<Mode, kernel_ops::NoMode>, Parts<Ts...>, ModedParts<Mode, Ts...>>;

/// @brief A chain's Coefficients, State and step value over a value type V, for a chain whose parts are not all
/// @ref poly_kernel: its own, whatever V, which is never deduced.
template <bool Poly, typename Mode, typename... Ks>
struct ChainOver {
	/// @brief The chain's Coefficients.
	template <typename V>
	using CoefficientsOf = chain_coefficients_t<Mode, typename Ks::Coefficients...>;
	/// @brief The chain's State.
	template <typename V>
	using StateOf = Parts<typename Ks::State...>;
	/// @brief What the chain's step takes and returns: its step type.
	template <typename V>
	using StepOf = std::tuple_element_t<0, std::tuple<Ks...>>::step_type;
};

/// @brief A chain of poly kernels' Coefficients, State and step value over V, f32 or Pack<f32, N>: each part's own
/// `CoefficientsT<V>` and `StateT<V>`, and V. V is deduced from the State.
template <typename Mode, typename... Ks>
struct ChainOver<true, Mode, Ks...> {
	/// @brief Each part's coefficients over V, beside the mode as the chain's Coefficients have it.
	template <typename V>
	using CoefficientsOf = chain_coefficients_t<Mode, typename Ks::template CoefficientsT<V>...>;
	/// @brief Each part's state over V.
	template <typename V>
	using StateOf = Parts<typename Ks::template StateT<V>...>;
	/// @brief What the chain's step takes and returns: V.
	template <typename V>
	using StepOf = V;

	/// @brief The chain's coefficients over V, as @ref poly_kernel reads them.
	template <typename V>
	using CoefficientsT = CoefficientsOf<V>;
	/// @brief The chain's state over V, as @ref poly_kernel reads it.
	template <typename V>
	using StateT = StateOf<V>;
};

/// @brief The types a chain of @p Ks derives from its parts' modes and value forms.
template <typename... Ks>
struct ChainTypes {
	/// @brief The `Mode` and `Modes` base: empty when no part is moded.
	using ModesBase = typename chain_modes_of<ChainModes<>, Ks...>::type;
	/// @brief Whether some part is moded.
	static constexpr bool kModed = (has_modes<Ks> || ...);
	/// @brief What the chain's `step` is templated on.
	using StepMode = typename chain_step_mode<ModesBase>::type;
	/// @brief Each part's coefficients, with the mode beside them when the chain is moded.
	using Coefficients = chain_coefficients_t<StepMode, typename Ks::Coefficients...>;
	/// @brief The chain's Coefficients, State and step value over V: the parts' own over V when every part is a
	/// @ref poly_kernel.
	using Over = ChainOver<(poly_kernel<Ks> && ...), StepMode, Ks...>;
};

} // namespace detail

/// @brief Part @p I of a moded chain's Coefficients.
template <std::size_t I, typename Mode, typename... Ts>
[[nodiscard]] [[gnu::always_inline]] constexpr auto& get(detail::ModedParts<Mode, Ts...>& c) {
	return get<I>(c.parts);
}

/// @brief Part @p I of a moded chain's Coefficients.
template <std::size_t I, typename Mode, typename... Ts>
[[nodiscard]] [[gnu::always_inline]] constexpr const auto& get(const detail::ModedParts<Mode, Ts...>& c) {
	return get<I>(c.parts);
}

/// @brief Whether @p Ks can be chained: at least two kernels, the first a generator, processor or mixer and every
/// later one a processor, all stepping the same step type over the same value type.
/// @details Rejected:
/// - fewer than two parts;
/// - a control kernel (its block is its step) or a staged kernel (it processes whole blocks), anywhere;
/// - a part after the first that is not a processor, since each later part takes the one value the part before it
///   returns;
/// - parts whose value or step types differ, so a chain never converts between domains;
/// - a part declaring kPartialStepAdvancesByCount: a chain steps whole vectors, so such a part's partial last vector
///   would advance by a whole vector in a chain but by the active lanes when run alone;
/// - a part with a Context: the chain would have to pass one on to it, and no chained kernel needs one.
template <typename... Ks>
concept chainable =
    sizeof...(Ks) >= 2 && (kernel<Ks> && ...) && (!control_kernel<Ks> && ...) && (!staged_kernel<Ks> && ...)
    && detail::chain_later_parts_process_v<Ks...> && detail::chain_shares_types_v<Ks...>
    && (!partial_step_advances_by_count_v<Ks> && ...) && (!has_context<Ks> && ...);

namespace detail {

/// @brief K's Block for a block of @p n samples, over any value form of its Coefficients and State: its `prepare` when
/// it declares one, else an EmptyBlock.
template <typename K, typename C, typename S>
[[nodiscard]] [[gnu::always_inline]] inline kernel_block_t<K>
prepare_part([[maybe_unused]] const C& c, [[maybe_unused]] S& s, [[maybe_unused]] std::size_t n) {
	if constexpr (declares_prepare<K>) {
		return K::prepare(c, s, n);
	}
	else {
		return kernel_block_t<K>{};
	}
}

/// @brief A chain's `prepare` and `Block`, present only when some part declares a `prepare`: a base, so that a chain
/// without one declares no member named `prepare` for @ref declares_prepare to find.
/// @tparam Over The chain's @ref ChainOver.
template <bool Declares, typename Over, typename... Ks>
struct ChainPrepare {};

/// @brief A chain's `prepare` and `Block`: each declaring part's `prepare`, in part order.
template <typename Over, typename... Ks>
struct ChainPrepare<true, Over, Ks...> {
private:
	/// @brief The chain's Coefficients over V.
	template <typename V>
	using CoefficientsOf = Over::template CoefficientsOf<V>;
	/// @brief The chain's State over V.
	template <typename V>
	using StateOf = Over::template StateOf<V>;

public:
	/// @brief Every part's per-block values, an EmptyBlock for a part without a `prepare`. The same for every V: a
	/// Block is shared by every lane.
	using Block = Parts<kernel_block_t<Ks>...>;

	/// @brief Every part's Block for a block of @p n samples: each declaring part's `prepare` on its own Coefficients
	/// and State, part 0 first, once.
	/// @tparam V The value type, for a chain of poly kernels: f32, or Pack<f32, N> under Poly and Bank.
	/// @param n The block's length in samples (frames for a stereo kernel); may be 0.
	template <typename V = std::tuple_element_t<0, std::tuple<Ks...>>::value_type>
	[[nodiscard]] [[gnu::always_inline]] static Block prepare(const CoefficientsOf<V>& c, StateOf<V>& s,
	                                                          std::size_t n) {
		return prepare_parts<V>(c, s, n, std::index_sequence_for<Ks...>{});
	}

private:
	/// @brief @ref prepare, with the parts' indices.
	template <typename V, std::size_t... I>
	[[nodiscard]] [[gnu::always_inline]] static Block
	prepare_parts(const CoefficientsOf<V>& c, StateOf<V>& s, std::size_t n, std::index_sequence<I...> /*parts*/) {
		Block block;
		// A comma fold runs its operands left to right: part 0 prepares first.
		((get<I>(block) = prepare_part<Ks>(get<I>(c), get<I>(s), n)), ...);
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
		reset_parts(s, std::index_sequence_for<Ks...>{});
	}

private:
	/// @brief @ref reset, with the parts' indices.
	template <std::size_t... I>
	[[gnu::always_inline]] static void reset_parts(Parts<typename Ks::State...>& s,
	                                               std::index_sequence<I...> /*parts*/) {
		(reset_state<Ks>(get<I>(s)), ...);
	}
};

} // namespace detail

/// @brief A kernel made of kernels: each sample (or vector) goes through part 0's `step`, then part 1's, … in one
/// pass per block, every step inline.
/// @tparam Ks The parts, in signal order; @ref chainable says which. A chain is itself a kernel of its first part's
/// form, so every wrapper takes it unchanged (`Mono<Chain<…>>`, `Stereo<Chain<…>>`, `Linked<Chain<…>, N>`), and a
/// chain may be a part of another chain.
/// @details
/// **Types.** `value_type` and `step_type` are the parts'. `Settings`, `State` and `Block` are @ref Parts of the
/// parts' own, part @c I at `get<I>`, each a plain value since each part's is. `Coefficients` are the parts' too, at
/// `get<I>(c)`: a bare @ref Parts for a chain of unmoded parts; for a moded chain, the parts beside a `mode` member.
/// `coefficients` calls each part's.
///
/// **Over V.** When every part is a @ref poly_kernel, `CoefficientsT<V>` and `StateT<V>` are built the same way from
/// the parts' own `CoefficientsT<V>` and `StateT<V>` (the `mode` beside them as in `Coefficients`), and
/// `Coefficients` and `State` are their f32 forms. @ref Parts and the moded Coefficients declare `fields` reaching
/// each part, the mode no field, so set_lane and get_lane move a lane part by part, and the chain is itself a poly
/// kernel: `Poly<Chain<…>, N>` steps the whole chain once per frame on a Pack, one copy per lane. `step` and
/// `prepare` are templates over V, deduced from the State; on packed lanes each part steps its whole State over V, as
/// Poly steps a poly kernel. A part's event reaches it through @ref part_event. For any other chain V is never
/// deduced and the forms are its own.
///
/// **Per block.** `prepare(c, s, n)` runs each declaring part's `prepare` on its own Coefficients and State, once, in
/// part order; its Block is every part's Block, an EmptyBlock for a part without one. A chain declares `prepare` and
/// `Block` only when some part declares a `prepare`, so a chain of parts without one costs its wrapper nothing per
/// block.
///
/// **Step.** `step` takes the first part's arity: none, one or two inputs. It steps part 0 on them, then each later
/// part on the value the part before it returned: `y = K1::step<M1>(c1, b1, s1, x…); y = K2::step<M2>(c2, b2, s2,
/// y); …`. Each part steps its own State in the order it would alone, so a chain is bit for bit its parts run as
/// separate passes. Each frame is read whole by its wrapper before any of it is written, so a chain renders in place
/// as it does out of place.
///
/// **Reset.** `reset` is declared only when some part has a reset quirk; it runs each part's quirk, or sets a part
/// without one to `State{}`. Without it the wrappers assign `State{}`, which is every part's `State{}`.
/// `reset_part<I>` resets part @c I alone, the same way, as an event through a wrapper's `on`:
/// `mono.on<&Chain<…>::reset_part<I>>()`.
///
/// **Modes.** A chain's mode is its moded parts' modes together: `Mode` holds one per moded part (one with `Modes`;
/// a @ref Pinned part has none), in part order, member @c J at `get<J>(mode)`. It is a structural aggregate with a
/// defaulted `==`, so it is a template argument as an enumerator is, and with_mode dispatches on it once per block,
/// matching every member, with no index to keep in step with the list. `Modes` is the product of the moded parts'
/// ModeLists, row-major in part order: part 0's mode changes slowest. Its first entry, which `Coefficients{}` and
/// `Mode{}` hold, is every moded part's first listed mode, its default. `coefficients` assembles `mode` from each
/// moded part's own `coefficients(settings).mode`, and `step<M>` steps each moded part at its member of @p M. The
/// product is every combination, each a loop of its own: keep it small, and leave a part with many modes its own
/// component. A chain with no moded part declares no `Mode` or `Modes`, and its step is unmoded.
///
/// **Op-major.** A chain declares no `kLanePackable`: `Linked` steps it lane by lane.
///
/// **Loop state.** A part declaring `kLoopState` steps on that member of its State; the chain declares none, so a
/// wrapper copies the chain's whole State around its loop.
///
/// **Inlining.** Every member is `[[gnu::always_inline]]`, so at the app's inline threshold of 5, which inlines only
/// the cheapest calls, the parts' steps land in the wrapper's loop with no call between them.
template <kernel... Ks>
requires chainable<Ks...>
struct Chain
    : detail::ChainTypes<Ks...>::ModesBase,
      detail::ChainTypes<Ks...>::Over,
      detail::ChainPrepare<(detail::declares_prepare<Ks> || ...), typename detail::ChainTypes<Ks...>::Over, Ks...>,
      detail::ChainReset<(detail::declares_reset<Ks> || ...), Ks...> {
private:
	/// @brief The chain's mode types, Coefficients and value forms.
	using Types = detail::ChainTypes<Ks...>;
	/// @brief Whether some part is moded, and so the chain is.
	static constexpr bool kModed = Types::kModed;
	/// @brief What `step` is templated on: the chain's `Mode`, or NoMode when it has none.
	using StepMode = typename Types::StepMode;
	/// @brief The chain's Coefficients over V: the parts' own over V for a chain of poly kernels, else Coefficients.
	template <typename V>
	using CoefficientsOf = Types::Over::template CoefficientsOf<V>;
	/// @brief The chain's State over V: the parts' own over V for a chain of poly kernels, else State.
	template <typename V>
	using StateOf = Types::Over::template StateOf<V>;
	/// @brief What a step over V takes and returns: V for a chain of poly kernels, else the step type.
	template <typename V>
	using StepOf = Types::Over::template StepOf<V>;

public:
	/// @brief Part @p I's kernel type.
	template <std::size_t I>
	using part_t = std::tuple_element_t<I, std::tuple<Ks...>>;

	using value_type = typename part_t<0>::value_type; ///< Every part's value type.
	using step_type = typename part_t<0>::step_type;   ///< Every part's step type.

	using Settings = Parts<typename Ks::Settings...>; ///< Each part's settings, part 0 first.
	/// @brief Each part's coefficients, part 0 first, at `get<I>(c)`; beside them, for a moded chain, the `mode` the
	/// wrapper dispatches on.
	using Coefficients = typename Types::Coefficients;
	using State = Parts<typename Ks::State...>; ///< Each part's state, part 0 first.

private:
	/// @brief Whether some part declares a `prepare`, and so the chain does.
	static constexpr bool kPrepares = (detail::declares_prepare<Ks> || ...);
	/// @brief Every part's Block, part 0 first.
	using Blocks = Parts<kernel_block_t<Ks>...>;
	/// @brief The Block a step receives: every part's when the chain prepares, else an EmptyBlock.
	using StepBlock = std::conditional_t<kPrepares, Blocks, EmptyBlock>;
	/// @brief The first part, whose arity the chain takes.
	using First = part_t<0>;
	/// @brief How many parts are moded.
	static constexpr std::size_t kModedCount = (std::size_t{detail::has_modes<Ks>} + ... + 0);
	/// @brief Whether each part is moded, part order.
	static constexpr std::array<bool, sizeof...(Ks)> kPartModed{detail::has_modes<Ks>...};
	/// @brief Each moded part's index, part order: moded part @c J is part `kModedParts[J]`.
	static constexpr std::array<std::size_t, kModedCount> kModedParts = [] {
		std::array<std::size_t, kModedCount> parts{};
		std::size_t j = 0;
		for (std::size_t i = 0; i < kPartModed.size(); ++i) {
			if (kPartModed[i]) {
				parts[j++] = i;
			}
		}
		return parts;
	}();
	/// @brief Each part's position among the moded parts: a moded part @c P's mode is member `kModedIndex[P]` of the
	/// chain's.
	static constexpr std::array<std::size_t, sizeof...(Ks)> kModedIndex = [] {
		std::array<std::size_t, sizeof...(Ks)> index{};
		std::size_t j = 0;
		for (std::size_t i = 0; i < kPartModed.size(); ++i) {
			index[i] = j;
			if (kPartModed[i]) {
				++j;
			}
		}
		return index;
	}();

public:
	/// @brief Each part's coefficients for its own settings; for a moded chain, the mode assembled from the moded
	/// parts' own coefficients.
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficients(const Settings& settings) {
		return coefficients_of(settings, std::index_sequence_for<Ks...>{});
	}

	/// @brief Part @p I to its reset State, every other part untouched: part @p I's reset quirk, or `State{}`.
	/// @details An event: a wrapper runs it through `on`, on each lane's State in turn.
	template <std::size_t I>
	requires(I < sizeof...(Ks))
	[[gnu::always_inline]] static void reset_part(State& s) {
		detail::reset_state<part_t<I>>(get<I>(s));
	}

	/// @brief Generate one step at mode @p M: part 0 generates, each later part processes.
	/// @tparam M The chain's mode for a moded chain, each moded part stepping at its member; NoMode::NONE, the
	/// default, for a chain without modes.
	/// @tparam V For a chain of poly kernels, the value type, deduced from @p s: f32, or Pack<f32, N> under Poly and
	/// Bank. Unused for any other chain.
	template <StepMode M = StepMode{}, typename V = value_type>
	[[nodiscard]] [[gnu::always_inline]] static StepOf<V> step(const CoefficientsOf<V>& c, const StepBlock& b,
	                                                           StateOf<V>& s)
	requires generator_kernel<First>
	{
		return step_all<M, V>(c, b, s);
	}

	/// @brief Process one step at mode @p M: part 0 takes @p x, each later part what the part before it returned.
	/// @tparam M The chain's mode for a moded chain, each moded part stepping at its member; NoMode::NONE, the
	/// default, for a chain without modes.
	/// @tparam V For a chain of poly kernels, the value type, deduced from @p s. Unused for any other chain.
	template <StepMode M = StepMode{}, typename V = value_type>
	[[nodiscard]] [[gnu::always_inline]] static StepOf<V> step(const CoefficientsOf<V>& c, const StepBlock& b,
	                                                           StateOf<V>& s, std::type_identity_t<StepOf<V>> x)
	requires processor_kernel<First>
	{
		return step_all<M, V>(c, b, s, x);
	}

	/// @brief Mix one step at mode @p M: part 0 mixes @p a and @p b2, each later part processes.
	/// @tparam M The chain's mode for a moded chain, each moded part stepping at its member; NoMode::NONE, the
	/// default, for a chain without modes.
	/// @tparam V Unused: a mixer is no poly kernel.
	template <StepMode M = StepMode{}, typename V = value_type>
	[[nodiscard]] [[gnu::always_inline]] static StepOf<V> step(const CoefficientsOf<V>& c, const StepBlock& b,
	                                                           StateOf<V>& s, std::type_identity_t<StepOf<V>> a,
	                                                           std::type_identity_t<StepOf<V>> b2)
	requires mixer_kernel<First>
	{
		return step_all<M, V>(c, b, s, a, b2);
	}

private:
	/// @brief @ref coefficients, with the parts' indices.
	template <std::size_t... I>
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficients_of(const Settings& settings,
	                                                                         std::index_sequence<I...> /*parts*/) {
		Coefficients c;
		((get<I>(c) = Ks::coefficients(get<I>(settings))), ...);
		if constexpr (kModed) {
			c.mode = mode_of(c, std::make_index_sequence<kModedCount>{});
		}
		return c;
	}

	/// @brief The chain's mode: each moded part's mode from its own coefficients, moded part 0 first.
	template <std::size_t... J>
	[[nodiscard]] [[gnu::always_inline]] static StepMode mode_of(const Coefficients& c,
	                                                             std::index_sequence<J...> /*moded*/) {
		return StepMode{get<kModedParts[J]>(c).mode...};
	}

	/// @brief One step of every part at mode @p M, with every part's Block: the chain's own, or EmptyBlocks when it
	/// prepares none.
	template <StepMode M, typename V, typename... X>
	[[nodiscard]] [[gnu::always_inline]] static StepOf<V> step_all(const CoefficientsOf<V>& c, const StepBlock& b,
	                                                               StateOf<V>& s, X... x) {
		if constexpr (kPrepares) {
			return step_parts<M, V>(c, b, s, std::make_index_sequence<sizeof...(Ks) - 1>{}, x...);
		}
		else {
			const Blocks none{};
			return step_parts<M, V>(c, none, s, std::make_index_sequence<sizeof...(Ks) - 1>{}, x...);
		}
	}

	/// @brief Part 0's step on @p x, then part I + 1's on the value before it, for each I in order.
	template <StepMode M, typename V, std::size_t... I, typename... X>
	[[nodiscard]] [[gnu::always_inline]] static StepOf<V> step_parts(const CoefficientsOf<V>& c, const Blocks& b,
	                                                                 StateOf<V>& s, std::index_sequence<I...> /*later*/,
	                                                                 X... x) {
		StepOf<V> y = step_part<M, V, 0>(c, b, s, x...);
		// A comma fold runs its operands left to right: each part steps on the output of the one before it.
		((y = step_part<M, V, I + 1>(c, b, s, y)), ...);
		return y;
	}

	/// @brief Part @p P's step on @p x, with its own Coefficients, Block and State, at its member of @p M when it is
	/// moded, else unmoded. On the chain's own State the part steps its loop state (@ref loop_state_t), as a wrapper
	/// steps it; on packed lanes it steps its whole State over V, as Poly and Bank step a poly kernel.
	template <StepMode M, typename V, std::size_t P, typename... X>
	[[nodiscard]] [[gnu::always_inline]] static StepOf<V> step_part(const CoefficientsOf<V>& c, const Blocks& b,
	                                                                StateOf<V>& s, X... x) {
		using K = part_t<P>;
		if constexpr (std::same_as<StateOf<V>, State>) {
			if constexpr (detail::has_modes<K>) {
				return kernel_ops::step_at<K, get<kModedIndex[P]>(M)>(get<P>(c), get<P>(b),
				                                                      kernel_ops::loop_state<K>(get<P>(s)), x...);
			}
			else {
				return kernel_ops::step_at<K, kernel_ops::NoMode::NONE>(get<P>(c), get<P>(b),
				                                                        kernel_ops::loop_state<K>(get<P>(s)), x...);
			}
		}
		else if constexpr (detail::has_modes<K>) {
			return K::template step<get<kModedIndex[P]>(M)>(get<P>(c), get<P>(b), get<P>(s), x...);
		}
		else {
			return K::step(get<P>(c), get<P>(b), get<P>(s), x...);
		}
	}
};

/// @brief Event @p Event of part @p I, run as an event of a chain: `Event(get<I>(c), get<I>(s), args…)`, or
/// `Event(get<I>(s), args…)` for an event taking the State alone.
/// @details A wrapper runs it through `on` as any event of the chain: `mono.on<part_event<2, &Vca<E>::gate_on>>()`,
/// or on one lane of a Poly or Bank, `poly.on<part_event<2, &Vca<E>::gate_on>>(lane)`. Part @c I's Coefficients
/// carry no mode of their own in a chain (the chain's `mode` is the one dispatched on), so an event reading its
/// part's `mode` reads the default.
template <std::size_t I, auto Event>
struct PartEvent {
	/// @brief @p Event on part @p I's Coefficients and State.
	template <typename C, typename S, typename... A>
	[[gnu::always_inline]] constexpr auto operator()(const C& c, S& s, A&&... args) const
	    -> decltype(Event(get<I>(c), get<I>(s), std::forward<A>(args)...)) {
		return Event(get<I>(c), get<I>(s), std::forward<A>(args)...);
	}

	/// @brief @p Event on part @p I's State alone.
	template <typename S, typename... A>
	[[gnu::always_inline]] constexpr auto operator()(S& s, A&&... args) const
	    -> decltype(Event(get<I>(s), std::forward<A>(args)...)) {
		return Event(get<I>(s), std::forward<A>(args)...);
	}
};

/// @brief Event @p Event of part @p I of a chain, as an event of the chain: see @ref PartEvent.
template <std::size_t I, auto Event>
// NOLINTNEXTLINE(readability-identifier-naming): an event, named in snake case as `reset_part` is.
inline constexpr PartEvent<I, Event> part_event{};

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

/// @brief A pinned kernel's `kLoopState`, present only when K declares one.
template <bool Declares, typename K>
struct PinnedLoopState {};

/// @brief A pinned kernel's `kLoopState`: K's, a member of the State Pinned shares with K.
template <typename K>
struct PinnedLoopState<true, K> {
	static constexpr auto kLoopState = K::kLoopState; ///< K's loop state.
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
/// @details Its Settings, Coefficients, State and Block are K's, and its `coefficients`, `prepare`, `reset` and
/// `kLoopState` are K's, declared exactly when K declares them. The Coefficients keep K's `mode` member, which
/// `coefficients` sets to @p M and the step never reads: it is fixed at @p M. Pinned declares no `Modes`, so it is no
/// moded kernel, and a chain dispatches on nothing for it. It declares no `kLanePackable`, so `Linked` steps it lane by
/// lane. Its step forwards whatever K's takes, the counted partial step included, and it declares K's
/// kPartialStepAdvancesByCount and kStateInPlace.
template <kernel K, auto M>
requires detail::has_modes<K> && std::same_as<decltype(M), std::remove_cv_t<decltype(K::Coefficients::mode)>>
             && (detail::lists_mode(typename K::Modes{}, M))
struct Pinned : detail::PinnedPrepare<detail::declares_prepare<K>, K>,
                detail::PinnedReset<detail::declares_reset<K>, K>,
                detail::PinnedLoopState<detail::declares_loop_state<K>, K> {
	using value_type = typename K::value_type;     ///< K's value type.
	using step_type = typename K::step_type;       ///< K's step type.
	using Settings = typename K::Settings;         ///< K's settings.
	using Coefficients = typename K::Coefficients; ///< K's coefficients; their `mode` is never read.
	using State = typename K::State;               ///< K's state.

	/// @brief Whether K's partial last vector advances by the active lanes.
	static constexpr bool kPartialStepAdvancesByCount = partial_step_advances_by_count_v<K>;
	/// @brief Whether the wrappers step K's loop state in place: K's choice.
	static constexpr bool kStateInPlace = detail::state_in_place_v<K>;

	/// @brief K's coefficients for @p settings, with @p M as their mode whatever the settings say.
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficients(const Settings& settings) {
		Coefficients c = K::coefficients(settings);
		c.mode = M;
		return c;
	}

	/// @brief One step of K at mode @p M, with whatever inputs K's step at its form takes.
	template <typename... X>
	requires requires(const Coefficients& c, const kernel_block_t<K>& b, loop_state_t<K>& s, X... x) {
		K::template step<M>(c, b, s, x...);
	}
	[[nodiscard]] [[gnu::always_inline]] static step_type step(const Coefficients& c, const kernel_block_t<K>& b,
	                                                           loop_state_t<K>& s, X... x) {
		return K::template step<M>(c, b, s, x...);
	}
};

} // namespace deluge::dsp
