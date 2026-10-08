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

#include "dsp-next/core/mode.h"
#include "dsp-next/core/types.h"
#include <concepts>
#include <cstddef>
#include <type_traits>

/// @file
/// @brief The kernel concepts: the shape of a stateless type holding a component's arithmetic.
///
/// A kernel declares `value_type` (a block's sample), `step_type` (what `step` takes and returns: a sample or a
/// vector of them), `Settings` (what the owner sets), `Coefficients` and `State` (plain value structs; `State{}` is
/// the reset state), and optionally `Block`, `Mode` and `Modes`. It provides a pure `coefficients(settings)`, an
/// optional once-per-block `prepare`, and either a `step` at one of three arities or, for block-staged arithmetic, a
/// `process`. The wrappers (`Mono`, `Stereo`) are the only loops; a kernel holds no data of its own.
///
/// **One form per kernel.** A kernel takes exactly one of four forms: `step(c, b, s)` (generator),
/// `step(c, b, s, x)` (processor), `step(c, b, s, a, b2)` (mixer), or `process(c, s, in, out)` (staged). Each form
/// concept counts the forms a class provides and requires that count to be one, so a class providing two (say a
/// processor `step` overloaded with a mixer `step`) satisfies no form and no `kernel`, and a wrapper never has to
/// choose between them. A partial-step overload `step(c, b, s, x, count)` therefore must not be callable at the
/// mixer arity: its `std::size_t` count must not accept a `step_type`, which holds for the vector step types
/// (`Argon<q31>`, `StereoVector`) that declare one.
namespace deluge::dsp {

/// @brief A kernel's per-block values when it declares none.
struct EmptyBlock {};

namespace detail {

/// @brief Resolves to K::Block when K declares one.
template <typename K>
struct kernel_block {
	using type = EmptyBlock; ///< K declares no Block.
};

/// @brief Resolves to K::Block when K declares one.
template <typename K>
requires requires { typename K::Block; }
struct kernel_block<K> {
	using type = typename K::Block; ///< K's own per-block values.
};

/// @brief Whether @p T is a ModeList.
template <typename T>
inline constexpr bool is_mode_list = false;

/// @brief Whether @p T is a ModeList.
template <auto... Ms>
inline constexpr bool is_mode_list<ModeList<Ms...>> = true;

/// @brief The first mode of a ModeList: the one the kernel concepts check `step` at.
template <typename List>
struct first_mode;

/// @brief The first mode of a ModeList: the one the kernel concepts check `step` at.
template <auto M0, auto... Ms>
struct first_mode<ModeList<M0, Ms...>> {
	static constexpr auto value = M0; ///< The list's first enumerator.
};

/// @brief The first mode of the ModeList @p List.
template <typename List>
inline constexpr auto first_mode_v = first_mode<List>::value;

/// @brief Whether K has modes: K::Modes is a ModeList, and K::Coefficients has a `mode` member of the list's
/// enumeration type, which carries the mode chosen by `coefficients()` to the wrapper's per-block dispatch.
template <typename K>
concept has_modes = requires {
	typename K::Modes;
	typename K::Coefficients;
} && is_mode_list<typename K::Modes> && requires {
	requires std::same_as<std::remove_cv_t<decltype(K::Coefficients::mode)>,
	                      std::remove_cv_t<decltype(first_mode_v<typename K::Modes>)>>;
};

/// @brief A plain value struct: default-constructible and trivially copyable, so it can be reset, copied and slotted
/// as bytes.
template <typename T>
concept plain_value = std::default_initializable<T> && std::is_trivially_copyable_v<T>;

/// @brief Whether K declares a `prepare` at all, whatever its signature: a single function named `prepare`, or any
/// overload set callable with the stated arguments. The wrappers call `prepare` exactly when this holds.
template <typename K>
concept declares_prepare = requires {
	&K::prepare;
} || requires(const typename K::Coefficients& c, typename K::State& s, std::size_t n) { K::prepare(c, s, n); };

/// @brief Whether K's `step` is callable with Coefficients, Block and State lvalues followed by @p X, returning
/// `step_type`. A moded kernel is checked at its first mode; a kernel without modes at its non-template `step`.
template <typename K, typename... X>
concept steps_with =
    (has_modes<K> && requires(const typename K::Coefficients& c, const typename kernel_block<K>::type& b, typename K::State& s, X... x) {
	    { K::template step<first_mode_v<typename K::Modes>>(c, b, s, x...) } -> std::same_as<typename K::step_type>;
    }) || (!has_modes<K> && requires(const typename K::Coefficients& c, const typename kernel_block<K>::type& b, typename K::State& s, X... x) {
	    { K::step(c, b, s, x...) } -> std::same_as<typename K::step_type>;
    });

/// @brief Whether K provides the generator step: step(c, b, s).
template <typename K>
concept steps_as_generator = steps_with<K>;

/// @brief Whether K provides the processor step: step(c, b, s, x).
template <typename K>
concept steps_as_processor = steps_with<K, typename K::step_type>;

/// @brief Whether K provides the mixer step: step(c, b, s, a, b2).
template <typename K>
concept steps_as_mixer = steps_with<K, typename K::step_type, typename K::step_type>;

/// @brief Whether K provides the block-staged form: process(c, s, Signal<value_type>, Buffer<value_type>).
template <typename K>
concept processes_blocks = requires(const typename K::Coefficients& c, typename K::State& s,
                                    Signal<typename K::value_type> in, Buffer<typename K::value_type> out) {
	{ K::process(c, s, in, out) } -> std::same_as<void>;
};

/// @brief How many of the four forms K provides; a kernel provides exactly one.
template <typename K>
inline constexpr int kernel_form_count =
    static_cast<int>(steps_as_generator<K>) + static_cast<int>(steps_as_processor<K>)
    + static_cast<int>(steps_as_mixer<K>) + static_cast<int>(processes_blocks<K>);

} // namespace detail

/// @brief K::Block if K declares one, else EmptyBlock.
template <typename K>
using kernel_block_t = typename detail::kernel_block<K>::type;

/// @brief Whether K's partial last vector advances its state by the active lane count (true) or a whole vector.
/// @details False unless K declares `static constexpr bool kPartialStepAdvancesByCount`. A vector kernel whose state
/// persists across blocks declares it true and provides a `step` overload taking the active lane count as a trailing
/// `std::size_t`.
template <typename K>
inline constexpr bool partial_step_advances_by_count = false;

/// @brief Whether K's partial last vector advances its state by the active lane count (true) or a whole vector.
template <typename K>
requires std::same_as<std::remove_cv_t<decltype(K::kPartialStepAdvancesByCount)>, bool>
inline constexpr bool partial_step_advances_by_count<K> = K::kPartialStepAdvancesByCount;

/// @brief The shape every kernel has: value/step types, Settings → Coefficients (pure), State, optional prepare.
/// @details Requires `value_type`, `step_type` and `Settings`; `Coefficients` and `State` as plain value structs
/// (default-constructible, trivially copyable); `static Coefficients coefficients(const Settings&)`; and, when K
/// declares a `prepare`, `static kernel_block_t<K> prepare(const Coefficients&, State&, std::size_t n)`, called once
/// per block of @c n samples.
template <typename K>
concept kernel_base =
    requires {
	    typename K::value_type;
	    typename K::step_type;
	    typename K::Settings;
	    typename K::Coefficients;
	    typename K::State;
    } && detail::plain_value<typename K::Coefficients> && detail::plain_value<typename K::State>
    && requires(const typename K::Settings& settings) {
	       { K::coefficients(settings) } -> std::same_as<typename K::Coefficients>;
       } && (!detail::declares_prepare<K> || requires(const typename K::Coefficients& c, typename K::State& s, std::size_t n) {
	       { K::prepare(c, s, n) } -> std::same_as<kernel_block_t<K>>;
       });

/// @brief A kernel generating its output: `step(c, b, s) -> step_type`, and no other form.
/// @details Checked at K's first mode, or at the non-template step when K has no modes.
template <typename K>
concept generator_kernel = kernel_base<K> && detail::kernel_form_count<K> == 1 && detail::steps_as_generator<K>;

/// @brief A kernel transforming its input: `step(c, b, s, x) -> step_type`, and no other form.
/// @details Checked at K's first mode, or at the non-template step when K has no modes.
template <typename K>
concept processor_kernel = kernel_base<K> && detail::kernel_form_count<K> == 1 && detail::steps_as_processor<K>;

/// @brief A kernel combining two inputs: `step(c, b, s, a, b2) -> step_type`, and no other form.
/// @details Checked at K's first mode, or at the non-template step when K has no modes.
template <typename K>
concept mixer_kernel = kernel_base<K> && detail::kernel_form_count<K> == 1 && detail::steps_as_mixer<K>;

/// @brief A block-staged kernel: process(c, s, Signal<value_type>, Buffer<value_type>) instead of step.
/// @details For arithmetic that reads a whole block, shapes it, then writes the whole block (the delay's tape I/O);
/// every other kernel is written as a `step`. It provides no `step` at any arity.
template <typename K>
concept staged_kernel = kernel_base<K> && detail::kernel_form_count<K> == 1 && detail::processes_blocks<K>;

/// @brief A kernel of any form; a class satisfies at most one of the four form concepts.
template <typename K>
concept kernel = generator_kernel<K> || processor_kernel<K> || mixer_kernel<K> || staged_kernel<K>;

/// @brief Whether K has modes (K::Modes is a ModeList and Coefficients has a `mode` member).
/// @details The `mode` member has the list's enumeration type; the wrapper dispatches on it once per block with
/// with_mode. All of a kernel's modes share one State.
template <typename K>
concept moded_kernel = kernel<K> && detail::has_modes<K>;

} // namespace deluge::dsp
