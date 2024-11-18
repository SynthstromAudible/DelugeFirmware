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

#include "usb_descriptors.h"
#include "io/debug/log.h"
#include "portable/renesas/rusb1/dcd_rusb1.h"
#include "tusb.h"
#include <inttypes.h>

// Get VID from
// http://www.mcselec.com/index.php?page=shop.product_details&flypage=shop.flypage&product_id=92&category_id=20&option=com_phpshop&Itemid=1
#define USB_VENDORID (0x16D0)  /* Vendor ID */
#define USB_PRODUCTID (0x0CE2) /* Product ID */

static void usbdConfigurePipes();

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// String Descriptor Index
enum {
	STRID_LANGID = 0,
	STRID_MANUFACTURER,
	STRID_PRODUCT,
	STRID_MIDI_OUT,
	STRID_MIDI_IN,
};

// array of pointer to string descriptors
// TODO: use u16 constants instead?
char const* string_desc_arr[] = {
    [STRID_LANGID] = (const char[]){0x09, 0x04}, // Supported language is English (0x0409)
    [STRID_MANUFACTURER] = "Synthstrom Audible", // Manufacturer
    [STRID_PRODUCT] = "Deluge (TinyUSB)",        // Product TODO: change back to "Deluge"
    [STRID_MIDI_OUT] = "OUT",
    [STRID_MIDI_IN] = "IN",
};

static uint16_t _desc_str[48 + 1];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
	(void)langid;
	size_t chr_count;

	switch (index) {
	case STRID_LANGID:
		memcpy(&_desc_str[1], string_desc_arr[0], 2);
		chr_count = 1;
		break;

	default:
		// Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
		// https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

		if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
			return NULL;

		const char* str = string_desc_arr[index];

		// Cap at max char
		chr_count = strlen(str);
		size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1; // -1 for string type
		if (chr_count > max_count)
			chr_count = max_count;

		// Convert ASCII string into UTF-16
		for (size_t i = 0; i < chr_count; i++) {
			_desc_str[1 + i] = str[i];
		}
		break;
	}

	// first byte is length (including header), second byte is string type
	_desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

	return _desc_str;
}

//--------------------------------------------------------------------+
// Interface Descriptors
//--------------------------------------------------------------------+
//

enum { ITF_NUM_MIDI = 0, ITF_NUM_MIDI_STREAMING, ITF_NUM_TOTAL };

#define NCABLES 3
#define EPNUM_MIDI_IN 1
#define EPNUM_MIDI_OUT 2

// TODO: move this in to usbd.h in tinyusb
// clang-format off
#define TUD_MIDI_DESC_JACK_EMBEDDED_LEN (6 + 9)

#define TUD_MIDI_JACKID_IN_EMBONLY(_cablenum) (((_cablenum) - 1) * 2 + 1)
#define TUD_MIDI_JACKID_OUT_EMBONLY(_cablenum) (((_cablenum) - 1) * 2 + 2)

#define TUD_MIDI_DESC_JACK_EMBEDDED(_cablenum, _stridx) \
  /* MS In Jack (Embedded) */\
  6, TUSB_DESC_CS_INTERFACE, MIDI_CS_INTERFACE_IN_JACK, MIDI_JACK_EMBEDDED, TUD_MIDI_JACKID_IN_EMBONLY(_cablenum), _stridx,\
  /* MS Out Jack (Embedded), connected to In Jack External */ \
  9, TUSB_DESC_CS_INTERFACE, MIDI_CS_INTERFACE_OUT_JACK, MIDI_JACK_EMBEDDED, TUD_MIDI_JACKID_OUT_EMBONLY(_cablenum), 1, TUD_MIDI_JACKID_IN_EMBONLY(_cablenum), 1, _stridx
// clang-format on

#define CONFIG_TOTAL_LEN                                                                                               \
	(TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_HEAD_LEN + TUD_MIDI_DESC_JACK_EMBEDDED_LEN * NCABLES                          \
	 + TUD_MIDI_DESC_EP_LEN(NCABLES) * 2)

uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 500),

    TUD_MIDI_DESC_HEAD(ITF_NUM_MIDI, 0, NCABLES),
    TUD_MIDI_DESC_JACK_EMBEDDED(1, 0),
    TUD_MIDI_DESC_JACK_EMBEDDED(2, 0),
    TUD_MIDI_DESC_JACK_EMBEDDED(3, 0),

    TUD_MIDI_DESC_EP(EPNUM_MIDI_OUT, 64, NCABLES),
    TUD_MIDI_JACKID_OUT_EMBONLY(1),
    TUD_MIDI_JACKID_OUT_EMBONLY(2),
    TUD_MIDI_JACKID_OUT_EMBONLY(3),

    TUD_MIDI_DESC_EP(EPNUM_MIDI_IN | TUSB_DIR_IN_MASK, 64, NCABLES),
    TUD_MIDI_JACKID_IN_EMBONLY(1),
    TUD_MIDI_JACKID_IN_EMBONLY(2),
    TUD_MIDI_JACKID_IN_EMBONLY(3),
};

#if TUD_OPT_HIGH_SPEED
uint8_t const desc_hs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 500),

    TUD_MIDI_DESC_HEAD(ITF_NUM_MIDI, 0, NCABLES),
    TUD_MIDI_DESC_JACK_EMBEDDED(1, 0),
    TUD_MIDI_DESC_JACK_EMBEDDED(2, 0),
    TUD_MIDI_DESC_JACK_EMBEDDED(3, 0),

    TUD_MIDI_DESC_EP(EPNUM_MIDI_OUT, 512, NCABLES),
    TUD_MIDI_JACKID_OUT_EMBONLY(1),
    TUD_MIDI_JACKID_OUT_EMBONLY(2),
    TUD_MIDI_JACKID_OUT_EMBONLY(3),

    TUD_MIDI_DESC_EP(EPNUM_MIDI_IN | TUSB_DIR_IN_MASK, 512, NCABLES),
    TUD_MIDI_JACKID_IN_EMBONLY(1),
    TUD_MIDI_JACKID_IN_EMBONLY(2),
    TUD_MIDI_JACKID_IN_EMBONLY(3),
};
}
;
#endif

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
	(void)index; // for multiple configurations

	usbdConfigurePipes();

#if TUD_OPT_HIGH_SPEED
	// Although we are highspeed, host may be fullspeed.
	return (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_hs_configuration : desc_fs_configuration;
#else
	return desc_fs_configuration;
#endif
}

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_VENDORID,
    .idProduct = USB_PRODUCTID,
    .bcdDevice = 0x0200,

    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = 0, // no serial numbers

    .bNumConfigurations = 0x01,
};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const* tud_descriptor_device_cb(void) {
	return (uint8_t const*)&desc_device;
}

static void usbdConfigurePipes() {
	rusb1_pipe_config_t midi_in_pipe = {
	    .buffer_offset = 8,
	    .buffer_size = 4,
	    .flags =
	        {
	            .double_buffer = 1,
	            .continuous = 0,
	        },
	};
	rusb1_pipe_config_t midi_out_pipe = {
	    .buffer_offset = 16,
	    .buffer_size = 4,
	    .flags =
	        {
	            .double_buffer = 1,
	            .continuous = 0,
	        },
	};

	rusb1_configure_pipe(0, EPNUM_MIDI_IN, TUSB_DIR_IN, kMidiInPipe, &midi_in_pipe);
	rusb1_configure_pipe(0, EPNUM_MIDI_OUT, TUSB_DIR_OUT, kMidiOutPipe, &midi_out_pipe);

	D_PRINTLN("Pipes configured");
}
