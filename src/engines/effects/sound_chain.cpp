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

#include "engines/effects/sound_chain.h"

#include <array>

namespace deluge::effects {

namespace {
constexpr std::array<std::string_view, 6> kOrder{"bitcrush", "mod_fx", "eq", "delay", "stutter", "compressor"};
} // namespace

SoundChain::SoundChain(RingAllocator mod_fx_allocator, RingAllocator granular_allocator, DelayArena& arena)
    : core_{mod_fx_allocator, granular_allocator, EnterGrain::CLEAR, arena, -1} {
}

bool SoundChain::process(std::span<dsp::StereoSample> block, const SoundChainInputs& in, ReverbSend send) {
	const EffectStates& settings = *in.fx.settings;
	int32_t post_fx_volume = in.post_fx_volume;
	core_.send_and_volume().prime(in.post_reverb_volume);

	core_.bitcrush().process(block, in.bitcrush, post_fx_volume);
	// A sound's type in effect is its setting: it has no soft off.
	FxInputs fx = in.fx;
	fx.mod_fx_type = settings.mod_fx.type;
	core_.render_fx(block, fx, post_fx_volume, in.sound, send);
	if (in.stutter != nullptr) {
		in.stutter->render(block, in.stutter_rate);
	}

	const bool sent = core_.send_and_volume().process(block, send.first(block.size()),
	                                                  {.post_fx_volume = post_fx_volume,
	                                                   .post_reverb_volume = in.post_reverb_volume,
	                                                   .reverb_send = in.reverb_send,
	                                                   .pan = 0,
	                                                   .ramp = true,
	                                                   .stereo = in.fx.stereo});

	Compressor& compressor = core_.compressor();
	compressor.configure(settings.compressor);
	compressor.dsp().setThreshold(in.compressor_threshold); // a sound's threshold is its param
	if (in.compressor_threshold > 0) {
		compressor.dsp().renderVolumeNeutral(block, post_fx_volume);
	}
	else {
		compressor.dsp().reset();
	}
	return sent;
}

std::span<const std::string_view> SoundChain::order() {
	return kOrder;
}

} // namespace deluge::effects
