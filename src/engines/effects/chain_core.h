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
#include "dsp-next/fixed/noise/cong.h"
#include "engines/effects/bitcrush.h"
#include "engines/effects/compressor.h"
#include "engines/effects/delay.h"
#include "engines/effects/equalizer.h"
#include "engines/effects/granular.h"
#include "engines/effects/mod_fx.h"
#include "engines/effects/send_and_volume.h"
#include "engines/effects/sidechain.h"
#include "engines/effects/state.h"

#include <cstdint>
#include <span>

namespace deluge::effects {

/// @brief The reverb send a chain adds into: one value per frame of the block, Q31; the engine's reverb reads it.
using ReverbSend = std::span<int32_t>;

/// @brief What every home's mod FX, EQ and delay read for one block, as the owner resolved them from its params.
struct FxInputs {
	/// The owner's render copy of its effect settings. Never null.
	const EffectStates* settings;
	/// The mod FX type in effect this block: a host chain's is NONE while its selected knob is at its minimum (its
	/// soft off); a sound's is its setting.
	ModFXType mod_fx_type;
	int32_t mod_fx_rate;     ///< The rate param's final value, UQ0.32 per frame.
	int32_t mod_fx_depth;    ///< The depth param's final value, Q31.
	int32_t mod_fx_offset;   ///< The offset knob, Q31 signed.
	int32_t mod_fx_feedback; ///< The feedback knob, Q31 signed.
	EqualizerParams eq;
	Delay::State delay; ///< The block's working state, which the owner set up on the chain's delay.
	bool stereo;        ///< Whether the engine renders in stereo.
	int32_t grain_send; ///< The granular's reverb send before the post-FX volume scales it, Q31.
	/// The synth's noise stream: the filters draw from it, written back after each block. Never null once built.
	dsp::fixed::Cong* noise = nullptr;
};

/// @brief The effect components every home has, and the mod FX, EQ and delay stage they share.
///
/// A home's runtime (SoundChain, ClipChain, MasterChain) owns one and adds the components only it has. The owner
/// reaches the components by name for its hand-overs (the mod FX and granular types, the delay's type and reserves).
class ChainCore {
public:
	/// @param mod_fx_allocator Where the mod FX's ring comes from.
	/// @param granular_allocator Where the granular's State and ring come from.
	/// @param enter What entering GRAIN does to the granular: CLEAR for a sound, KEEP for a host chain.
	/// @param arena Where the delay's tapes come from.
	/// @param last_post_reverb_volume The send and volume's first ramp start, Q31; -1 leaves it for the first block.
	ChainCore(RingAllocator mod_fx_allocator, RingAllocator granular_allocator, EnterGrain enter, DelayArena& arena,
	          int32_t last_post_reverb_volume);
	ChainCore(const ChainCore&) = delete;
	ChainCore& operator=(const ChainCore&) = delete;

	[[nodiscard]] Equalizer& equalizer() { return equalizer_; }
	[[nodiscard]] const Equalizer& equalizer() const { return equalizer_; }
	[[nodiscard]] Delay& delay() { return delay_; }
	[[nodiscard]] const Delay& delay() const { return delay_; }
	[[nodiscard]] Bitcrush& bitcrush() { return bitcrush_; }
	[[nodiscard]] const Bitcrush& bitcrush() const { return bitcrush_; }
	[[nodiscard]] ModFx& mod_fx() { return mod_fx_; }
	[[nodiscard]] const ModFx& mod_fx() const { return mod_fx_; }
	[[nodiscard]] Granular& granular() { return granular_; }
	[[nodiscard]] const Granular& granular() const { return granular_; }
	[[nodiscard]] Compressor& compressor() { return compressor_; }
	[[nodiscard]] const Compressor& compressor() const { return compressor_; }
	[[nodiscard]] Sidechain& sidechain() { return sidechain_; }
	[[nodiscard]] const Sidechain& sidechain() const { return sidechain_; }
	[[nodiscard]] SendAndVolume& send_and_volume() { return send_and_volume_; }
	[[nodiscard]] const SendAndVolume& send_and_volume() const { return send_and_volume_; }

	/// @brief The mod FX (or the granular, when the type in effect is GRAIN), the EQ, then the delay, over @p block in
	/// place.
	/// @param post_fx_volume The post-FX volume, Q31: the mod FX and the granular scale it.
	/// @param sound Whether any sound came in this block.
	/// @param block The audio to process in place.
	/// @param in The block's resolved effect inputs.
	/// @param send The granular's reverb feed is added into it.
	void render_fx(std::span<dsp::StereoSample> block, const FxInputs& in, int32_t& post_fx_volume, bool sound,
	               ReverbSend send);

	/// @brief A host chain's soft off of a GRAIN it holds (its selected knob at its minimum): the grain's ring is taken
	/// up afresh while the type stays GRAIN.
	/// @param in The block's resolved effect inputs.
	void soft_off_grain(const FxInputs& in);

private:
	Equalizer equalizer_;
	Delay delay_;
	Bitcrush bitcrush_;
	ModFx mod_fx_;
	Granular granular_;
	Compressor compressor_;
	Sidechain sidechain_;
	SendAndVolume send_and_volume_;
};

} // namespace deluge::effects
