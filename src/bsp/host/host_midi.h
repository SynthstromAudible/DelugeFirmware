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

/// host_midi — serial (DIN) MIDI for the emulator.
///
/// The Deluge's DIN ports are a raw 31250-baud MIDI byte stream; on the host we bridge that
/// stream to/from an arbitrary file descriptor, so it needs no MIDI library (which matters in
/// the default -m32 build, where there is no 32-bit libasound). The recommended endpoint is an
/// ALSA snd-virmidi raw device (`sudo modprobe snd-virmidi`; then `/dev/snd/midiCxDy`), which
/// appears as a real ALSA-seq port any DAW / keyboard can `aconnect` to. FIFOs or a PTY work too.
///
/// Config (env):
///   DELUGE_HOST_MIDI       bidirectional device opened O_RDWR (e.g. a snd-virmidi node). If unset,
///                          auto-detects the first /dev/snd/midiC*D* device.
///   DELUGE_HOST_MIDI_IN    read-only endpoint (MIDI into the Deluge); overrides the in side.
///   DELUGE_HOST_MIDI_OUT   write-only endpoint (MIDI out of the Deluge); overrides the out side.
///   DELUGE_HOST_MIDI=off   disable (the scripted DELUGE_HOST_MIDI_NOTE harness still works).

#ifndef BSP_HOST_MIDI_H
#define BSP_HOST_MIDI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Open the MIDI endpoint(s) per the environment. Returns true if an inbound endpoint is active
/// (so the DIN read path should use it instead of the scripted harness). Idempotent.
bool host_midi_open(void);

/// True once any MIDI endpoint is open.
bool host_midi_active(void);

/// True if an inbound (host→Deluge) endpoint is connected.
bool host_midi_have_input(void);

/// Non-blocking: dequeue one inbound MIDI byte into `*b`. Returns true if a byte was available.
bool host_midi_read_byte(uint8_t* b);

/// Non-blocking: send `len` MIDI bytes out (host←Deluge). Drops on a full/broken endpoint.
void host_midi_write(const uint8_t* src, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif // BSP_HOST_MIDI_H
