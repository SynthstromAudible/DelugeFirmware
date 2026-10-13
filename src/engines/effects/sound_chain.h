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
#include "engines/effects/chain_core.h"
#include "engines/effects/stutter.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::effects {

/// @brief What a sound's post-voice chain reads for one block, as the sound resolved it from its patcher's finals and
/// its unpatched params.
struct SoundChainInputs {
	FxInputs fx;
	BitcrushParams bitcrush;
	int32_t post_fx_volume;       ///< GLOBAL_VOLUME_POST_FX's final, Q31.
	int32_t post_reverb_volume;   ///< GLOBAL_VOLUME_POST_REVERB_SEND's final, Q31.
	int32_t reverb_send;          ///< The reverb send amount, Q31.
	bool sound;                   ///< Whether the sound has voices.
	int32_t compressor_threshold; ///< The threshold param, Q31; at or below 0 the compressor resets instead.
	Stutter* stutter;             ///< The song's stutter while this sound is the one stuttering, else null.
	uint32_t stutter_rate;        ///< The stutter's rate on the delay's rate scale (16777216 neutral).
};

/// @brief A sound's effects after its voices: bitcrush, mod FX or grain, EQ, delay, stutter, reverb send and volume,
/// then the compressor, in that order. The per-voice filter and saturation stay with the sound, and its sidechain is a
/// patch source the sound renders before its voices.
class SoundChain {
public:
	/// @param arena Where the delay's tapes come from. A sound's entry into GRAIN clears the granular, and its first
	/// block ramps from its own post-reverb volume.
	SoundChain(RingAllocator mod_fx_allocator, RingAllocator granular_allocator, DelayArena& arena);

	[[nodiscard]] ChainCore& core() { return core_; }
	[[nodiscard]] const ChainCore& core() const { return core_; }

	/// @brief Renders the chain over @p block (the voices' sum) in place.
	/// @param send The reverb send, one value per frame.
	/// @return Whether the block fed the reverb.
	bool process(std::span<dsp::StereoSample> block, const SoundChainInputs& in, ReverbSend send);

	/// @return The effects the chain renders, in order (chain.h's names).
	[[nodiscard]] static std::span<const std::string_view> order();

private:
	ChainCore core_;
};

} // namespace deluge::effects
