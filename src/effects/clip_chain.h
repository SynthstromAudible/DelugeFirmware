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
#include "effects/chain_core.h"
#include "effects/filter.h"
#include "effects/saturation.h"
#include "effects/stutter.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::effects {

/// @brief What a kit's or an audio track's chain reads for one block, as the owner resolved it from its unpatched
/// params.
struct ClipChainInputs {
	FxInputs fx;
	BitcrushParams bitcrush;
	FilterParams filter;
	dsp::fixed::FilterScratch filter_scratch; ///< The audio tier's filter scratch.
	int32_t volume_post_fx;                   ///< The post-FX volume before the filter's compensation, Q31.
	int32_t reverb_send;                      ///< The reverb send amount, Q31.
	int32_t pan;                              ///< The pan param halved, Q31 (0 the centre).
	bool sidechain_on;                        ///< Whether the sidechain volume param is above its minimum.
	int32_t sidechain_hit;                    ///< The hit pending for the sidechain this block, Q31; 0 for none.
	int32_t sidechain_shape;                  ///< The sidechain shape param, Q31.
	int32_t sidechain_depth;                  ///< The sidechain volume as a patch cable's depth, Q29.
	uint32_t tick_inverse;                    ///< The playback handler's getTimePerInternalTickInverse().
	int32_t post_reverb_volume_neutral;       ///< The post-reverb volume with no duck, Q31.
	int32_t compressor_threshold; ///< The threshold param, Q31; at or below 0 the compressor resets instead.
	Stutter* stutter;             ///< The song's stutter while this owner is the one stuttering, else null.
	uint32_t stutter_rate;        ///< The stutter's rate on the delay's rate scale (16777216 neutral).
};

/// @brief The volumes begin_block works out before the content renders, which process applies.
struct ClipBlock {
	int32_t volume_post_fx;     ///< After the filter's compensation, Q31; the bitcrush, mod FX and grain scale it.
	int32_t post_reverb_volume; ///< Ducked by the sidechain while it is on, Q31.
};

/// @brief A kit's or an audio track's effects: the sidechain's duck, then (after the owner renders its content into the
/// block) saturation, filter, bitcrush, mod FX or grain, EQ, delay, stutter, the recorder's tap, reverb send, volume
/// and pan, then the compressor.
class ClipChain {
public:
	/// @brief Build a chain whose mod FX and granular buffers come from the given allocators.
	/// @param mod_fx_allocator The ring allocator for the mod FX's buffers.
	/// @param granular_allocator The ring allocator for the granular effect's buffers.
	/// @param arena Where the delay's tapes come from. A host chain keeps its grains on entering GRAIN.
	/// @param post_reverb_volume_neutral The post-reverb volume the first block ramps from, Q31.
	ClipChain(RingAllocator mod_fx_allocator, RingAllocator granular_allocator, DelayArena& arena,
	          int32_t post_reverb_volume_neutral);

	[[nodiscard]] ChainCore& core() { return core_; }
	[[nodiscard]] const ChainCore& core() const { return core_; }
	[[nodiscard]] Filter& filter() { return filter_; }
	[[nodiscard]] const Filter& filter() const { return filter_; }
	[[nodiscard]] Saturation& saturation() { return saturation_; }
	[[nodiscard]] const Saturation& saturation() const { return saturation_; }

	/// @brief Before the content renders: the filter's settings (which compensate the post-FX volume), the sidechain's
	/// duck of the post-reverb volume, and the compressor's settings.
	/// @param in What the chain reads for this block.
	/// @param frames The block's length.
	/// @return The volumes process applies.
	[[nodiscard]] ClipBlock begin_block(const ClipChainInputs& in, std::size_t frames);

	// TODO(c++26): take std::function_ref<void(std::span<const dsp::StereoSample>)> once the device's libstdc++
	// provides it (arm-none-eabi GCC 16; 15.2.1 does not).
	/// @brief After the content rendered into @p block: the chain over it in place.
	/// @param block The block the content rendered into; processed in place.
	/// @param in What the chain reads for this block.
	/// @param levels What begin_block returned; the bitcrush, mod FX and grain scale its post-FX volume.
	/// @param rendered Whether the content rendered this block; when not, the filter and bitcrush are skipped and the
	/// mod FX, grain and delay see no sound coming in.
	/// @param send The reverb send, one value per frame.
	/// @param tap Called with the block after the stutter, before the send and volume: the owner's recorder.
	/// @return Whether the block fed the reverb.
	template <typename Tap>
	bool process(std::span<dsp::StereoSample> block, const ClipChainInputs& in, ClipBlock& levels, bool rendered,
	             ReverbSend send, Tap&& tap) {
		pre_tap(block, in, levels, rendered, send);
		tap(std::span<const dsp::StereoSample>{block});
		return post_tap(block, in, levels, send);
	}

	/// @brief The effects process renders, in order (chain.h's names); the sidechain's duck comes before them, in
	/// begin_block.
	/// @return The effects' names, in render order.
	[[nodiscard]] static std::span<const std::string_view> order();

private:
	/// @brief Saturation to the stutter.
	void pre_tap(std::span<dsp::StereoSample> block, const ClipChainInputs& in, ClipBlock& levels, bool rendered,
	             ReverbSend send);
	/// @brief Send, volume and pan, then the compressor.
	/// @return Whether the block fed the reverb.
	bool post_tap(std::span<dsp::StereoSample> block, const ClipChainInputs& in, const ClipBlock& levels,
	              ReverbSend send);

	ChainCore core_;
	Filter filter_;
	Saturation saturation_;
};

} // namespace deluge::effects
