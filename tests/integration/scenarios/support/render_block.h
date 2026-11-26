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

#include "libdeluge/app.h"
#include "libdeluge/audio_ports.h"
#include "libdeluge/types.h"

#include <cstdint>

namespace scenarios {

/// One render block with the codec's pair only, as the host board has no USB device: `frames` frames of `in` (may be
/// null) into `out`.
inline void renderCodecBlock(const DelugeStereoSample* in, DelugeStereoSample* out, uint32_t frames) {
	const DelugeStereoSample* const inPairs[1] = {in};
	DelugeStereoSample* const outPairs[1] = {out};
	DelugeAudioBlock block{};
	block.frames = frames;
	block.ports[DELUGE_PORT_CODEC] = DelugeAudioPort{
	    .in = in != nullptr ? inPairs : nullptr,
	    .out = outPairs,
	    .in_pairs = static_cast<uint8_t>(in != nullptr ? 1 : 0),
	    .out_pairs = 1,
	    .flags = DELUGE_PORT_MAIN_MIX_OUT | DELUGE_PORT_MONITORABLE | DELUGE_PORT_CODEC_INPUT,
	    .far_receiving = true,
	    .far_sending = true,
	    .out_silent = 1,
	};
	deluge_app_render(&block);
}

} // namespace scenarios
