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

#include "usb_state.h"

namespace deluge::io::usb {
volatile bool usbLock;
bool anythingInUSBOutputBuffer;

uint8_t stopSendingAfterDeviceNum[USB_NUM_USBIP];
uint8_t usbDeviceNumBeingSentToNow[USB_NUM_USBIP];
uint8_t anyUSBSendingStillHappening[USB_NUM_USBIP];

} // namespace deluge::io::usb
extern "C" {

usb_utr_t g_usb_midi_send_utr[USB_NUM_USBIP];
usb_utr_t g_usb_midi_recv_utr[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES];

uint8_t currentDeviceNumWithSendPipe[2] = {MAX_NUM_USB_MIDI_DEVICES, MAX_NUM_USB_MIDI_DEVICES};
}
