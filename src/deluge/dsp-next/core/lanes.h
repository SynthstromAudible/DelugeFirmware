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

#include "dsp-next/core/frame.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/lane_pack.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"

#include <concepts>
#include <cstddef>
#include <utility>

/// @file
/// @brief Lanes<K, N>: one kernel run on N independent streams, interleaved as frames.
namespace deluge::dsp {

namespace detail {

/// @brief How a block's frame type @p F holds its lanes: the lane type, the lane count, and the conversions to and
/// from a @ref Frame that the loop steps.
template <class F>
struct frame_lanes;

/// @brief A @ref Frame is its own lanes.
template <class T, std::size_t N>
struct frame_lanes<Frame<T, N>> {
	using lane_type = T;                    ///< One lane's sample.
	static constexpr std::size_t lanes = N; ///< The lane count.

	/// @brief @p f's lanes.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Frame<T, N> to_lanes(Frame<T, N> f) { return f; }
	/// @brief The frame holding @p f's lanes.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Frame<T, N> from_lanes(Frame<T, N> f) { return f; }
};

/// @brief A @ref StereoSample is two q31 lanes, left in lane 0, converted by bit cast (@ref to_frame).
template <>
struct frame_lanes<StereoSample> {
	using lane_type = q31;                  ///< One channel's sample, Q31.
	static constexpr std::size_t lanes = 2; ///< Left and right.

	/// @brief @p s as two lanes: left in lane 0, right in lane 1.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Frame<q31, 2> to_lanes(StereoSample s) { return to_frame(s); }
	/// @brief Two lanes as a stereo frame: lane 0 to left, lane 1 to right.
	[[nodiscard]] [[gnu::always_inline]] static constexpr StereoSample from_lanes(Frame<q31, 2> f) {
		return to_stereo(f);
	}
};

/// @brief The state an op-major `Lanes<K, N>` holds: K's state over a pack of N lanes. `void` for a kernel stepped
/// lane by lane, which holds one State per lane instead.
template <typename K, std::size_t N>
struct packed_state {
	using type = void; ///< No packed state.
};

/// @brief K's state over `LanePack<q31, N>`: lane i of each member is lane i's State.
template <lane_packable K, std::size_t N>
struct packed_state<K, N> {
	using type = typename K::template StateT<LanePack<q31, N>>; ///< Every lane's State, member by member.
	static_assert(sizeof(type) == N * sizeof(typename K::State) && std::is_trivially_copyable_v<type>,
	              "a packed state is N States, member by member, each member N lanes of q31");
};

} // namespace detail

/// @brief A scalar kernel run on N independent streams: one Coefficients shared by every lane, one State per lane,
/// over blocks of interleaved frames, lane 0 stepped first in every frame.
/// @tparam K The kernel: a generator, processor or mixer kernel whose step is one sample (`step_type` is
/// `value_type`). A staged kernel has no step to run per lane and a control kernel no per-sample loop, so both are
/// rejected at the class.
/// @tparam N The number of lanes. At least 1.
/// @tparam F The frame type: N samples of K's `value_type`, lane 0 first. `Frame<value_type, N>` by default; a
/// @ref StereoSample holds two q31 lanes, left in lane 0. @ref detail::frame_lanes maps it to the lanes the loop
/// steps.
/// @details
/// **Shared coefficients.** `configure` calls `K::coefficients` once; every lane steps with the result.
///
/// **Per block.** `renderBlock` calls K's `prepare(coefficients, state, n)` once per lane, when K declares one, since a
/// Block may depend on the State: lane 0's first, then lane 1's, and on, so a `prepare` drawing from a stream every
/// lane shares (noise) draws in lane order on every block. Each lane steps with its own Block. Then, for a moded
/// kernel, it dispatches once on `coefficients().mode` through with_mode, and inside that one dispatch a single loop
/// runs every frame in the block's mode: a mode change set by `configure` takes effect at the next block, and every
/// State carries across it untouched.
///
/// **Order.** Each frame steps lane 0, then 1, … N − 1, into a local frame, then stores it. A kernel drawing from a
/// stream every lane shares (noise) therefore draws in lane order on every frame; at N = 2 that is left before right.
///
/// **The loop.** One input pointer per input block and one output pointer step over whole frames. Each frame is
/// loaded whole, the lanes are stepped, and then the frame is stored. Every input frame is read whole before any lane
/// of its output is written, so an input block may be the same buffer as the output. Where the pointer steps fall
/// against the store is clang's choice, not the source's. Rendered in place (one span passed as input and output, as
/// `DelayProcessor` does), the two pointers merge into one, stepped after the frame's store, so the next frame's load
/// address is computed after a pending store; check the in-place disassembly, and bench in place what ships in place.
/// The remedy class, once a bench shows that stall matters, is a one-frame software pipeline: load frame i + 1 before
/// storing frame i.
///
/// **Single samples.** `render(…)` is a one-frame block: it builds one-frame spans and runs a whole block's `prepare`,
/// dispatch and loop, which under the shipping `-fno-inline-functions` costs several out-of-line `std::span` calls per
/// call. It is a convenience for tests and for control-rate values, not for per-frame use in shipping code.
///
/// **Op-major.** When K is @ref lane_packable, `Lanes` holds K's state over a `LanePack<q31, N>` and steps each frame
/// with one `step` over the pack: every operation runs across the lanes before the next, the independent work the
/// Cortex-A9 overlaps. Each pack operation is its scalar operation lane by lane, so the result is bit for bit the
/// lane-by-lane one. Such a K has no `prepare` and no Block, and its step draws no noise, so the order rules above hold
/// trivially.
///
/// **No noise op-major.** A step written over V must not draw from a stream the lanes share (noise): op-major steps
/// every lane in one call, so one draw would reach every lane, where lane by lane draws once per lane in lane order. A
/// kernel that needs noise does not opt in, and steps lane by lane. Nothing detects a draw at compile time, since it is
/// a call inside the step, so each opted-in kernel's spec checks its op-major form against itself stepped lane by lane.
///
/// **State.** `state(lane)` is a snapshot, by value in both forms: a copy of the lane's State when it is called, which
/// later blocks do not update.
///
/// **The loop state.** `prepare` runs on each lane's member State, and `on`, `reset` and `state(lane)` see the whole
/// State. The loop steps each lane's loop state only: a copy of it, or, when K declares `kStateInPlace`, the member
/// itself. A kernel declaring either trait is stepped lane by lane, never op-major.
///
/// **Inlining.** As in @ref Mono: the app builds with `-fno-inline-functions`, so `render`, `renderBlock`, the loop and
/// the per-frame lane step are `[[gnu::always_inline]]`. The loop runs on local copies of the coefficients and every
/// lane's loop state (@ref loop_state_t: the whole State unless K declares `kLoopState`), taken after `prepare` and
/// written back after the block, so that they stay in registers even though the output may alias the component. The
/// States and Blocks are built-in arrays in a struct, whose copy constructs nothing and whose indexing is no call.
template <kernel K, std::size_t N, class F = Frame<typename K::value_type, N>>
requires(N >= 1 && !staged_kernel<K> && !control_kernel<K>)
class Lanes {
	using Coefficients = typename K::Coefficients; ///< K's coefficients, computed on configure, shared by every lane.
	using State = typename K::State;               ///< K's state, one per lane, carried from block to block.
	using LoopState = loop_state_t<K>;             ///< The part of K's State the loop steps, one per lane.
	static constexpr bool kInPlace = detail::state_in_place<K>;         ///< Whether the loop steps the members.
	static constexpr bool kSplitsLoop = detail::declares_loop_state<K>; ///< Whether the loop state is a member.
	using Block = kernel_block_t<K>;                        ///< K's per-block values, one per lane, or EmptyBlock.
	using Context = typename detail::context_of<K>::type;   ///< K's per-block context, shared by the lanes.
	using Access = detail::frame_lanes<F>;                  ///< How F holds its lanes.
	using LaneFrame = Frame<typename Access::lane_type, N>; ///< The lanes the loop steps.
	static constexpr bool kOpMajor = lane_packable<K>;      ///< Whether each frame is one step over a LanePack.
	using Pack = LanePack<q31, N>;                          ///< A frame's lanes as one value, for the op-major step.
	using PackedState = typename detail::packed_state<K, N>::type; ///< Every lane's State in one, when op-major.

	/// @brief One State per lane, lane 0 first.
	/// @details A struct around a built-in array: a copy is one assignment that constructs nothing, so the loop's
	/// local copy never passes its address to an out-of-line constructor and stays in registers; and indexing is no
	/// call, as std::array's is under -fno-inline-functions.
	struct States {
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
		State lane[N]; ///< The States, lane 0 first.
	};

	/// @brief One Block per lane, lane 0 first, held as @ref States is.
	struct Blocks {
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
		Block lane[N]; ///< The Blocks, lane 0 first.
	};

	/// @brief One loop state per lane, lane 0 first, held as @ref States is: the local copy the loop steps when K
	/// declares `kLoopState` and not `kStateInPlace`.
	struct LoopStates {
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
		LoopState lane[N]; ///< The loop states, lane 0 first.
	};

	static_assert(std::same_as<typename K::step_type, typename K::value_type>,
	              "Lanes steps one sample per lane: K's step type is its value type");
	static_assert(std::same_as<typename Access::lane_type, typename K::value_type> && Access::lanes == N,
	              "the frame type holds N samples of K's value type");
	static_assert(detail::lane_pack_opt_in_met<K>,
	              "K declares kLanePackable = true, but its StateT or step is not written over its value type");

public:
	using value_type = F; ///< A block's frame: one sample per lane.

	/// @brief Value-initialised Coefficients and State{} in every lane.
	[[gnu::always_inline]] Lanes() = default;

	/// @brief Configured from @p settings, with State{} in every lane.
	/// @param settings The kernel's settings, turned into Coefficients once.
	[[gnu::always_inline]] explicit Lanes(const typename K::Settings& settings)
	    : coefficients_{K::coefficients(settings)} {}

	/// @brief Store the coefficients for @p settings, computed once for every lane; they apply from the next block,
	/// every State untouched.
	/// @param settings The kernel's settings.
	[[gnu::always_inline]] void configure(const typename K::Settings& settings) {
		coefficients_ = K::coefficients(settings);
	}

	/// @brief The coefficients from the last configure, including any owner-facing values the kernel exposes.
	/// @return The current Coefficients, shared by every lane.
	[[nodiscard]] [[gnu::always_inline]] const Coefficients& coefficients() const { return coefficients_; }

	/// @brief Return every lane to the reset State: State{}, or whatever K's `static void reset(State&)` quirk sets
	/// when K declares one.
	[[gnu::always_inline]] void reset() {
		if constexpr (kOpMajor) {
			// StateT's initialisers are written over V, so a value-initialised pack is State{} in every lane.
			packed_ = PackedState{};
		}
		else {
			for (State& s : states_.lane) {
				detail::reset_state<K>(s);
			}
		}
	}

	/// @brief A snapshot of lane @p lane's state between blocks. @p lane is less than N.
	/// @details By value in both forms, so a caller never holds a view into state the next block rewrites. Op-major,
	/// it is read out of the packed state: member m is N q31 lanes, so lane @p lane's word m sits at m × N + @p lane.
	/// @param lane The lane, less than N.
	/// @return A copy of the lane's State.
	[[nodiscard]] [[gnu::always_inline]] State state(std::size_t lane) const {
		if constexpr (kOpMajor) {
			return unpackLane(lane);
		}
		else {
			return states_.lane[lane];
		}
	}

	/// @brief Run the event @p Event on each lane's State, lane 0 first, then 1, … N − 1: with the coefficients when
	/// it takes them, else without.
	/// @tparam Event A function taking K's State by non-const reference, and optionally its Coefficients first, as
	/// @ref Control::on accepts.
	/// @param args The event's own arguments, after the coefficients and state; every lane is given the same
	/// lvalues, so an event takes them by value or reference, never by rvalue reference.
	/// @details Returns nothing, since there are N results. Op-major, each lane's State is unpacked, given to the
	/// event and packed back before the next lane's, so an event touches exactly one lane at a time and the other
	/// lanes' words are written back unchanged.
	template <auto Event, typename... A>
	[[gnu::always_inline]] void on(A&&... args) {
		if constexpr (kOpMajor) {
			for (std::size_t lane = 0; lane < N; ++lane) {
				State s = unpackLane(lane);
				detail::run_event<K, Event>(coefficients_, s, args...);
				packLane(lane, s);
			}
		}
		else {
			for (State& s : states_.lane) {
				detail::run_event<K, Event>(coefficients_, s, args...);
			}
		}
	}

	/// @brief Generate one frame: a one-frame block, so one `prepare(…, 1)` per lane and one mode dispatch per call.
	/// @details A convenience for tests and control rate, not for per-frame use in shipping code (see **Single
	/// samples** in the class documentation).
	/// @return The next frame.
	[[nodiscard]] [[gnu::always_inline]] value_type render()
	requires generator_kernel<K> && (!has_context<K>)
	{
		value_type out{};
		renderBlock(Buffer<value_type>{&out, 1});
		return out;
	}

	/// @brief Fill @p out with the next `out.size()` frames, lane 0 first in every frame.
	/// @param out The block to fill, every frame of it.
	[[gnu::always_inline]] void renderBlock(Buffer<value_type> out)
	requires generator_kernel<K> && (!has_context<K>)
	{
		detail::NoContext none;
		runBlock(none, out);
	}

	/// @brief Fill @p out from a generator kernel with a Context, every lane drawing on @p context in lane order
	/// through a block-local copy.
	/// @param out The block of frames to fill.
	/// @param context The owner's context: copied in before the block and written back once after it.
	[[gnu::always_inline]] void renderBlock(Buffer<value_type> out, Context& context)
	requires generator_kernel<K> && has_context<K>
	{
		Context local = context;
		runBlock(local, out);
		context = local;
	}

	/// @brief Process one frame: a one-frame block, so one `prepare(…, 1)` per lane and one mode dispatch per call.
	/// @details A convenience for tests and control rate, not for per-frame use in shipping code (see **Single
	/// samples** in the class documentation).
	/// @param x The input frame.
	/// @return The output frame.
	[[nodiscard]] [[gnu::always_inline]] value_type render(value_type x)
	requires processor_kernel<K> && (!has_context<K>)
	{
		value_type out{};
		renderBlock(Signal<value_type>{&x, 1}, Buffer<value_type>{&out, 1});
		return out;
	}

	/// @brief Process `out.size()` frames of @p in into @p out, lane 0 first in every frame.
	/// @param in At least `out.size()` frames; may be the same buffer as @p out.
	/// @param out The block to fill, every frame of it.
	[[gnu::always_inline]] void renderBlock(Signal<value_type> in, Buffer<value_type> out)
	requires processor_kernel<K> && (!has_context<K>)
	{
		detail::NoContext none;
		runBlock(none, out, in);
	}

	/// @brief Process one frame through a processor kernel with a Context, lane 0 drawing first.
	/// @param x The input frame.
	/// @param context The owner's context, drawn on and written back.
	/// @return The output frame.
	[[nodiscard]] [[gnu::always_inline]] value_type render(value_type x, Context& context)
	requires processor_kernel<K> && has_context<K>
	{
		value_type out{};
		renderBlock(Signal<value_type>{&x, 1}, Buffer<value_type>{&out, 1}, context);
		return out;
	}

	/// @brief Process @p in into @p out through a processor kernel with a Context, every lane drawing on @p context in
	/// lane order (lane 0 then lane 1 within each frame) through a block-local copy.
	/// @param in At least `out.size()` frames; may be the same buffer as @p out.
	/// @param out The block of frames to fill.
	/// @param context The owner's context: copied in before the block and written back once after it.
	[[gnu::always_inline]] void renderBlock(Signal<value_type> in, Buffer<value_type> out, Context& context)
	requires processor_kernel<K> && has_context<K>
	{
		Context local = context;
		runBlock(local, out, in);
		context = local;
	}

	/// @brief Process `out.size()` samples of lane @p Lane alone, @p in into @p out: that lane's State steps exactly
	/// as a `Mono<K>` holding it would, and every other lane's State is untouched.
	/// @details For an owner whose stream is sometimes one lane and sometimes all of them on the same State, such as a
	/// voice's filter rendering mono blocks through the left channel and stereo blocks through both. The lane's
	/// `prepare` runs once (on the member State, lane-by-lane), then one mode dispatch and one loop over the samples
	/// on the lane's loop state, as @ref renderBlock steps it: a local copy written back after the block, or the
	/// member itself when K declares `kStateInPlace`. A kernel drawing noise draws once per sample, for this lane
	/// only. Op-major, the lane's State is unpacked for the block and packed back after it.
	/// @tparam Lane The lane, less than N: a compile-time index, since the State written back is indexed by it.
	/// @param in At least `out.size()` samples; may be the same buffer as @p out.
	/// @param out The block to fill, every sample of it.
	template <std::size_t Lane>
	[[gnu::always_inline]] void renderLane(Signal<typename K::value_type> in, Buffer<typename K::value_type> out)
	requires processor_kernel<K> && (!has_context<K>)
	{
		detail::NoContext none;
		runLane<Lane>(none, in, out);
	}

	/// @brief @ref renderLane for a kernel with a Context: lane @p Lane alone draws on @p context, once per sample,
	/// through a block-local copy written back after the block.
	/// @tparam Lane The lane, less than N.
	/// @param in At least `out.size()` samples; may be the same buffer as @p out.
	/// @param out The block to fill, every sample of it.
	/// @param context The owner's context: copied in before the block and written back once after it.
	template <std::size_t Lane>
	[[gnu::always_inline]] void renderLane(Signal<typename K::value_type> in, Buffer<typename K::value_type> out,
	                                       Context& context)
	requires processor_kernel<K> && has_context<K>
	{
		Context local = context;
		runLane<Lane>(local, in, out);
		context = local;
	}

private:
	/// @brief The lane loop @ref renderLane runs: prepare, one mode dispatch, one loop over the lane's samples.
	template <std::size_t Lane, typename Ctx>
	[[gnu::always_inline]] void runLane(Ctx& context, Signal<typename K::value_type> in,
	                                    Buffer<typename K::value_type> out) {
		static_assert(Lane < N, "renderLane: no such lane");
		const auto n = static_cast<std::ptrdiff_t>(out.size());
		if constexpr (kOpMajor) {
			State s = unpackLane(Lane);
			const Coefficients c = coefficients_;
			laneLoop(c, kernel_ops::prepare_block<K>(c, s, out.size()), s, context, in.data(), out.data(), n);
			packLane(Lane, s);
		}
		else {
			const Block block = kernel_ops::prepare_block<K>(coefficients_, states_.lane[Lane], out.size());
			const Coefficients c = coefficients_;
			if constexpr (kInPlace) {
				laneLoop(c, block, kernel_ops::loop_state<K>(states_.lane[Lane]), context, in.data(), out.data(), n);
			}
			else {
				LoopState s = kernel_ops::loop_state<K>(states_.lane[Lane]);
				laneLoop(c, block, s, context, in.data(), out.data(), n);
				kernel_ops::loop_state<K>(states_.lane[Lane]) = s;
			}
		}
	}

public:
	/// @brief Mix one pair of frames: a one-frame block, so one `prepare(…, 1)` per lane and one mode dispatch per
	/// call.
	/// @details A convenience for tests and control rate, not for per-frame use in shipping code (see **Single
	/// samples** in the class documentation).
	/// @param a,b The pair of input frames.
	/// @return The mixed frame.
	[[nodiscard]] [[gnu::always_inline]] value_type render(value_type a, value_type b)
	requires mixer_kernel<K> && (!has_context<K>)
	{
		value_type out{};
		renderBlock(Signal<value_type>{&a, 1}, Signal<value_type>{&b, 1}, Buffer<value_type>{&out, 1});
		return out;
	}

	/// @brief Mix `out.size()` frames of @p a and @p b into @p out, lane 0 first in every frame.
	/// @param a,b At least `out.size()` frames each; either may be the same buffer as @p out.
	/// @param out The block to fill, every frame of it.
	[[gnu::always_inline]] void renderBlock(Signal<value_type> a, Signal<value_type> b, Buffer<value_type> out)
	requires mixer_kernel<K> && (!has_context<K>)
	{
		detail::NoContext none;
		runBlock(none, out, a, b);
	}

	/// @brief Mix @p a and @p b into @p out through a mixer kernel with a Context, every lane drawing on @p context in
	/// lane order through a block-local copy.
	/// @param a At least `out.size()` frames.
	/// @param b At least `out.size()` frames.
	/// @param out The block of frames to fill.
	/// @param context The owner's context: copied in before the block and written back once after it.
	[[gnu::always_inline]] void renderBlock(Signal<value_type> a, Signal<value_type> b, Buffer<value_type> out,
	                                        Context& context)
	requires mixer_kernel<K> && has_context<K>
	{
		Context local = context;
		runBlock(local, out, a, b);
		context = local;
	}

private:
	/// @brief The q31 words in one State. Member m of a packed state is N q31 lanes, so lane i's word m sits at
	/// m × N + i.
	static constexpr std::size_t kWords = sizeof(State) / sizeof(q31);
	static_assert(!kOpMajor || kWords > 0,
	              "an op-major State is whole q31 words, at least one, so a packed lane never forms Words<0>");

	/// @brief @p W q31 words, for reading a trivially copyable value as words. Named only on the op-major paths, whose
	/// State is at least one word: @ref lane_packable requires a State made of whole q31 words, and an empty struct
	/// is one byte.
	template <std::size_t W>
	struct Words {
		// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
		q31 word[W]; ///< The words.
	};

	/// @brief Lane @p lane's State, read out of the packed state. Op-major only.
	[[nodiscard]] [[gnu::always_inline]] State unpackLane(std::size_t lane) const {
		const auto packed = __builtin_bit_cast(Words<kWords * N>, packed_);
		Words<kWords> one{};
		for (std::size_t m = 0; m < kWords; ++m) {
			one.word[m] = packed.word[(m * N) + lane];
		}
		return __builtin_bit_cast(State, one);
	}

	/// @brief Write @p s into lane @p lane of the packed state, every other lane's words untouched. Op-major only.
	[[gnu::always_inline]] void packLane(std::size_t lane, const State& s) {
		auto packed = __builtin_bit_cast(Words<kWords * N>, packed_);
		const auto one = __builtin_bit_cast(Words<kWords>, s);
		for (std::size_t m = 0; m < kWords; ++m) {
			packed.word[(m * N) + lane] = one.word[m];
		}
		packed_ = __builtin_bit_cast(PackedState, packed);
	}

	/// @brief Render one block: `prepare` once per lane in lane order, one mode dispatch, one loop.
	/// @param out The block to fill.
	/// @param in The input blocks: none, one or two.
	/// @details `prepare` runs on the member States, before any copy is taken, so its writes are in place before the
	/// loop starts and a local's address never escapes to a `prepare` that is not inlined. The loop then steps every
	/// lane's loop state: a local copy written back after the block, or, when K declares `kStateInPlace`, the members
	/// themselves.
	template <typename Ctx, typename... In>
	[[gnu::always_inline]] void runBlock([[maybe_unused]] Ctx& context, Buffer<value_type> out, In... in) {
		if constexpr (kOpMajor) {
			runPackedBlock(out, in...);
		}
		else {
			const std::size_t n = out.size();
			const Blocks blocks = prepareLanes(n, std::make_index_sequence<N>{});
			const Coefficients c = coefficients_;
			value_type* const dst = out.data();
			if constexpr (kInPlace) {
				kernel_ops::dispatch_mode<K>(c, [&]<auto M> [[gnu::always_inline]] () {
					loop<M>(c, blocks, states_, context, dst, static_cast<std::ptrdiff_t>(n), in.data()...);
				});
			}
			else if constexpr (kSplitsLoop) {
				LoopStates s = loopStates(std::make_index_sequence<N>{});
				kernel_ops::dispatch_mode<K>(c, [&]<auto M> [[gnu::always_inline]] () {
					loop<M>(c, blocks, s, context, dst, static_cast<std::ptrdiff_t>(n), in.data()...);
				});
				storeLoopStates(s, std::make_index_sequence<N>{});
			}
			else {
				States s = states_;
				kernel_ops::dispatch_mode<K>(c, [&]<auto M> [[gnu::always_inline]] () {
					loop<M>(c, blocks, s, context, dst, static_cast<std::ptrdiff_t>(n), in.data()...);
				});
				states_ = s;
			}
		}
	}

	/// @brief Every lane's Block for a block of @p n frames: K's `prepare` on each member State, lane 0 first.
	template <std::size_t... Lane>
	[[nodiscard]] [[gnu::always_inline]] Blocks prepareLanes(std::size_t n, std::index_sequence<Lane...> /*lanes*/) {
		// A braced list evaluates its elements left to right: lane 0 prepares first.
		return Blocks{{kernel_ops::prepare_block<K>(coefficients_, states_.lane[Lane], n)...}};
	}

	/// @brief Every lane's loop state, copied out of its member State.
	template <std::size_t... Lane>
	[[nodiscard]] [[gnu::always_inline]] LoopStates loopStates(std::index_sequence<Lane...> /*lanes*/) {
		return LoopStates{{kernel_ops::loop_state<K>(states_.lane[Lane])...}};
	}

	/// @brief Write every lane's loop state @p s back into its member State.
	template <std::size_t... Lane>
	[[gnu::always_inline]] void storeLoopStates(const LoopStates& s, std::index_sequence<Lane...> /*lanes*/) {
		((kernel_ops::loop_state<K>(states_.lane[Lane]) = s.lane[Lane]), ...);
	}

	/// @brief The loop state @p s holds: @p s itself when it is one, else the part of a whole State K's step takes.
	template <typename S>
	[[nodiscard]] [[gnu::always_inline]] static LoopState& loopStateOf(S& s) {
		if constexpr (std::same_as<S, LoopState>) {
			return s;
		}
		else {
			return kernel_ops::loop_state<K>(s);
		}
	}

	/// @brief One frame at mode @p M: lane 0's `step`, then lane 1's, … N − 1's, each with its own Block and State.
	/// @param x The frame's inputs, already loaded whole: none, one or two lane frames.
	/// @return The frame's output lanes.
	/// @tparam S The lanes' States (@ref States, the members or a copy) or their loop states (@ref LoopStates).
	template <auto M, typename S, typename Ctx, typename... X>
	[[nodiscard]] [[gnu::always_inline]] static LaneFrame stepFrame(const Coefficients& c, const Blocks& blocks, S& s,
	                                                                Ctx& context, const X&... x) {
		const auto step_lane = [&]<std::size_t Lane> [[gnu::always_inline]] () {
			return kernel_ops::step_with<K, M>(c, blocks.lane[Lane], loopStateOf(s.lane[Lane]), context,
			                                   x.lane[Lane]...);
		};
		LaneFrame y;
		// A comma fold runs its operands left to right: lane 0 first.
		[&]<std::size_t... Lane> [[gnu::always_inline]] (std::index_sequence<Lane...>) {
			((y.lane[Lane] = step_lane.template operator()<Lane>()), ...);
		}(std::make_index_sequence<N>{});
		return y;
	}

	/// @brief @p n frames of the input blocks @p src (none, one or two) into @p dst, one @ref stepFrame per frame.
	/// @details Each frame's inputs are loaded whole before the frame is stepped, and the frame is stored after, so an
	/// input may be the output. In place the pointers merge and step after the store (the class's **The loop**).
	/// @tparam S The lanes' States (@ref States, the members or a copy) or their loop states (@ref LoopStates).
	template <auto M, typename S, typename Ctx, typename... Src>
	[[gnu::always_inline]] static void loop(const Coefficients& c, const Blocks& blocks, S& s, Ctx& context,
	                                        value_type* dst, std::ptrdiff_t n, const Src*... src) {
		value_type* const end = dst + n;
		while (dst != end) {
			const LaneFrame y = stepFrame<M>(c, blocks, s, context, Access::to_lanes(*src++)...);
			*dst++ = Access::from_lanes(y);
		}
	}

	/// @brief @p n samples of one lane, @p src into @p dst, one `step` per sample at the block's mode, on that lane's
	/// State @p s and Block @p block.
	template <typename Ctx>
	[[gnu::always_inline]] static void laneLoop(const Coefficients& c, const Block& block, LoopState& s, Ctx& context,
	                                            const typename K::value_type* src, typename K::value_type* dst,
	                                            std::ptrdiff_t n) {
		kernel_ops::dispatch_mode<K>(c, [&]<auto M> [[gnu::always_inline]] () {
			for (std::ptrdiff_t i = 0; i < n; ++i) {
				dst[i] = kernel_ops::step_with<K, M>(c, block, s, context, src[i]);
			}
		});
	}

	/// @brief Render one block op-major: one mode dispatch, one loop stepping each frame as one pack.
	/// @details K has no `prepare`, so no Block: each step gets an EmptyBlock. The loop runs on a local copy of the
	/// packed state, written back after the block, as the lane-by-lane form does.
	template <typename... In>
	[[gnu::always_inline]] void runPackedBlock(Buffer<value_type> out, In... in) {
		const std::size_t n = out.size();
		const Coefficients c = coefficients_;
		PackedState s = packed_;
		value_type* const dst = out.data();
		kernel_ops::dispatch_mode<K>(c, [&]<auto M> [[gnu::always_inline]] () {
			packedLoop<M>(c, s, dst, static_cast<std::ptrdiff_t>(n), in.data()...);
		});
		packed_ = s;
	}

	/// @brief @p n frames of the input blocks @p src (none, one or two) into @p dst, one op-major step per frame.
	/// @details As @ref loop: each frame's inputs are loaded whole before the step, and the frame is stored after it.
	/// @tparam S The packed state: PackedState, a template parameter so that a lane-by-lane Lanes never forms it.
	template <auto M, typename S, typename... Src>
	[[gnu::always_inline]] static void packedLoop(const Coefficients& c, S& s, value_type* dst, std::ptrdiff_t n,
	                                              const Src*... src) {
		value_type* const end = dst + n;
		while (dst != end) {
			const Pack y = kernel_ops::step_packed_at<K, M, Pack>(c, s, Pack::load(Access::to_lanes(*src++))...);
			LaneFrame frame;
			y.store(frame);
			*dst++ = Access::from_lanes(frame);
		}
	}

	/// @brief Each lane's State as a built-in array, or nothing when the State is packed.
	struct NoStates {};
	/// @brief The packed state, or nothing when each lane holds its own State.
	struct NoPackedState {};

	Coefficients coefficients_{}; ///< K's coefficients for the configured settings, shared by every lane.
	/// @brief Each lane's state, lane 0 first, carried from block to block, when stepped lane by lane.
	[[no_unique_address]] std::conditional_t<kOpMajor, NoStates, States> states_{};
	/// @brief Every lane's state as one pack, carried from block to block, when op-major.
	[[no_unique_address]] std::conditional_t<kOpMajor, PackedState, NoPackedState> packed_{};
};

} // namespace deluge::dsp
