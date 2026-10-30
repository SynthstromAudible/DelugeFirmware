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

// Each home's effect sequence over the chain components, written out step by step as the owners render it: a sound's
// from the send and volume's prime to the compressor; a kit's or an audio track's from the filter's settings and the
// sidechain's duck to the compressor, its content render left out (the block arrives rendered); the song master's half
// before the reverb, the caller's between, then its half after. Owner reads are the inputs fields that carry them. The
// runtimes are checked against this, so it is written independently of them and changes only with the sound itself.

#pragma once

#include "engines/effects/chain_core.h"
#include "engines/effects/clip_chain.h"
#include "engines/effects/filter.h"
#include "engines/effects/master_chain.h"
#include "engines/effects/saturation.h"
#include "engines/effects/sound_chain.h"
#include "util/fixedpoint.h"

#include <optional>
#include <span>

namespace chain_reference {

using deluge::dsp::StereoSample;
namespace fx = deluge::effects;

/// @brief The mod FX (or the grain), the EQ and the delay.
///
/// A sound passes its mod FX type, a host chain the type in effect.
/// @param c                The chain components.
/// @param buffer           The block, processed in place.
/// @param modFXType        The mod FX type to run.
/// @param in               The effect inputs.
/// @param postFXVolume     The post-FX volume, which the mod FX may adjust.
/// @param anySoundComingIn Whether the block carries sound, for the mod FX.
/// @param verbAmount       The grain's reverb feed amount.
/// @param reverbBuffer     The reverb send the grain feeds.
inline void process_fx(fx::ChainCore& c, std::span<StereoSample> buffer, fx::ModFXType modFXType,
                       const fx::FxInputs& in, int32_t* postFXVolume, bool anySoundComingIn, q31_t verbAmount,
                       fx::ReverbSend reverbBuffer) {
	if (modFXType == fx::ModFXType::GRAIN) {
		const fx::GranularInputs grain{
		    .rate = in.mod_fx_rate,
		    .mix = in.mod_fx_depth,
		    .density = in.mod_fx_offset,
		    .randomness = in.mod_fx_feedback,
		    .send_amount = multiply_32x32_rshift32(*postFXVolume, verbAmount) << 5,
		    .sound = anySoundComingIn,
		};
		c.granular().render(buffer, grain, *postFXVolume, {reverbBuffer.data(), buffer.size()});
	}
	else {
		const fx::ModFxInputs mod{
		    .active = modFXType,
		    .rate = in.mod_fx_rate,
		    .depth = in.mod_fx_depth,
		    .offset = in.mod_fx_offset,
		    .feedback = in.mod_fx_feedback,
		    .stereo = in.stereo,
		    .sound = anySoundComingIn,
		};
		c.mod_fx().render(buffer, mod, *postFXVolume);
	}
	c.equalizer().process(buffer, in.eq);
	c.delay().process(buffer, in.delay, in.stereo);
}

/// @brief The reverb send, then the volumes and pan.
/// @param c                    The chain components.
/// @param buffer               The block, processed in place.
/// @param reverbBuffer         The reverb send.
/// @param postFXVolume         The volume before the reverb.
/// @param postReverbVolume     The volume after the reverb.
/// @param reverbSendAmount     The reverb send amount.
/// @param pan                  The pan.
/// @param doAmplitudeIncrement Whether the volume ramps across the block.
/// @param stereo               Whether the block is stereo.
/// @return Whether anything was sent (the engine notes the reverb's time).
inline bool process_reverb_send_and_volume(fx::ChainCore& c, std::span<StereoSample> buffer,
                                           fx::ReverbSend reverbBuffer, int32_t postFXVolume, int32_t postReverbVolume,
                                           int32_t reverbSendAmount, int32_t pan, bool doAmplitudeIncrement,
                                           bool stereo) {
	return c.send_and_volume().process(buffer, {reverbBuffer.data(), buffer.size()},
	                                   {.post_fx_volume = postFXVolume,
	                                    .post_reverb_volume = postReverbVolume,
	                                    .reverb_send = reverbSendAmount,
	                                    .pan = pan,
	                                    .ramp = doAmplitudeIncrement,
	                                    .stereo = stereo});
}

/// @brief A sound's chain, from the prime to the compressor.
/// @param c            The chain components.
/// @param in           The sound chain inputs.
/// @param sound_stereo The sound's block, processed in place.
/// @param reverbBuffer The reverb send.
/// @return Whether anything was sent to the reverb.
inline bool sound(fx::ChainCore& c, const fx::SoundChainInputs& in, std::span<StereoSample> sound_stereo,
                  fx::ReverbSend reverbBuffer) {
	const fx::EffectStates& settings = *in.fx.settings;
	int32_t postFXVolume = in.post_fx_volume;
	int32_t postReverbVolume = in.post_reverb_volume;

	c.send_and_volume().prime(postReverbVolume);

	postFXVolume >>= c.bitcrush().process(sound_stereo, in.bitcrush).volume_shift;
	process_fx(c, sound_stereo, settings.mod_fx.type, in.fx, &postFXVolume, in.sound, in.fx.grain_send, reverbBuffer);
	if (in.stutter != nullptr) {
		in.stutter->render(sound_stereo, in.stutter_rate);
	}

	const bool sent = process_reverb_send_and_volume(c, sound_stereo, reverbBuffer, postFXVolume, postReverbVolume,
	                                                 in.reverb_send, 0, true, in.fx.stereo);

	q31_t compThreshold = in.compressor_threshold;
	c.compressor().configure(settings.compressor);
	c.compressor().dsp().setThreshold(compThreshold);
	if (compThreshold > 0) {
		c.compressor().dsp().renderVolumeNeutral(sound_stereo, postFXVolume);
	}
	else {
		c.compressor().dsp().reset();
	}
	return sent;
}

/// @brief A kit's or an audio track's chain, from the filter's settings to the compressor, the content render left out.
///
/// The block arrives already rendered.
/// @param c                         The chain components.
/// @param filter_                   The clip's filter.
/// @param saturation_               The clip's saturation.
/// @param in                        The clip chain inputs.
/// @param global_effectable_audio   The block, processed in place.
/// @param renderedLastTime          Whether the content rendered this time.
/// @param reverbBuffer              The reverb send.
/// @param tap                       The recorder, called with the block before the reverb send.
/// @return Whether anything was sent to the reverb.
template <typename Tap>
bool clip(fx::ChainCore& c, fx::Filter& filter_, fx::Saturation& saturation_, const fx::ClipChainInputs& in,
          std::span<StereoSample> global_effectable_audio, bool renderedLastTime, fx::ReverbSend reverbBuffer,
          Tap&& tap) {
	const fx::EffectStates& settings = *in.fx.settings;
	int32_t volumePostFX = in.volume_post_fx;

	// The filters' settings, which compensate the post-FX volume.
	volumePostFX = filter_.configure(settings.filter, in.filter, volumePostFX);

	int32_t reverbSendAmount = in.reverb_send;
	int32_t pan = in.pan;

	int32_t postReverbVolume = in.post_reverb_volume_neutral;
	if (in.sidechain_on) {
		if (in.sidechain_hit != 0) {
			c.sidechain().registerHit(in.sidechain_hit);
		}
		int32_t sidechainOutput = c.sidechain().render(global_effectable_audio.size(), in.sidechain_shape,
		                                               settings.sidechain, in.tick_inverse);

		int32_t positivePatchedValue = multiply_32x32_rshift32(sidechainOutput, in.sidechain_depth) + 536870912;

		postReverbVolume = (positivePatchedValue >> 15) * (positivePatchedValue >> 16);
	}

	c.compressor().configure(settings.compressor);
	const q31_t compThreshold = in.compressor_threshold;
	c.compressor().dsp().setThreshold(compThreshold);

	saturation_.process(global_effectable_audio, {.clipping_amount = settings.saturation.clipping_amount,
	                                              .variant = fx::SaturationVariant::CHAIN});

	if (renderedLastTime) {
		filter_.process(global_effectable_audio, in.filter_scratch, *in.fx.noise);
		volumePostFX >>= c.bitcrush().process(global_effectable_audio, in.bitcrush).volume_shift;
	}

	// The soft off of a GRAIN the setting holds, then the mod FX, EQ and delay.
	if (in.fx.mod_fx_type != fx::ModFXType::GRAIN && settings.mod_fx.type == fx::ModFXType::GRAIN) {
		c.granular().startSkippingRendering();
	}
	process_fx(c, global_effectable_audio, in.fx.mod_fx_type, in.fx, &volumePostFX, renderedLastTime, in.fx.grain_send,
	           reverbBuffer);
	if (in.stutter != nullptr) {
		in.stutter->render(global_effectable_audio, in.stutter_rate);
	}
	tap(std::span<const StereoSample>{global_effectable_audio});

	const bool sent = process_reverb_send_and_volume(c, global_effectable_audio, reverbBuffer, volumePostFX,
	                                                 postReverbVolume, reverbSendAmount, pan, true, in.fx.stereo);

	if (compThreshold > 0) {
		c.compressor().dsp().renderVolumeNeutral(global_effectable_audio, volumePostFX);
	}
	else {
		c.compressor().dsp().reset();
	}
	return sent;
}

/// @brief The song master: its mod FX, EQ, delay and send and volume, then @p between, then its filter, bitcrush,
/// stutter, pan and compressor.
///
/// @p between is called with whether the send sent; it runs the reverb and the sample preview and returns the
/// post-reverb inputs, or nothing to end the chain there.
/// @param c               The chain components.
/// @param filter_         The master's filter.
/// @param in              The master chain inputs.
/// @param renderingBuffer The block, processed in place.
/// @param reverbBuffer    The reverb send.
/// @param between         The caller's step between the two halves.
/// @return Whether anything was sent to the reverb.
template <typename Between>
bool master(fx::ChainCore& c, fx::Filter& filter_, const fx::MasterChainInputs& in,
            std::span<StereoSample> renderingBuffer, fx::ReverbSend reverbBuffer, Between&& between) {
	const fx::EffectStates& settings = *in.fx.settings;
	int32_t volumePostFX = in.volume_post_fx;

	if (in.fx.mod_fx_type != fx::ModFXType::GRAIN && settings.mod_fx.type == fx::ModFXType::GRAIN) {
		c.granular().startSkippingRendering();
	}
	process_fx(c, renderingBuffer, in.fx.mod_fx_type, in.fx, &volumePostFX, true, in.fx.grain_send, reverbBuffer);

	const bool sent = process_reverb_send_and_volume(c, renderingBuffer, reverbBuffer, volumePostFX,
	                                                 in.post_reverb_volume, in.reverb_send, 0, false, in.fx.stereo);

	const std::optional<fx::MasterPostInputs> post = between(sent);
	if (!post.has_value()) {
		return sent;
	}

	int32_t masterVolumeAdjustmentL = post->master_volume;
	int32_t masterVolumeAdjustmentR = post->master_volume;
	masterVolumeAdjustmentL = filter_.configure(settings.filter, post->filter, masterVolumeAdjustmentL);
	filter_.process(renderingBuffer, post->filter_scratch, *post->noise);
	masterVolumeAdjustmentL >>= c.bitcrush().process(renderingBuffer, post->bitcrush).volume_shift;

	masterVolumeAdjustmentR = masterVolumeAdjustmentL;

	if (post->stutter != nullptr) {
		post->stutter->render(renderingBuffer, post->stutter_rate);
	}

	if (post->do_pan) {
		masterVolumeAdjustmentL = multiply_32x32_rshift32(masterVolumeAdjustmentL, post->pan_l) << 2;
		masterVolumeAdjustmentR = multiply_32x32_rshift32(masterVolumeAdjustmentR, post->pan_r) << 2;
	}

	c.compressor().configure(settings.compressor);
	c.compressor().dsp().render(renderingBuffer, masterVolumeAdjustmentL >> 1, masterVolumeAdjustmentR >> 1,
	                            post->song_volume >> 3);
	return sent;
}

} // namespace chain_reference
