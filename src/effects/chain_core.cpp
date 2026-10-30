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

#include "effects/chain_core.h"

#include "util/fixedpoint.h"

namespace deluge::effects {

ChainCore::ChainCore(RingAllocator mod_fx_allocator, RingAllocator granular_allocator, EnterGrain enter,
                     DelayArena& arena, int32_t last_post_reverb_volume)
    : delay_{arena}, mod_fx_{mod_fx_allocator}, granular_{granular_allocator, enter},
      send_and_volume_{last_post_reverb_volume} {
}

void ChainCore::render_fx(std::span<dsp::StereoSample> block, const FxInputs& in, int32_t& post_fx_volume, bool sound,
                          ReverbSend send) {
	if (in.mod_fx_type == ModFXType::GRAIN) {
		granular_.render(block,
		                 {
		                     .rate = in.mod_fx_rate,
		                     .mix = in.mod_fx_depth,
		                     .density = in.mod_fx_offset,
		                     .randomness = in.mod_fx_feedback,
		                     .send_amount = multiply_32x32_rshift32(post_fx_volume, in.grain_send) << 5,
		                     .sound = sound,
		                 },
		                 post_fx_volume, send.first(block.size()), *in.noise);
	}
	else {
		mod_fx_.render(block,
		               {
		                   .active = in.mod_fx_type,
		                   .rate = in.mod_fx_rate,
		                   .depth = in.mod_fx_depth,
		                   .offset = in.mod_fx_offset,
		                   .feedback = in.mod_fx_feedback,
		                   .stereo = in.stereo,
		                   .sound = sound,
		               },
		               post_fx_volume, *in.noise);
	}
	equalizer_.process(block, in.eq);
	delay_.process(block, in.delay, in.stereo);
}

void ChainCore::soft_off_grain(const FxInputs& in) {
	if (in.mod_fx_type != ModFXType::GRAIN && in.settings->mod_fx.type == ModFXType::GRAIN) {
		granular_.startSkippingRendering();
	}
}

} // namespace deluge::effects
