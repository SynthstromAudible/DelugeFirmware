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

#include "dsp-next/core/lane_pack.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/stereo_vector.h"
#include "dsp-next/core/types.h"
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief The kernel concepts: the shape of a stateless type holding a component's arithmetic.
///
/// A kernel declares `value_type` (a block's sample), `step_type` (what `step` takes and returns: a sample or a
/// vector of them), `Settings` (what the owner sets), `Coefficients` and `State` (plain value structs; `State{}` is
/// the reset state), and optionally `Block`, `Mode` and `Modes`. It provides a pure `coefficients(settings)`, an
/// optional once-per-block `prepare`, and either a `step` at one of three arities, or, for block-staged arithmetic,
/// a `process`, or, for a block-rate control signal, an `advance`. The wrappers (`Mono`, `Stereo`, `Control`) are the
/// only loops; a kernel holds no data of its own. A wrapper calls a kernel only through @ref kernel_ops, which passes
/// the coefficients as a const reference so that the call resolves to the form's own signature; a composed kernel
/// calls its parts through the same functions.
///
/// **One form per kernel.** A kernel takes exactly one of five forms: `step(c, b, s)` (generator),
/// `step(c, b, s, x)` (processor), `step(c, b, s, a, b2)` (mixer), `process(c, s, in, out)` (staged), or
/// `advance(c, s, n)` (control). Each form concept counts the forms a class provides and requires that count to be
/// one, so a class providing two (say a processor `step` overloaded with a mixer `step`) satisfies no form and no
/// `kernel`, and a wrapper never has to choose between them. The partial-step overloads take their count as an @ref
/// ActiveLanes tag rather than an integer, so a counted processor step `step(c, b, s, x, ActiveLanes)` never reads as a
/// mixer step, whatever the step type converts to.
///
/// **Checked at one mode.** A moded kernel's `step` (or `process`) is checked at the first mode of its `Modes` only.
/// A step that is ill-formed at another mode is reported where the wrapper instantiates it, when `Mono` or `Stereo`
/// is built for the kernel. A control kernel's `advance` is checked at every listed mode: its exact signature is
/// checked by taking its address, which costs nothing per mode.
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

/// @brief Whether @p mode is one of the modes of the ModeList passed as the first argument.
template <auto... Ms, typename Mode>
constexpr bool lists_mode(ModeList<Ms...> /*list*/, Mode mode) {
	return ((mode == Ms) || ...);
}

/// @brief Whether moded K's default `Coefficients{}` names a mode in K's `Modes`, evaluated at compile time.
/// @details A default-constructed wrapper holds value-initialised Coefficients and dispatches on their `mode` at its
/// first block, so that mode must be listed: an unlisted one reaches the dispatch's unreachable fallthrough. A
/// `Coefficients{}` that is not a constant expression fails this too, since its mode cannot be checked.
template <typename K>
concept default_mode_listed = requires {
	typename std::bool_constant<lists_mode(typename K::Modes{}, typename K::Coefficients{}.mode)>;
} && lists_mode(typename K::Modes{}, typename K::Coefficients{}.mode);

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

/// @brief A `reset` every probe has, so that naming `reset` in a class derived from both it and a kernel is
/// ambiguous exactly when the kernel declares one.
struct ResetFallback {
	/// @brief Never defined or called; it exists only to collide with a kernel's own `reset`.
	static void reset();
};

/// @brief Derives from a kernel and the fallback, so that `&ResetProbe<K>::reset` is ill-formed exactly when K
/// declares a member named `reset`.
template <typename K>
struct ResetProbe : K, ResetFallback {};

/// @brief Whether K declares a member named `reset`, of any kind, detected by name as @ref declares_prepare is.
/// @details A `reset` of the wrong shape is seen here and rejected by kernel_base, never skipped. The wrappers call
/// `K::reset` exactly when this holds. K must be a non-final class.
template <typename K>
concept declares_reset = std::is_class_v<K> && !std::is_final_v<K> && !requires { &ResetProbe<K>::reset; };

/// @brief Whether K's `reset` is exactly `static void reset(State&)`.
/// @details Checked by converting `&K::reset` to a pointer to that function type, which picks a static function of
/// exactly that type from an overload set, or a function template whose parameters deduce to exactly that type. A
/// `reset` of any other type is rejected, not converted.
template <typename K>
concept resets_exactly = requires { static_cast<void (*)(typename K::State&)>(&K::reset); };

/// @brief Whether K's `prepare` is exactly `static kernel_block_t<K> prepare(const Coefficients&, State&,
/// std::size_t)`.
/// @details Checked as @ref resets_exactly is, by converting `&K::prepare` to a pointer to that function type: a
/// static function of exactly that type, or a function template whose parameters deduce to exactly that type, is
/// accepted. A template whose parameters cannot be deduced from it (one on a mode) and a `prepare` taking any other
/// type are rejected, never converted.
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

/// @brief The function type of a control kernel's `advance`: `value_type (const Coefficients&, State&, std::size_t)`.
template <typename K>
using advance_fn = typename K::value_type(const typename K::Coefficients&, typename K::State&, std::size_t);

/// @brief Whether moded K's `advance<M>` is exactly `static value_type advance(const Coefficients&, State&,
/// std::size_t)` at mode @p M: a static function template whose specialisation at @p M has that type.
template <typename K, auto M>
concept advances_exactly_at = requires { static_cast<advance_fn<K>*>(&K::template advance<M>); };

/// @brief Whether moded K's `advance<M>` has the exact signature at every mode of the ModeList @p List.
template <typename K, typename List>
inline constexpr bool advances_at_every_mode = false;

/// @brief Whether moded K's `advance<M>` has the exact signature at every mode of the ModeList @p List.
template <typename K, auto... Ms>
inline constexpr bool advances_at_every_mode<K, ModeList<Ms...>> = (advances_exactly_at<K, Ms> && ...);

/// @brief Whether moded K's `advance<M>` is exactly `static value_type advance(const Coefficients&, State&,
/// std::size_t)` at every listed mode.
template <typename K>
concept moded_advances_blocks = has_modes<K> && advances_at_every_mode<K, typename K::Modes>;

/// @brief Whether K, which has no modes, has exactly `static value_type advance(const Coefficients&, State&,
/// std::size_t)`.
/// @details Checked as @ref prepares_exactly is: a static function of exactly that type, or a function template whose
/// parameters deduce to exactly that type, is accepted; an `advance` taking any other type is rejected.
template <typename K>
concept unmoded_advances_blocks = !has_modes<K> && requires { static_cast<advance_fn<K>*>(&K::advance); };

/// @brief Whether K provides the control form: `advance(c, s, n)`, exactly `static value_type advance(const
/// Coefficients&, State&, std::size_t)`. A moded kernel's `advance` is a template on the mode with that signature at
/// every listed mode; a kernel without modes has an `advance` whose address converts to that exact function type.
template <typename K>
concept advances_blocks = moded_advances_blocks<K> || unmoded_advances_blocks<K>;

/// @brief How many of the five forms K provides; a kernel provides exactly one.
template <typename K>
inline constexpr int kernel_form_count = static_cast<int>(steps_as_generator<K>)
                                         + static_cast<int>(steps_as_processor<K>) + static_cast<int>(steps_as_mixer<K>)
                                         + static_cast<int>(processes_blocks<K>) + static_cast<int>(advances_blocks<K>);

/// @brief Whether K is a non-final class declaring the types every kernel has.
template <typename K>
concept declares_kernel_types = std::is_class_v<K> && !std::is_final_v<K> && requires {
	typename K::value_type;
	typename K::step_type;
	typename K::Settings;
	typename K::Coefficients;
	typename K::State;
};

/// @brief K steps one sample (or frame) at a time: its step type is its value type.
template <typename K>
concept scalar_step_kernel = std::same_as<typename K::step_type, typename K::value_type>;

/// @brief K steps a vector of consecutive q31 samples.
template <typename K>
concept vector_step_kernel =
    std::same_as<typename K::value_type, q31> && std::same_as<typename K::step_type, Argon<q31>>;

/// @brief K steps a vector of consecutive stereo frames, de-interleaved one vector per channel.
template <typename K>
concept stereo_vector_step_kernel =
    std::same_as<typename K::value_type, StereoSample> && std::same_as<typename K::step_type, StereoVector>;

/// @brief Whether a wrapper has a loop for K's step type over its value type: the step type is the value type,
/// `Argon<q31>` over q31, or StereoVector over StereoSample.
template <typename K>
concept supported_step_pair = scalar_step_kernel<K> || vector_step_kernel<K> || stereo_vector_step_kernel<K>;

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

/// @brief Whether K's `reset`, if it declares one, is exactly `static void reset(State&)`.
template <typename K>
concept reset_well_formed = !declares_reset<K> || resets_exactly<K>;

} // namespace detail

/// @brief K::Block if K declares one, else EmptyBlock.
template <typename K>
using kernel_block_t = typename detail::kernel_block<K>::type;

/// @brief Whether K's partial last vector advances its state by the active lane count (true) or a whole vector.
/// @details False unless K declares `static constexpr bool kPartialStepAdvancesByCount`. A vector kernel whose state
/// persists across blocks declares it true and provides the counted `step` overload taking a trailing ActiveLanes.
/// The generator and processor concepts require that overload when this is true, and reject the declaration on a
/// scalar step; the mixer and staged concepts reject it, having no partial step.
template <typename K>
inline constexpr bool partial_step_advances_by_count = false;

/// @brief Whether K's partial last vector advances its state by the active lane count (true) or a whole vector.
template <typename K>
requires std::same_as<std::remove_cv_t<decltype(K::kPartialStepAdvancesByCount)>, bool>
inline constexpr bool partial_step_advances_by_count<K> = K::kPartialStepAdvancesByCount;

/// @brief The shape every kernel has: value/step types, Settings → Coefficients (pure), State, optional prepare.
/// @details Requires:
/// - a non-final class declaring `value_type`, `step_type` and `Settings`, with a step type a wrapper has a loop
///   for: the value type itself, `Argon<q31>` over q31 blocks, or StereoVector over StereoSample blocks;
/// - `Coefficients`, `State` and kernel_block_t<K> as plain value structs (default-constructible, trivially
///   copyable);
/// - `static Coefficients coefficients(const Settings&)`;
/// - when K declares any member named `prepare`, exactly `static kernel_block_t<K> prepare(const Coefficients&,
///   State&, std::size_t n)`, called once per block of @c n samples. @c n may be 0: an empty block still prepares.
///   A non-static or mismatched `prepare`, or a template whose parameters do not deduce to that signature (one on a
///   mode), fails here rather than being skipped;
/// - when K declares a `Block`, a `prepare` to make it;
/// - when K declares any member named `reset`, exactly `static void reset(State&)`, the reset quirk the wrappers
///   call instead of assigning `State{}`. A non-static or otherwise mis-shaped `reset` fails here rather than being
///   skipped;
/// - when K has modes, a default `Coefficients{}` that is a constant expression naming a listed mode, since a
///   default-constructed wrapper dispatches on it (an enumeration member defaults to its zero enumerator, so a kernel
///   whose zero enumerator is not listed gives `mode` a default member initialiser).
template <typename K>
concept kernel_base =
    detail::declares_kernel_types<K> && detail::supported_step_pair<K> && detail::plain_value<typename K::Coefficients>
    && detail::plain_value<typename K::State> && detail::plain_value<kernel_block_t<K>>
    && detail::computes_coefficients<K> && detail::prepare_well_formed<K> && detail::reset_well_formed<K>
    && (!detail::has_modes<K> || detail::default_mode_listed<K>);

/// @brief A kernel generating its output: `step(c, b, s) -> step_type`, and no other form. When K declares
/// kPartialStepAdvancesByCount, which only a vector step may (a scalar loop has no partial step), also the counted
/// `step(c, b, s, ActiveLanes) -> step_type`.
/// @details Checked at K's first mode, or at the non-template step when K has no modes.
template <typename K>
concept generator_kernel =
    kernel_base<K> && detail::kernel_form_count<K> == 1 && detail::steps_as_generator<K>
    && (!partial_step_advances_by_count<K> || (!detail::scalar_step_kernel<K> && detail::steps_with<K, ActiveLanes>));

/// @brief A kernel transforming its input: `step(c, b, s, x) -> step_type`, and no other form. When K declares
/// kPartialStepAdvancesByCount, which only a vector step may (a scalar loop has no partial step), also the counted
/// `step(c, b, s, x, ActiveLanes) -> step_type`.
/// @details Checked at K's first mode, or at the non-template step when K has no modes.
template <typename K>
concept processor_kernel =
    kernel_base<K> && detail::kernel_form_count<K> == 1 && detail::steps_as_processor<K>
    && (!partial_step_advances_by_count<K>
        || (!detail::scalar_step_kernel<K> && detail::steps_with<K, typename K::step_type, ActiveLanes>));

/// @brief A kernel combining two inputs: `step(c, b, s, a, b2) -> step_type`, and no other form.
/// @details Checked at K's first mode, or at the non-template step when K has no modes. A mixer has no counted
/// partial step, so it must not declare kPartialStepAdvancesByCount.
template <typename K>
concept mixer_kernel = kernel_base<K> && detail::kernel_form_count<K> == 1
                       && detail::steps_as_mixer<K> && !partial_step_advances_by_count<K>;

/// @brief A block-staged kernel: process(c, s, Signal<value_type>, Buffer<value_type>) instead of step.
/// @details For arithmetic that reads a whole block, shapes it, then writes the whole block (the delay's tape I/O);
/// every other kernel is written as a `step`. It provides no `step` at any arity and has no partial step, so it must
/// not declare kPartialStepAdvancesByCount. `process` takes no Block, so a staged kernel declares no `prepare`: its
/// per-block work is `process` itself. A moded staged kernel's `process` is a template on the mode, checked at its
/// first mode.
///
/// **In place.** `process` must give the same result when `in` and `out` are the same buffer, as every wrapper path
/// does: the app renders in place. A `process` that reads a sample after writing one it has not yet read (a
/// reversal written front to back, a look-ahead) reads its own output; it stages what it still needs before
/// overwriting it.
template <typename K>
concept staged_kernel = kernel_base<K> && detail::kernel_form_count<K> == 1 && detail::processes_blocks<K>
                        && !partial_step_advances_by_count<K> && !detail::declares_prepare<K>;

/// @brief A block-rate kernel: `advance(c, s, n) -> value_type` moves a control signal on by a whole block of @c n
/// samples, and no other form.
/// @details For a block-rate signal whose arithmetic is per block, not per sample: an envelope or a duck that the
/// owner reads once per block and applies as one value. `Control` calls `advance` once per block; @c n is the block's
/// length in samples and may be 0, so an empty block still makes one call. The signature is exact, as `prepare`'s and
/// `reset`'s are: `static value_type advance(const Coefficients&, State&, std::size_t n)`, or, when K has modes,
/// `template <Mode M> static value_type advance(...)` with that signature at every listed mode. It is checked by
/// converting `advance`'s address to a pointer to that function type, so a function template whose parameters deduce
/// to exactly that type is accepted too. An `advance` taking its count as any other type (an `int`, or a `uint32_t`
/// where that is not `std::size_t`) is rejected, not converted.
///
/// The block is the step, so a control kernel:
/// - steps a scalar (`step_type` is `value_type`);
/// - declares no `prepare` and so no `Block` (its per-block work is `advance` itself);
/// - declares no kPartialStepAdvancesByCount, having no vector to end partially.
template <typename K>
concept control_kernel =
    kernel_base<K> && detail::kernel_form_count<K> == 1 && detail::advances_blocks<K> && detail::scalar_step_kernel<K>
    && !detail::declares_prepare<K> && !partial_step_advances_by_count<K>;

/// @brief A kernel of any form; a class satisfies at most one of the five form concepts.
template <typename K>
concept kernel = generator_kernel<K> || processor_kernel<K> || mixer_kernel<K> || staged_kernel<K> || control_kernel<K>;

/// @brief Whether K has modes (K::Modes is a ModeList and Coefficients has a `mode` member).
/// @details The `mode` member has the list's enumeration type; the wrapper dispatches on it once per block with
/// with_mode. All of a kernel's modes share one State, and a moded kernel's `step`, `process` or `advance` is a
/// template on the mode. Its default `Coefficients{}` names a listed mode (kernel_base requires it), so a
/// default-constructed wrapper's first block dispatches on a mode the kernel has.
template <typename K>
concept moded_kernel = kernel<K> && detail::has_modes<K>;

namespace detail {

/// @brief The lane pack the opt-in is detected at: two q31 lanes.
using ProbePack = LanePack<q31, 2>;

/// @brief Whether K declares `static constexpr bool kLanePackable = true`.
template <typename K>
concept declares_lane_packable = requires {
	requires std::same_as<std::remove_cv_t<decltype(K::kLanePackable)>, bool>;
	requires K::kLanePackable;
};

/// @brief Whether K's state is written over its value type: `StateT<q31>` is K's State, and `StateT<P>` holds every
/// member as a P, so a pack of N lanes is N States side by side, member by member.
/// @details Checked by size: a pack's state is the probe width's times a State's, and both copy as plain bytes. The
/// size shows every member is a P only while the packed state is aligned as a q31 is, so that no padding can make up
/// a member of another type; an over-aligned packed state is rejected.
template <typename K, typename P>
concept state_written_over_value =
    requires { typename K::template StateT<P>; } && std::same_as<typename K::template StateT<q31>, typename K::State>
    && plain_value<typename K::template StateT<P>> && std::is_trivially_copyable_v<typename K::State>
    && sizeof(typename K::State) % sizeof(q31) == 0 && alignof(typename K::template StateT<P>) == alignof(q31)
    && sizeof(typename K::template StateT<P>) == 2 * sizeof(typename K::State);

/// @brief Whether K's `step` written over a value type is callable at pack P, with the form's step values as P's, and
/// returns a P: `step<first mode, P>` for a moded kernel, `step<P>` for one without modes.
template <typename K, typename P, typename... X>
concept steps_packed_with =
    requires(const typename K::Coefficients& c, const EmptyBlock& b, typename K::template StateT<P>& s, X... x) {
	    requires((has_modes<K> && requires {
		             { K::template step<first_mode_v<typename K::Modes>, P>(c, b, s, x...) } -> std::same_as<P>;
	             }) || (!has_modes<K> && requires {
		             { K::template step<P>(c, b, s, x...) } -> std::same_as<P>;
	             }));
    };

/// @brief Whether K's step at pack P takes the step values K's form takes: none, one or two.
template <typename K, typename P>
concept steps_packed =
    (generator_kernel<K> && steps_packed_with<K, P>) || (processor_kernel<K> && steps_packed_with<K, P, P>)
    || (mixer_kernel<K> && steps_packed_with<K, P, P, P>);

} // namespace detail

/// @brief Whether K opts in to the op-major step: written once over its value type, it steps every lane of a frame
/// as one @ref LanePack.
/// @details K declares:
/// - `static constexpr bool kLanePackable = true`;
/// - `template <class V> struct StateT`, every member a V, with `using State = StateT<q31>`;
/// - `template <Mode M, class V> static V step(const Coefficients&, const EmptyBlock&, StateT<V>&, …)` (without the
///   mode when K has none), its arithmetic written so it resolves for q31 and for a LanePack: `wrapping_add`,
///   `wrapping_sub` and the fixed-point multiplies called unqualified.
///
/// The coefficients stay scalar: every lane shares them, and a pack's operations broadcast a scalar operand. K is a
/// generator, processor or mixer kernel stepping q31. The op-major step is not offered to a kernel that declares a
/// `prepare`, whose Block may depend on each lane's State, nor to one with a `reset` quirk, which acts on one State:
/// `Lanes` steps those lane by lane.
template <typename K>
concept lane_packable =
    (generator_kernel<K> || processor_kernel<K> || mixer_kernel<K>)
    && std::same_as<typename K::value_type, q31> && std::same_as<typename K::step_type, q31>
    && detail::declares_lane_packable<K> && !detail::declares_prepare<K> && !detail::declares_reset<K>
    && detail::state_written_over_value<K, detail::ProbePack> && detail::steps_packed<K, detail::ProbePack>;

namespace detail {

/// @brief Whether K's op-major opt-in, if it declares one, is met: K is @ref lane_packable, or it declares a `prepare`
/// or a `reset` quirk, which keep it lane by lane by design.
/// @details False exactly for a kernel that declares `kLanePackable = true` but whose StateT or step is not written
/// over its value type. `Lanes` rejects such a kernel rather than step it lane by lane unannounced.
template <typename K>
concept lane_pack_opt_in_met =
    !declares_lane_packable<K> || declares_prepare<K> || declares_reset<K> || lane_packable<K>;

} // namespace detail

/// @brief The calls a wrapper makes on a kernel, public so that a composed kernel makes the same calls on its parts.
/// @details `Mono`, `Stereo` and `Control` call a kernel through these alone: @ref prepare_block, @ref dispatch_mode,
/// then @ref step_at, @ref process_at or @ref advance_at for the kernel's form. A kernel built from smaller kernels
/// prepares each part with @ref prepare_block and steps each part with @ref step_at, so it never has to know whether a
/// part declares `prepare` or has modes. Everything here runs per block or per step and is `[[gnu::always_inline]]`.
namespace kernel_ops {

/// @brief The mode a kernel without modes is stepped at: a placeholder @ref step_at, @ref process_at and @ref
/// advance_at are templated on and ignore.
enum class NoMode : uint8_t { NONE };

/// @brief K's per-block values for a block of @p n samples: its `prepare` when it declares one, else an
/// EmptyBlock.
/// @param c K's coefficients.
/// @param s K's state, which `prepare` may update.
/// @param n The block's length in samples (frames for a stereo kernel); may be 0.
/// @return The block's values, which every step of the block receives.
template <kernel K>
[[gnu::always_inline]] inline kernel_block_t<K> prepare_block([[maybe_unused]] const typename K::Coefficients& c,
                                                              [[maybe_unused]] typename K::State& s,
                                                              [[maybe_unused]] std::size_t n) {
	if constexpr (detail::declares_prepare<K>) {
		return K::prepare(c, s, n);
	}
	else {
		return kernel_block_t<K>{};
	}
}

/// @brief Call @p body.template operator()<M>() once: at the mode of @p c for a moded kernel, through with_mode,
/// else at NoMode::NONE.
/// @tparam K The kernel whose mode is dispatched on.
/// @tparam Body The callable run at the mode.
/// @param c K's coefficients, whose `mode` a moded kernel dispatches on.
/// @param body A callable with a templated call operator taking the mode as its sole non-type argument.
template <kernel K, typename Body>
[[gnu::always_inline]] inline void dispatch_mode([[maybe_unused]] const typename K::Coefficients& c, Body&& body) {
	if constexpr (moded_kernel<K>) {
		with_mode<typename K::Modes>(c.mode, body);
	}
	else {
		body.template operator()<NoMode::NONE>();
	}
}

/// @brief One `step` of K at mode @p M: `K::step<M>` for a moded kernel; for a kernel without modes, its
/// non-template `step`, with @p M ignored (pass NoMode::NONE).
/// @param c K's coefficients.
/// @param block K's per-block values, from @ref prepare_block.
/// @param s K's state.
/// @param x The step's inputs: none, one or two step values, then an ActiveLanes for the counted partial step.
/// @return The step's output: one `K::step_type`.
template <kernel K, auto M, typename... X>
[[gnu::always_inline]] inline typename K::step_type
step_at(const typename K::Coefficients& c, const kernel_block_t<K>& block, typename K::State& s, X... x) {
	if constexpr (moded_kernel<K>) {
		return K::template step<M>(c, block, s, x...);
	}
	else {
		return K::step(c, block, s, x...);
	}
}

/// @brief One op-major `step` of K at mode @p M over pack P: every lane of a frame at once, with the coefficients
/// shared. `K::step<M, P>` for a moded kernel; `K::step<P>` for one without modes, with @p M ignored.
/// @param c K's coefficients, shared by every lane.
/// @param s The packed state: lane i of each member is lane i's State.
/// @param x The step's inputs: none, one or two packs.
/// @return The frame's outputs, one per lane.
template <lane_packable K, auto M, typename P, typename... X>
[[nodiscard]] [[gnu::always_inline]] inline P step_packed_at(const typename K::Coefficients& c,
                                                             typename K::template StateT<P>& s, X... x) {
	if constexpr (moded_kernel<K>) {
		return K::template step<M, P>(c, EmptyBlock{}, s, x...);
	}
	else {
		return K::template step<P>(c, EmptyBlock{}, s, x...);
	}
}

/// @brief One `advance` of control kernel K at mode @p M: `K::advance<M>` for a moded kernel; for a kernel without
/// modes, its non-template `advance`, with @p M ignored (pass NoMode::NONE).
/// @details The coefficients are passed as a const reference, so the call resolves to the `advance` the control form
/// names even when K overloads it on non-const coefficients.
/// @param c K's coefficients.
/// @param s K's state.
/// @param n The block's length in samples; may be 0.
/// @return The signal's value after the block.
template <control_kernel K, auto M>
[[gnu::always_inline]] inline typename K::value_type advance_at(const typename K::Coefficients& c, typename K::State& s,
                                                                std::size_t n) {
	if constexpr (moded_kernel<K>) {
		return K::template advance<M>(c, s, n);
	}
	else {
		return K::advance(c, s, n);
	}
}

/// @brief One `process` of staged kernel K at mode @p M: `K::process<M>` for a moded kernel; for a kernel without
/// modes, its non-template `process`, with @p M ignored (pass NoMode::NONE).
/// @details The coefficients are passed as a const reference, so the call resolves to the `process` the staged form
/// names even when K overloads it on non-const coefficients.
/// @param c K's coefficients.
/// @param s K's state.
/// @param in At least `out.size()` samples; may be the same buffer as @p out.
/// @param out The block to fill.
template <staged_kernel K, auto M>
[[gnu::always_inline]] inline void process_at(const typename K::Coefficients& c, typename K::State& s,
                                              Signal<typename K::value_type> in, Buffer<typename K::value_type> out) {
	if constexpr (moded_kernel<K>) {
		K::template process<M>(c, s, in, out);
	}
	else {
		K::process(c, s, in, out);
	}
}

} // namespace kernel_ops

} // namespace deluge::dsp
