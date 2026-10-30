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

#include "engines/effects/clip_chain.h"

#include <array>

namespace deluge::effects {

namespace {
constexpr std::array<std::string_view, 8> kOrder{"saturation", "filter", "bitcrush", "mod_fx",
                                                 "eq",         "delay",  "stutter",  "compressor"};
} // namespace

ClipChain::ClipChain(RingAllocator mod_fx_allocator, RingAllocator granular_allocator, DelayArena& arena,
                     int32_t post_reverb_volume_neutral)
    : core_{mod_fx_allocator, granular_allocator, EnterGrain::KEEP, arena, post_reverb_volume_neutral} {
}

ClipBlock ClipChain::begin_block(const ClipChainInputs& in, std::size_t frames) {
	const EffectStates& settings = *in.fx.settings;
	ClipBlock levels{
	    .volume_post_fx = filter_.configure(settings.filter, in.filter, in.volume_post_fx),
	    .post_reverb_volume = in.post_reverb_volume_neutral,
	};

	if (in.sidechain_on) {
		levels.post_reverb_volume = core_.sidechain().level<dsp::fixed::DuckScale::CLIP>({
		    .frames = static_cast<uint32_t>(frames),
		    .hit = in.sidechain_hit,
		    .shape = in.sidechain_shape,
		    .depth = in.sidechain_depth,
		    .state = settings.sidechain,
		    .tick_inverse = in.tick_inverse,
		});
	}

	core_.compressor().configure(settings.compressor);
	core_.compressor().dsp().setThreshold(in.compressor_threshold); // a clip chain's threshold is its param
	return levels;
}

void ClipChain::pre_tap(std::span<dsp::StereoSample> block, const ClipChainInputs& in, ClipBlock& levels, bool rendered,
                        ReverbSend send) {
	const EffectStates& settings = *in.fx.settings;
	saturation_.process(block,
	                    {.clipping_amount = settings.saturation.clipping_amount, .variant = SaturationVariant::CHAIN});
	if (rendered) {
		filter_.process(block, in.filter_scratch);
		core_.bitcrush().process(block, in.bitcrush, levels.volume_post_fx);
	}
	core_.soft_off_grain(in.fx);
	core_.render_fx(block, in.fx, levels.volume_post_fx, rendered, send);
	if (in.stutter != nullptr) {
		in.stutter->render(block, in.stutter_rate);
	}
}

bool ClipChain::post_tap(std::span<dsp::StereoSample> block, const ClipChainInputs& in, const ClipBlock& levels,
                         ReverbSend send) {
	const bool sent = core_.send_and_volume().process(block, send.first(block.size()),
	                                                  {.post_fx_volume = levels.volume_post_fx,
	                                                   .post_reverb_volume = levels.post_reverb_volume,
	                                                   .reverb_send = in.reverb_send,
	                                                   .pan = in.pan,
	                                                   .ramp = true,
	                                                   .stereo = in.fx.stereo});
	if (in.compressor_threshold > 0) {
		core_.compressor().dsp().renderVolumeNeutral(block, levels.volume_post_fx);
	}
	else {
		core_.compressor().dsp().reset();
	}
	return sent;
}

std::span<const std::string_view> ClipChain::order() {
	return kOrder;
}

} // namespace deluge::effects
