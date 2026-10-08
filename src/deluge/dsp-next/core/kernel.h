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
/// choose between them. The partial-step overloads take their count as an @ref ActiveLanes tag rather than an
/// integer, so a counted processor step `step(c, b, s, x, ActiveLanes)` never reads as a mixer step, whatever the
/// step type converts to.
///
/// **Checked at one mode.** A moded kernel's `step` (or `process`) is checked at the first mode of its `Modes` only.
/// A step that is ill-formed at another mode is reported where the wrapper instantiates it, when `Mono` or `Stereo`
/// is built for the kernel.
namespace deluge::dsp {

/// @brief A kernel's per-block values when it declares none.
struct EmptyBlock {};

/// @brief The active lane count of a partial last vector, passed to a kernel's counted step.
/// @details A kernel declaring `kPartialStepAdvancesByCount` provides `step(c, b, s, x, ActiveLanes)` (processor) or
/// `step(c, b, s, ActiveLanes)` (generator), which the wrapper calls for the partial last vector so that the state
/// advances by @ref n lanes rather than a whole vector. It is a tag, not an integer, so no step type converts to it.
struct ActiveLanes {
	std::size_t n; ///< How many leading lanes of the vector are live: at least 1, fewer than the vector's lanes.
};

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

/// @brief A `prepare` every probe has, so that naming `prepare` in a class derived from both it and a kernel is
/// ambiguous exactly when the kernel declares one.
struct PrepareFallback {
	/// @brief Never defined or called; it exists only to collide with a kernel's own `prepare`.
	static void prepare();
};

/// @brief Derives from a kernel and the fallback, so that `&PrepareProbe<K>::prepare` is ill-formed exactly when K
/// declares a member named `prepare`.
template <typename K>
struct PrepareProbe : K, PrepareFallback {};

/// @brief Whether K declares a member named `prepare`, of any kind: a function, an overload set, a template or a
/// non-static member.
/// @details It detects the name rather than a call, so a `prepare` of the wrong shape is seen here and rejected by
/// kernel_base, never skipped. The wrappers call `prepare` exactly when this holds. K must be a non-final class.
template <typename K>
concept declares_prepare = std::is_class_v<K> && !std::is_final_v<K> && !requires { &PrepareProbe<K>::prepare; };

/// @brief Whether K's `prepare` is exactly `static kernel_block_t<K> prepare(const Coefficients&, State&,
/// std::size_t)`: a static, non-template function of that type, or an overload set containing one.
template <typename K>
concept prepares_exactly = requires {
	static_cast<typename kernel_block<K>::type (*)(const typename K::Coefficients&, typename K::State&, std::size_t)>(
	    &K::prepare);
};

/// @brief Whether moded K's `step<first mode>` is callable with Coefficients, Block and State lvalues followed by
/// @p X, returning `step_type`.
template <typename K, typename... X>
concept moded_steps_with = has_modes<K>
                           && requires(const typename K::Coefficients& c, const typename kernel_block<K>::type& b,
                                       typename K::State& s, X... x) {
	                              {
		                              K::template step<first_mode_v<typename K::Modes>>(c, b, s, x...)
	                              } -> std::same_as<typename K::step_type>;
                              };

/// @brief Whether K, which has no modes, has a non-template `step` callable with Coefficients, Block and State
/// lvalues followed by @p X, returning `step_type`.
template <typename K, typename... X>
concept unmoded_steps_with = !has_modes<K>
                             && requires(const typename K::Coefficients& c, const typename kernel_block<K>::type& b,
                                         typename K::State& s, X... x) {
	                                { K::step(c, b, s, x...) } -> std::same_as<typename K::step_type>;
                                };

/// @brief Whether K's `step` is callable with Coefficients, Block and State lvalues followed by @p X, returning
/// `step_type`. A moded kernel is checked at its first mode; a kernel without modes at its non-template `step`.
template <typename K, typename... X>
concept steps_with = moded_steps_with<K, X...> || unmoded_steps_with<K, X...>;

/// @brief Whether K provides the generator step: step(c, b, s).
template <typename K>
concept steps_as_generator = steps_with<K>;

/// @brief Whether K provides the processor step: step(c, b, s, x).
template <typename K>
concept steps_as_processor = steps_with<K, typename K::step_type>;

/// @brief Whether K provides the mixer step: step(c, b, s, a, b2).
template <typename K>
concept steps_as_mixer = steps_with<K, typename K::step_type, typename K::step_type>;

/// @brief Whether moded K's `process<first mode>(c, s, Signal<value_type>, Buffer<value_type>)` is callable.
template <typename K>
concept moded_processes_blocks = has_modes<K>
                                 && requires(const typename K::Coefficients& c, typename K::State& s,
                                             Signal<typename K::value_type> in, Buffer<typename K::value_type> out) {
	                                    {
		                                    K::template process<first_mode_v<typename K::Modes>>(c, s, in, out)
	                                    } -> std::same_as<void>;
                                    };

/// @brief Whether K, which has no modes, has a non-template `process(c, s, Signal<value_type>, Buffer<value_type>)`.
template <typename K>
concept unmoded_processes_blocks = !has_modes<K>
                                   && requires(const typename K::Coefficients& c, typename K::State& s,
                                               Signal<typename K::value_type> in, Buffer<typename K::value_type> out) {
	                                      { K::process(c, s, in, out) } -> std::same_as<void>;
                                      };

/// @brief Whether K provides the block-staged form: process(c, s, Signal<value_type>, Buffer<value_type>). A moded
/// kernel is checked at its first mode; a kernel without modes at its non-template `process`.
template <typename K>
concept processes_blocks = moded_processes_blocks<K> || unmoded_processes_blocks<K>;

/// @brief How many of the four forms K provides; a kernel provides exactly one.
template <typename K>
inline constexpr int kernel_form_count =
    static_cast<int>(steps_as_generator<K>) + static_cast<int>(steps_as_processor<K>)
    + static_cast<int>(steps_as_mixer<K>) + static_cast<int>(processes_blocks<K>);

/// @brief Whether K is a non-final class declaring the types every kernel has.
template <typename K>
concept declares_kernel_types = std::is_class_v<K> && !std::is_final_v<K> && requires {
	typename K::value_type;
	typename K::step_type;
	typename K::Settings;
	typename K::Coefficients;
	typename K::State;
};

/// @brief Whether K has `static Coefficients coefficients(const Settings&)`.
template <typename K>
concept computes_coefficients = requires(const typename K::Settings& settings) {
	{ K::coefficients(settings) } -> std::same_as<typename K::Coefficients>;
};

/// @brief Whether K declares a `Block`.
template <typename K>
concept declares_block = requires { typename K::Block; };

/// @brief Whether K's `prepare`, if it declares one, is exactly the stated signature, and whether a declared `Block`
/// has a `prepare` to make it.
template <typename K>
concept prepare_well_formed =
    (!declares_prepare<K> || prepares_exactly<K>) && (!declares_block<K> || declares_prepare<K>);

} // namespace detail

/// @brief K::Block if K declares one, else EmptyBlock.
template <typename K>
using kernel_block_t = typename detail::kernel_block<K>::type;

/// @brief Whether K's partial last vector advances its state by the active lane count (true) or a whole vector.
/// @details False unless K declares `static constexpr bool kPartialStepAdvancesByCount`. A vector kernel whose state
/// persists across blocks declares it true and provides the counted `step` overload taking a trailing ActiveLanes.
/// The generator and processor concepts require that overload when this is true; the mixer and staged concepts
/// reject the declaration, having no partial step.
template <typename K>
inline constexpr bool partial_step_advances_by_count = false;

/// @brief Whether K's partial last vector advances its state by the active lane count (true) or a whole vector.
template <typename K>
requires std::same_as<std::remove_cv_t<decltype(K::kPartialStepAdvancesByCount)>, bool>
inline constexpr bool partial_step_advances_by_count<K> = K::kPartialStepAdvancesByCount;

/// @brief The shape every kernel has: value/step types, Settings → Coefficients (pure), State, optional prepare.
/// @details Requires:
/// - a non-final class declaring `value_type`, `step_type` and `Settings`;
/// - `Coefficients`, `State` and kernel_block_t<K> as plain value structs (default-constructible, trivially
///   copyable);
/// - `static Coefficients coefficients(const Settings&)`;
/// - when K declares any member named `prepare`, exactly `static kernel_block_t<K> prepare(const Coefficients&,
///   State&, std::size_t n)`, called once per block of @c n samples. A template, non-static or mismatched `prepare`
///   fails here rather than being skipped;
/// - when K declares a `Block`, a `prepare` to make it.
template <typename K>
concept kernel_base = detail::declares_kernel_types<K> && detail::plain_value<typename K::Coefficients>
                      && detail::plain_value<typename K::State> && detail::plain_value<kernel_block_t<K>>
                      && detail::computes_coefficients<K> && detail::prepare_well_formed<K>;

/// @brief A kernel generating its output: `step(c, b, s) -> step_type`, and no other form. When K declares
/// kPartialStepAdvancesByCount, also the counted `step(c, b, s, ActiveLanes) -> step_type`.
/// @details Checked at K's first mode, or at the non-template step when K has no modes.
template <typename K>
concept generator_kernel = kernel_base<K> && detail::kernel_form_count<K> == 1 && detail::steps_as_generator<K>
                           && (!partial_step_advances_by_count<K> || detail::steps_with<K, ActiveLanes>);

/// @brief A kernel transforming its input: `step(c, b, s, x) -> step_type`, and no other form. When K declares
/// kPartialStepAdvancesByCount, also the counted `step(c, b, s, x, ActiveLanes) -> step_type`.
/// @details Checked at K's first mode, or at the non-template step when K has no modes.
template <typename K>
concept processor_kernel =
    kernel_base<K> && detail::kernel_form_count<K> == 1 && detail::steps_as_processor<K>
    && (!partial_step_advances_by_count<K> || detail::steps_with<K, typename K::step_type, ActiveLanes>);

/// @brief A kernel combining two inputs: `step(c, b, s, a, b2) -> step_type`, and no other form.
/// @details Checked at K's first mode, or at the non-template step when K has no modes. A mixer has no counted
/// partial step, so it must not declare kPartialStepAdvancesByCount.
template <typename K>
concept mixer_kernel = kernel_base<K> && detail::kernel_form_count<K> == 1
                       && detail::steps_as_mixer<K> && !partial_step_advances_by_count<K>;

/// @brief A block-staged kernel: process(c, s, Signal<value_type>, Buffer<value_type>) instead of step.
/// @details For arithmetic that reads a whole block, shapes it, then writes the whole block (the delay's tape I/O);
/// every other kernel is written as a `step`. It provides no `step` at any arity and has no partial step, so it must
/// not declare kPartialStepAdvancesByCount. A moded staged kernel's `process` is a template on the mode, checked at
/// its first mode.
template <typename K>
concept staged_kernel = kernel_base<K> && detail::kernel_form_count<K> == 1
                        && detail::processes_blocks<K> && !partial_step_advances_by_count<K>;

/// @brief A kernel of any form; a class satisfies at most one of the four form concepts.
template <typename K>
concept kernel = generator_kernel<K> || processor_kernel<K> || mixer_kernel<K> || staged_kernel<K>;

/// @brief Whether K has modes (K::Modes is a ModeList and Coefficients has a `mode` member).
/// @details The `mode` member has the list's enumeration type; the wrapper dispatches on it once per block with
/// with_mode. All of a kernel's modes share one State, and a moded kernel's `step` or `process` is a template on the
/// mode.
template <typename K>
concept moded_kernel = kernel<K> && detail::has_modes<K>;

} // namespace deluge::dsp
