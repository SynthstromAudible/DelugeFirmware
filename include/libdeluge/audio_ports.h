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

/// libdeluge/audio_ports.h — the render block as a set of ports.
///
/// Each render block reaches the application as one `DelugeAudioBlock`: for every port, its input pairs and its output
/// pairs, each a block of `frames` stereo frames, all aligned to the same instant. A port's pairs and properties are
/// fixed before the first block and stay so until power-off. The application acts on a port's properties, never on
/// which port it is; rates, clocks, packet formats, latency and volume are all below this line.
#ifndef LIBDELUGE_AUDIO_PORTS_H
#define LIBDELUGE_AUDIO_PORTS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// What the application may do with a port.
typedef enum DelugeAudioPortFlags : uint8_t {
	/// Output pair 0 carries the main mix, and is not a destination of its own.
	DELUGE_PORT_MAIN_MIX_OUT = 1 << 0,
	/// The inputs may be mixed straight into the outputs (direct monitoring).
	DELUGE_PORT_MONITORABLE = 1 << 1,
	/// The inputs take the codec's normalising, jack detection and feedback rules.
	DELUGE_PORT_CODEC_INPUT = 1 << 2,
} DelugeAudioPortFlags;

/// Where each port sits in a block. The application finds what it needs by a port's properties and pairs; the indices
/// are the platform's.
typedef enum DelugeAudioPortIndex : uint8_t {
	DELUGE_PORT_CODEC = 0,
	DELUGE_PORT_USB = 1,
	DELUGE_PORT_COUNT = 2,
} DelugeAudioPortIndex;

/// One port of a render block.
typedef struct DelugeAudioPort {
	/// `in_pairs` blocks of the block's `frames`, read by the application; NULL when there are none.
	const DelugeStereoSample* const* in;
	/// `out_pairs` blocks of the block's `frames`, written by the application; NULL when there are none.
	DelugeStereoSample* const* out;
	uint8_t in_pairs;
	uint8_t out_pairs;
	/// `DelugeAudioPortFlags`.
	uint8_t flags;
	/// The far end takes our output: a USB host collects it.
	bool far_receiving;
	/// The far end sends us input: a USB host streams to us.
	bool far_sending;
	/// Written by the application: bit n set means output pair n is silent this block, and the platform sends silence
	/// for it whatever the block holds. The platform sets every bit before the render.
	uint32_t out_silent;
} DelugeAudioPort;

/// One render block: every port, and what the platform's own audio work cost.
typedef struct DelugeAudioBlock {
	uint32_t frames;
	DelugeAudioPort ports[DELUGE_PORT_COUNT];
	/// The cycles the platform spent on the last block's ports outside the render (reading and packing USB), for the
	/// voice budget to count with the render's own; 0 for the first block.
	uint32_t platform_cycles;
} DelugeAudioBlock;

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_AUDIO_PORTS_H
