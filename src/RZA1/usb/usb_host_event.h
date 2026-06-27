/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#ifdef __cplusplus
extern "C" {
#endif

/// USB host enumeration events, recorded by the USB host stack and drained by
/// higher layers (the BSP MIDI port, then the app). This is the pull side of the
/// boundary: the stack *records* an event here; nothing is pushed upward. The app
/// polls and decides what (if anything) to display — the stack does no UI or
/// localization. A small ring buffers bursts (a hub attach enumerates several
/// devices); the oldest event is dropped if the consumer falls far behind.
enum UsbHostEvent {
    USB_HOST_EVENT_NONE = 0,
    USB_HOST_EVENT_HUB_ATTACHED,
    USB_HOST_EVENT_DEVICE_DETACHED,
    USB_HOST_EVENT_DEVICE_NOT_RECOGNIZED,
    USB_HOST_EVENT_DEVICES_MAX,
};

/// Record a USB host event (called from the USB host stack). [task] [isr]
void usbHostEventRecord(int event);

/// Return and remove the oldest pending USB host event (USB_HOST_EVENT_NONE if
/// the queue is empty). [task]
int usbHostEventTake(void);

#ifdef __cplusplus
}
#endif
