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
/// owns the USB side of both streams and matches the two clocks; the application sees them as the USB port of each
/// render block (`audio_ports.h`), in step with the codec's.
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

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_USB_AUDIO_H
