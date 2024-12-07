/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "definitions.h"

#ifdef __cplusplus

namespace deluge::io::usb {

/// When true, something is using the low-level USB structures.
///
/// XXX: this is super ill-defined and seems to mostly exist to avoid reentrant ISR problems
extern volatile bool usbLock;

/// RAII lock control for the USB lock
class USBAutoLock {
public:
	USBAutoLock() { usbLock = true; }
	~USBAutoLock() { usbLock = false; }

	USBAutoLock(const USBAutoLock&) = delete;
	USBAutoLock(USBAutoLock&&) = delete;
	USBAutoLock& operator=(const USBAutoLock&) = delete;
	USBAutoLock& operator=(USBAutoLock&&) = delete;
};

/// When true, some data is queued to the USB output buffer.
extern bool anythingInUSBOutputBuffer;

/// Last device to send to
extern uint8_t stopSendingAfterDeviceNum[USB_NUM_USBIP];

/// Current hosted device number, used in host send completion callback
extern uint8_t usbDeviceNumBeingSentToNow[USB_NUM_USBIP];

/// Flag to prevent reentrant sending
extern uint8_t anyUSBSendingStillHappening[USB_NUM_USBIP];

} // namespace deluge::io::usb

extern "C" {
#endif

// defined in r_usb_pdriver.c
extern uint16_t g_usb_peri_connected;

// defined in r_usb_cdataio.c
extern usb_utr_t* g_p_usb_pipe[USB_MAX_PIPE_NO + 1u];

extern usb_utr_t g_usb_midi_send_utr[USB_NUM_USBIP];
extern usb_utr_t g_usb_midi_recv_utr[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES];

// defined in r_usb_hmidi_driver.c
extern uint16_t g_usb_hmidi_tmp_ep_tbl[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES][(USB_EPL * 2) + 1];

// One without, and one with, interrupt endpoints
extern uint8_t currentDeviceNumWithSendPipe[2];

#ifdef __cplusplus
}
#endif
