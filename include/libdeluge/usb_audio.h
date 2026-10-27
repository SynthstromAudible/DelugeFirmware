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

/// libdeluge/usb_audio.h — the USB device, and USB audio as a peripheral.
///
/// The device presents USB-MIDI and, when started with audio, a USB Audio Class 2.0 function: a stereo stream
/// from the host (its speaker) and one to it (its microphone), both 44.1 kHz. Our sample clock sets the rate: the
/// stream to the host carries as many frames as the codec has played since its last packet, and is the implicit
/// feedback for the stream from the host, so a host that honours it sends exactly as fast as we play. The platform
/// owns the USB side of both streams and matches the two clocks; the application sees whole blocks of frames, in
/// step with its render.
#ifndef LIBDELUGE_USB_AUDIO_H
#define LIBDELUGE_USB_AUDIO_H

#include "abi.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Put the USB device on the bus. Until this is called the device does not enumerate. `with_audio` decides whether
/// its configuration carries the USB audio function as well as USB-MIDI; it holds until the next power cycle,
/// because a host reads the configuration once. Call once, after the settings that decide it are read; later calls
/// do nothing. [task]
void deluge_usb_device_start(bool with_audio) DELUGE_NOEXCEPT;

/// Whether the device was started with the USB audio function. [task] [audio]
bool deluge_usb_audio_present(void) DELUGE_NOEXCEPT;

/// Whether the host is streaming audio to us: it has selected the stream's operational alternate setting, and
/// packets are arriving. [task] [audio]
bool deluge_usb_audio_receiving(void) DELUGE_NOEXCEPT;

/// Whether the host is collecting our audio: it has selected the stream's operational alternate setting. [task]
/// [audio]
bool deluge_usb_audio_sending(void) DELUGE_NOEXCEPT;

/// Read the host's audio for the render block that is about to play: `frames` stereo frames into `dst`, each
/// channel a full-scale signed 32-bit sample (the host's 16 or 24 bits in the top bits, as the codec's input
/// arrives). Frames with nothing from the host behind them - before streaming starts, after it stops, or on an
/// underrun - are silence. The platform keeps the stream's latency in a fixed window, dropping or repeating the odd
/// frame if a host that ignores implicit feedback drifts from our clock. Returns how many frames came from the host.
/// [audio]
uint32_t deluge_usb_audio_read(DelugeStereoSample* dst, uint32_t frames) DELUGE_NOEXCEPT;

/// Queue `frames` stereo frames for the host, each channel a full-scale signed 32-bit sample (cut to the stream's
/// 16 or 24 bits on the way out). Call once per render block, with that block's frames, in order. Dropped when the
/// host isn't collecting, or past what the queue can hold. [audio]
void deluge_usb_audio_write(const DelugeStereoSample* src, uint32_t frames) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_USB_AUDIO_H
