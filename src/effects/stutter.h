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

#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/delay/delay_tape.h"
#include "dsp-next/fixed/stutter/stutter_kernel.h"
#include "effects/delay.h"  // TapeShape
#include "effects/mod_fx.h" // RingAllocator
#include "effects/param_fragment.h"
#include "effects/state.h"

#include <cstdint>
#include <span>

namespace deluge::effects {

/// @brief A stutter's tape handed over: the one allocated before the owner's section, and the one taken back inside
/// it.
struct StutterChange {
	dsp::StereoSample* acquired = nullptr; ///< Storage prepare allocated, not zeroed; null when allocation failed.
	TapeShape shape{};                     ///< Its tape's shape.
	uint32_t rate = 0;                     ///< The rate it was shaped for, 8.24 on the delay's rate scale.
	dsp::StereoSample* released = nullptr; ///< Storage commit or commit_end took back; freed by finish.
};

/// @brief The stutter: the dsp-next Stutter kernel over a tape this component owns, recorded when a stutter begins and
/// played back until it ends.
///
/// The tape comes from the allocator. Its storage is not zeroed: the kernel's read head clears each sample before the
/// write behind it reaches it, so nothing unwritten is ever played. A stutter begins and ends in three steps around
/// the owner's audio-excluded section: @ref prepare allocates the tape for the rate, @ref commit (or @ref commit_end)
/// swaps it inside the section, and @ref finish frees what was taken back. The render never allocates or frees.
class Stutter {
public:
	/// @brief The rate param, on the delay's rate scale (neutral 2^24). A knob change never writes automation.
	static constexpr EffectParam kRate{.name = "stutterRate",
	                                   .curve = dsp::fixed::ParamCurve::EXPONENTIAL,
	                                   .neutral = 16777216,
	                                   .flags = plugin::ParamFlag::NOT_AUTOMATABLE};

	using Kernel = dsp::fixed::Stutter;

	/// @brief Takes the allocator the tapes come from and go back to.
	/// @param allocator The allocator for the tapes' storage.
	explicit Stutter(RingAllocator allocator);
	/// @brief Frees the tape held; the owner's teardown runs with the render excluded.
	~Stutter();
	Stutter(const Stutter&) = delete;
	Stutter& operator=(const Stutter&) = delete;

	/// @brief Interaction tier, before the section: allocates storage for the tape a stutter at @p rate (8.24) records,
	/// its size plus the write lag.
	/// @param rate The stutter's rate, 8.24 on the delay's rate scale.
	/// @return The allocated storage and its shape; `acquired` is null when allocation failed.
	[[nodiscard]] StutterChange prepare(uint32_t rate);
	/// @brief Inside the owner's AudioExcludedSection: starts recording over @p change's tape with @p settings'
	/// direction and ping-pong. A tape already held moves to @p change's released.
	/// @param change   The handover from prepare.
	/// @param settings The stutter's direction and ping-pong.
	/// @return False, and nothing changed, when @p change holds no storage.
	[[nodiscard]] bool commit(StutterChange& change, const StutterState& settings);
	/// @brief Inside the owner's AudioExcludedSection: stops the stutter and takes its tape back into @p change.
	/// @param change The handover receiving the tape.
	/// @pre @p change holds nothing released.
	void commit_end(StutterChange& change);
	/// @brief After the section: frees what commit or commit_end took back, and storage prepare allocated that commit
	/// did not use.
	/// @param change The handover to free; its pointers are null afterwards.
	void finish(StutterChange& change);

	/// @brief Render: records into or plays back the tape over @p block in place at @p rate (8.24), turning to play
	/// after the block in which the recording ran out. Passes through while inactive.
	/// @param block The frames to process, in place.
	/// @param rate  The stutter's rate, 8.24 on the delay's rate scale.
	void render(std::span<dsp::StereoSample> block, uint32_t rate);

	/// @return Whether a stutter is recording or playing.
	[[nodiscard]] bool active() const { return storage_ != nullptr; }
	/// @return The kernel's state; for specs.
	[[nodiscard]] const Kernel::State& state() const { return mono_.state(); }
	/// @return The tape; inactive while no stutter plays. For specs.
	[[nodiscard]] const dsp::fixed::DelayTape& tape() const { return tape_; }

private:
	RingAllocator allocator_;
	dsp::Mono<Kernel> mono_;
	dsp::fixed::DelayTape tape_;
	dsp::StereoSample* storage_ = nullptr; ///< The tape's storage, while a stutter plays.
};

} // namespace deluge::effects
