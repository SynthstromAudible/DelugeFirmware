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

#include <cstdint>
#include <span>

namespace deluge::effects {

/// @brief One block's send and volume: the volumes and send the owner resolved (Q31, 134217728 is unity for the
/// volumes), its pan (Q31, 0 is the centre), whether to ramp the post-reverb volume from the last block's, and whether
/// the engine renders in stereo (pan applies only then).
struct SendAndVolumeParams {
	int32_t post_fx_volume{0};
	int32_t post_reverb_volume{0};
	int32_t reverb_send{0};
	int32_t pan{0};
	bool ramp{false};
	bool stereo{true};
};

/// @brief Sends to the reverb, then applies the post-FX and post-reverb volume with pan, ramping the post-reverb
/// volume across the block from the last block's.
class SendAndVolume {
public:
	/// @brief Ramps first from @p last_post_reverb_volume; -1 is "unset", which prime() fills in.
	/// @param last_post_reverb_volume The volume to ramp from, Q31, or -1 for unset.
	constexpr explicit SendAndVolume(int32_t last_post_reverb_volume = 0)
	    : last_post_reverb_volume_{last_post_reverb_volume} {}

	/// @brief Adds the send to @p reverb and applies the volumes to @p block in place; returns whether anything was
	/// sent (the engine notes when reverb last ran).
	/// @param block  The frames to apply the volumes to, in place.
	/// @param reverb The reverb input the send is added to, one value per frame of @p block.
	/// @param params The block's volumes, send and pan.
	/// @return True if anything was sent to the reverb.
	[[nodiscard]] bool process(std::span<dsp::StereoSample> block, std::span<int32_t> reverb,
	                           const SendAndVolumeParams& params);

	/// @brief Before a first block: ramp from @p post_reverb_volume, if nothing has been ramped from yet.
	/// @param post_reverb_volume The volume to ramp from, Q31.
	void prime(int32_t post_reverb_volume) {
		if (last_post_reverb_volume_ == -1) {
			last_post_reverb_volume_ = post_reverb_volume;
		}
	}

	/// @brief Forget the last block's volume: the next prime() sets it.
	void forget() { last_post_reverb_volume_ = -1; }

private:
	int32_t last_post_reverb_volume_; ///< Q31; -1 until primed or processed
};

} // namespace deluge::effects
