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
#include "RZA1/system/iodefines/dmac_iodefine.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/root_complex/usb_hosted.h"

extern "C" {
#include "drivers/uart/uart.h"

// XXX: manually declared here instead of including r_usb_extern.h becuse that header doesn't compile as C++ code.
extern usb_regadr_t usb_hstd_get_usb_ip_adr(uint16_t ipno);

usb_utr_t g_usb_midi_send_utr[USB_NUM_USBIP];
usb_utr_t g_usb_midi_recv_utr[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES];

uint8_t currentDeviceNumWithSendPipe[2] = {MAX_NUM_USB_MIDI_DEVICES, MAX_NUM_USB_MIDI_DEVICES};

uint32_t timeLastBRDY[USB_NUM_USBIP];

void usbReceiveComplete(int32_t ip, int32_t deviceNum, int32_t tranlen) {
	ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][deviceNum];

	connectedDevice->numBytesReceived = 64 - tranlen; // Seems wack, but yet, tranlen is now how many bytes didn't get
	                                                  // received out of the original transfer size
	// Warning - sometimes (with a Teensy, e.g. my knob box), length will be 0. Not sure why - but we need to cope with
	// that case.

	connectedDevice->currentlyWaitingToReceive = 0; // Take note that we need to set up another receive
}

// We now bypass calling this for successful as peripheral on A1 (see usb_pstd_bemp_pipe_process_rohan_midi())
void usbSendCompletePeripheralOrA1(usb_utr_t* p_mess, uint16_t data1, uint16_t data2) {
	// If error, forget about device.
	// No actually don't - sometimes there'll be an error if another device connected or disconnected from hub during
	// fast MIDI sending. This seems to happen even though I've stopped it from setting up or down the out-pipe as it
	// goes
	if (p_mess->status == USB_DATA_ERR) {
		if constexpr (ALPHA_OR_BETA_VERSION) {
			uartPrintln("USB Send error");
		}
		// g_usb_host_connected[deviceNum] = 0;
	}

#if USB_NUM_USBIP == 1
	int32_t ip = 0;
#else
	int32_t ip = p_mess->ip;
#endif

	usbSendCompleteAsHost(ip);
}

void usbReceiveCompletePeripheralOrA1(usb_utr_t* p_mess, uint16_t data1, uint16_t data2) {

#if USB_NUM_USBIP == 1
	int32_t ip = 0;
#else
	int32_t ip = p_mess->ip;
#endif

	if (p_mess->status == USB_DATA_ERR) {
		return; // Can happen if user disconnects device - totally normal
	}

	int32_t deviceNum = p_mess - &g_usb_midi_recv_utr[ip][0];

	// Are there actually any other possibilities that could happen here? Can't remember.
	if (p_mess->status != USB_DATA_SHT) {
		uartPrint("status: ");
		uartPrintNumber(p_mess->status);
	}

	usbReceiveComplete(ip, deviceNum, p_mess->tranlen);
}

void brdyOccurred(int32_t ip) {
	timeLastBRDY[ip] = DMACnNonVolatile(SSI_TX_DMA_CHANNEL).CRSA_n; // Reading this not as volatile works fine
}
}

namespace deluge::io::usb {
volatile bool usbLock;
bool anythingInUSBOutputBuffer;

uint8_t stopSendingAfterDeviceNum[USB_NUM_USBIP];
uint8_t usbDeviceNumBeingSentToNow[USB_NUM_USBIP];
uint8_t anyUSBSendingStillHappening[USB_NUM_USBIP];

void usbSetup() {
	g_usb_peri_connected = 0; // Needs initializing with A2 driver

	for (int32_t ip = 0; ip < USB_NUM_USBIP; ip++) {

		// This might not be used due to the change in r_usb_hlibusbip (deluge is host) to call usbSendCompleteAsHost()
		// directly and the change in r_usb_plibusbip (deluge is pheriperal) to just set some variables or it might be
		// used for some other interrups like error conditions???
		// TODO: try to delet this and see if something breaks..
		g_usb_midi_send_utr[ip].complete = (usb_cb_t)usbSendCompletePeripheralOrA1;

		g_usb_midi_send_utr[ip].p_setup = 0; /* Setup message address set */
		g_usb_midi_send_utr[ip].segment = USB_TRAN_END;
		g_usb_midi_send_utr[ip].ip = ip;
		g_usb_midi_send_utr[ip].ipp = usb_hstd_get_usb_ip_adr(ip);

		for (int32_t d = 0; d < MAX_NUM_USB_MIDI_DEVICES; d++) {
			g_usb_midi_recv_utr[ip][d].p_tranadr = connectedUSBMIDIDevices[ip][d].receiveData;
			g_usb_midi_recv_utr[ip][d].complete = (usb_cb_t)usbReceiveCompletePeripheralOrA1;

			g_usb_midi_recv_utr[ip][d].p_setup = 0; /* Setup message address set */
			g_usb_midi_recv_utr[ip][d].segment = USB_TRAN_END;
			g_usb_midi_recv_utr[ip][d].ip = ip;
			g_usb_midi_recv_utr[ip][d].ipp = usb_hstd_get_usb_ip_adr(ip);
		}
	}
}

} // namespace deluge::io::usb
