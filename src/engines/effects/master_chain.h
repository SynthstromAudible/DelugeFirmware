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

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/filter/filter_set.h"
#include "engines/effects/chain_core.h"
#include "engines/effects/filter.h"
#include "engines/effects/stutter.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace deluge::effects {

/// @brief What the master's half before the reverb reads, as the song resolved it from its params when its outputs
/// had rendered.
struct MasterChainInputs {
	FxInputs fx;
	int32_t volume_post_fx;     ///< The song's post-FX volume, Q31.
	int32_t post_reverb_volume; ///< Q31.
	int32_t reverb_send;        ///< The reverb send amount the send and volume adds, Q31.
};

/// @brief What the master's half after the reverb reads, resolved once the reverb has rendered.
struct MasterPostInputs {
	FilterParams filter;
	dsp::fixed::FilterScratch filter_scratch; ///< The audio tier's filter scratch.
	BitcrushParams bitcrush;
	int32_t master_volume; ///< The volume before the filter's compensation, Q31.
	bool do_pan;           ///< Whether the pan applies: off the centre, in stereo, and not a no-op.
	int32_t pan_l;         ///< The left gain when it does, Q29.
	int32_t pan_r;         ///< The right gain when it does, Q29.
	int32_t song_volume;   ///< The song volume's final, Q31; the compressor's makeup takes it >> 3.
	Stutter* stutter;      ///< The song's stutter while the master is the one stuttering, else null.
	uint32_t stutter_rate; ///< The stutter's rate on the delay's rate scale (16777216 neutral).
};

/// @brief The song's master: mod FX or grain, EQ, delay, reverb send and volume; then whatever the engine mixes in
/// between (the reverb's return and the sample preview); then filter, bitcrush, stutter, pan and the compressor with
/// the song volume. Its saturation and sidechain are saved with the song and not rendered.
class MasterChain {
public:
	/// @brief Build a master chain whose mod FX and granular buffers come from the given allocators.
	/// @param mod_fx_allocator The ring allocator for the mod FX's buffers.
	/// @param granular_allocator The ring allocator for the granular effect's buffers.
	/// @param arena Where the delay's tapes come from. A host chain keeps its grains on entering GRAIN.
	MasterChain(RingAllocator mod_fx_allocator, RingAllocator granular_allocator, DelayArena& arena);

	[[nodiscard]] ChainCore& core() { return core_; }
	[[nodiscard]] const ChainCore& core() const { return core_; }
	[[nodiscard]] Filter& filter() { return filter_; }
	[[nodiscard]] const Filter& filter() const { return filter_; }

	// TODO(c++26): take std::function_ref<std::optional<MasterPostInputs>(bool)> once the device's libstdc++ provides
	// it (arm-none-eabi GCC 16; 15.2.1 does not).
	/// @brief The half before the reverb over @p block in place, then @p between, then the half after it.
	/// @param block The stereo block, processed in place.
	/// @param in What the half before the reverb reads.
	/// @param send The reverb send, one value per frame.
	/// @param between Called with whether the half before fed the reverb (the engine notes it before the reverb
	/// renders); renders what the engine mixes in between the halves, then returns the half after's inputs, or
	/// nothing to skip that half (a stem export without the song's effects).
	/// @return Whether the block fed the reverb.
	template <typename Between>
	bool render(std::span<dsp::StereoSample> block, const MasterChainInputs& in, ReverbSend send, Between&& between) {
		const bool sent = pre_reverb(block, in, send);
		const std::optional<MasterPostInputs> post = between(sent);
		if (post.has_value()) {
			post_reverb(block, *in.fx.settings, *post);
		}
		return sent;
	}

	/// @brief The effects render renders, in order (chain.h's names): the reverb returns before the reverb_return'th.
	/// @return The effects' names, in render order.
	[[nodiscard]] static std::span<const std::string_view> order();

private:
	/// @brief The half before the reverb.
	/// @param block The stereo block, processed in place.
	/// @param in What the half before the reverb reads.
	/// @param send The reverb send, one value per frame.
	/// @return Whether the block fed the reverb.
	bool pre_reverb(std::span<dsp::StereoSample> block, const MasterChainInputs& in, ReverbSend send);
	/// @brief The half after the reverb: filter, bitcrush, stutter, pan and the compressor.
	/// @param block The stereo block, processed in place.
	/// @param settings The song's effect states.
	/// @param post What the half after the reverb reads.
	void post_reverb(std::span<dsp::StereoSample> block, const EffectStates& settings, const MasterPostInputs& post);

	ChainCore core_;
	Filter filter_;
};

} // namespace deluge::effects
