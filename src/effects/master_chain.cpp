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

#include "effects/master_chain.h"

#include "dsp-next/core/fixed_point.h"

#include <array>

namespace deluge::effects {

namespace {
constexpr std::array<std::string_view, 7> kOrder{"mod_fx",   "eq",      "delay",     "filter",
                                                 "bitcrush", "stutter", "compressor"};
} // namespace

MasterChain::MasterChain(RingAllocator mod_fx_allocator, RingAllocator granular_allocator, DelayArena& arena)
    : core_{mod_fx_allocator, granular_allocator, EnterGrain::KEEP, arena, 0} {
}

bool MasterChain::pre_reverb(std::span<dsp::StereoSample> block, const MasterChainInputs& in, ReverbSend send) {
	// The mod FX and grain scale the volume in place
	dsp::Q<27> post_fx_volume = in.volume_post_fx;
	core_.soft_off_grain(in.fx);
	core_.render_fx(block, in.fx, post_fx_volume, true, send); // the master always has sound coming in
	return core_.send_and_volume().process(block, send.first(block.size()),
	                                       {.post_fx_volume = post_fx_volume,
	                                        .post_reverb_volume = in.post_reverb_volume,
	                                        .reverb_send = in.reverb_send,
	                                        .pan = dsp::Q<31>{},
	                                        .ramp = false,
	                                        .stereo = in.fx.stereo});
}

void MasterChain::post_reverb(std::span<dsp::StereoSample> block, const EffectStates& settings,
                              const MasterPostInputs& post) {
	using dsp::Q;
	// The master's volume is Q27, 2^27 unity; a bitcrush lowers it by a run-time shift.
	Q<27> volume = filter_.configure(settings.filter, post.filter, post.master_volume);
	filter_.process(block, post.filter_scratch, *post.noise);
	volume = scale_shr_floor(volume, core_.bitcrush().process(block, post.bitcrush).volume_shift);
	Q<27> volume_l = volume;
	Q<27> volume_r = volume_l;

	if (post.stutter != nullptr) {
		post.stutter->render(block, post.stutter_rate);
	}
	if (post.do_pan) {
		// The Q27 volume times a Q30 pan amplitude is Q25, moved back to Q27.
		volume_l = mul_as_wrap<Q<27>>(volume_l, post.pan_l);
		volume_r = mul_as_wrap<Q<27>>(volume_r, post.pan_r);
	}

	Compressor& compressor = core_.compressor();
	compressor.configure(settings.compressor); // the master's threshold is its knob
	// The compressor's channel gains are 4.27 and its final volume 3.29: the master halves its Q27 volume into the
	// gains, and reads an eighth of the song's Q27 volume as the final volume, as it is tuned.
	compressor.dsp().render(block, scale_shr_floor<1>(volume_l), scale_shr_floor<1>(volume_r),
	                        reinterpret_bits<Q<29>>(scale_shr_floor<3>(post.song_volume)));
}

std::span<const std::string_view> MasterChain::order() {
	return kOrder;
}

} // namespace deluge::effects
