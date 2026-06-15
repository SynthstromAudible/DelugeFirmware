/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

/*
 * Portions of this code initially copied from the relevant sample code by Renesas.
 *
 * These global config definitions are used in USBConnection.c
 *
 * The connect functions in USBConnection.c are called around line 800 of deluge_main
 */

/***********************************************************************************************************************
Includes   <System Includes> , "Project Includes"
***********************************************************************************************************************/
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"

/***********************************************************************************************************************
Macro definitions
***********************************************************************************************************************/
#define USB_BCDNUM (0x0200u)  /* bcdUSB */
#define USB_RELEASE (0x0200u) /* Release Number */
#define USB_CONFIGNUM (1u)    /* Configuration number */
#define USB_DCPMAXP (64u)     /* DCP max packet size */

// Get VID from
// http://www.mcselec.com/index.php?page=shop.product_details&flypage=shop.flypage&product_id=92&category_id=20&option=com_phpshop&Itemid=1
#define USB_VENDORID (0x16D0)  /* Vendor ID */
#define USB_PRODUCTID (0x0CE2) /* Product ID */

// size of the USB configuration, including all interfaces
/* 9 - config description
 * 9 - interface description
 * 7 - MIDI streaming header
 * (6+9)*ncables - in and out jack per cable
 * (9+4+ncables)*2 - shared bulk endpoint, descriptors include list of cables
 * for easy c+p -> 9+9+7+((6+9)*ncables)+(9+4+ncables)*2
 */
#define NCABLES 3
#define USB_MIDI_CD_WTOTALLENGTH (9 + 7 + (15 * NCABLES) + (13 + NCABLES) * 2)
// 9 for config descriptor. Add any additional config lengths here
#define TOTAL_CONFIG_LENGTH (9 + USB_MIDI_CD_WTOTALLENGTH)
// Good summary ref on overall USB structure https://www.beyondlogic.org/usbnutshell/usb5.shtml

// USB midi defines
#define CS_INTERFACE 0x24
#define CS_ENDPOINT 0x25
#define MIDI_IN_JACK 0x02
#define MIDI_OUT_JACK 0x03
/***********************************************************************************************************************
Exported global variables (to be accessed by other files)
***********************************************************************************************************************/
/* Standard Device Descriptor
 * Top level USB descriptor
 * Used by host to enumerate device - e.g. what is this
 * bytes 4-7 declares that the device is specified at the interface level
 * bytes 8-16 declare that it's a synthstrom deluge (presumably)
 * byte 17 says how many configurations are available - e.g. it could
 * offer both an FS and HS configuration and allow the host to choose,
 * but in this case we offer a single USB midi FS config
 */
uint8_t g_midi_device[USB_DD_BLENGTH + (USB_DD_BLENGTH % 2)] = {
    USB_DD_BLENGTH,                                        /*  0:bLength */
    USB_DT_DEVICE,                                         /*  1:bDescriptorType */
    (uint8_t)(USB_BCDNUM & (uint8_t)0xff),                 /*  2:bcdUSB_lo */
    (uint8_t)((uint8_t)(USB_BCDNUM >> 8) & (uint8_t)0xff), /*  3:bcdUSB_hi */
                                                           // Device to be specified at interface level
    0x00,                                                  /*  4:bDeviceClass */
    0x00,                                                  /*  5:bDeviceSubClass */
    0x00,                                                  /*  6:bDeviceProtocol */

    (uint8_t)USB_DCPMAXP,                                     /*  7:bMAXPacketSize(for DCP) */
    (uint8_t)(USB_VENDORID & (uint8_t)0xff),                  /*  8:idVendor_lo */
    (uint8_t)((uint8_t)(USB_VENDORID >> 8) & (uint8_t)0xff),  /*  9:idVendor_hi */
    (uint8_t)(USB_PRODUCTID & (uint8_t)0xff),                 /* 10:idProduct_lo */
    (uint8_t)((uint8_t)(USB_PRODUCTID >> 8) & (uint8_t)0xff), /* 11:idProduct_hi */
    (uint8_t)(USB_RELEASE & (uint8_t)0xff),                   /* 12:bcdDevice_lo */
    (uint8_t)((uint8_t)(USB_RELEASE >> 8) & (uint8_t)0xff),   /* 13:bcdDevice_hi */
    1,                                                        /* 14:iManufacturer */
    2,                                                        /* 15:iProduct */
    0,                                                        /* 16:iSerialNumber */
    USB_CONFIGNUM,                                            /* 17:bNumConfigurations */
};

/************************************************************
 * Configuration Or Other_Speed_Configuration Descriptor     *
 ************************************************************/

/* USB Configuration description - USB spec 9.6.3
 * 2nd level of USB declaration - defines power and number of interfaces
 * This config specifies that it is configuration 1 and has a single interface
 * To add usb audio we would add a second interface under this configuration
 */
uint8_t g_midi_configuration[TOTAL_CONFIG_LENGTH + (TOTAL_CONFIG_LENGTH % 2)] = {
    USB_CD_BLENGTH,                       /*  0:bLength */
    USB_DT_CONFIGURATION,                 /*  1:bDescriptorType */
    (uint8_t)(TOTAL_CONFIG_LENGTH % 256), /*  2:wTotalLength(L) */
    (uint8_t)(TOTAL_CONFIG_LENGTH / 256), /*  3:wTotalLength(H) */
    1,                                    /*  4:bNumInterfaces */
    1,                                    /*  5:bConfigurationValue */
    0,                                    /*  6:iConfiguration */
    (uint8_t)(USB_CF_RESERVED),           /*  7:bmAttributes */
    (uint8_t)(500 / 2),                   /*  8:bMaxPower (2mA unit) */

    /* Interface Descriptor
     * 3rd level of USB declarations. This declares a single USB midi interface with 2 endpoints (subclass of audio)
     */
    USB_ID_BLENGTH,   /*  0:bLength */
    USB_DT_INTERFACE, /*  1:bDescriptorType */
    0,                /*  2:bInterfaceNumber */
    0,                /*  3:bAlternateSetting */
    2,                /*  4:bNumEndpoints */
    USB_IFCLS_AUD,    /*  5:bInterfaceClass(AUD) */
    0x03,             /*  6:bInterfaceSubClass(MIDI) */
    0,                /*  7:bInterfaceProtocol */
    0,                /*  8:iInterface */

    /* Midi Streaming Interface descriptors
     * A level below interface, specific to USB midi
     * ref - https://www.usb.org/sites/default/files/midi10.pdf sect 6.1.2
     * to add ports/cables add more jacks here
     */
    // Header
    0x07,         // header length
    CS_INTERFACE, // bDescriptorType - CS interface
    0x01,         // Subtype - Midi Streaming Header
    0x00,
    0x01,                        // BCD revision (1.00)
    (uint8_t)(7 + 15 * NCABLES), // TotalLength - LSB of interface descriptors - 7+15*ncables
    0x00,                        // Interface descriptors MSB
    // MIDI_IN 1
    0x06,         // bLength
    CS_INTERFACE, // bDescriptorType
    MIDI_IN_JACK, // bDescriptorSubtype - MIDI_IN_JACK
    0x01,         // bJackType - EMBEDDED
    0x01,         // bJackID - 1
    0x00,         // iJack (unused)

    // MIDI_OUT 1
    0x09,          // bLength
    CS_INTERFACE,  // bDescriptorType - CS_I
    MIDI_OUT_JACK, // bDescriptorSubtype - MIDI_OUT_JACK
    0x01,          // bJackType - EMBEDDED
    0x02,          // bJackID - 2
    0x01,          // bNrInputPins (I can't find an explanation for what this means)
    0x01,          // BaSourceID (ditto here but I think it is asking which midi in jack is associated?)
    0x01,          // BaSourcePin (ditto)
    0x00,          // iJack (unused)

    // MIDI_IN 2
    0x06,         // bLength
    CS_INTERFACE, // bDescriptorType
    0x02,         // bDescriptorSubtype - MIDI_IN_JACK
    0x01,         // bJackType - EMBEDDED
    0x03,         // bJackID
    0x00,         // iJack (unused)

    // MIDI_OUT 2
    0x09,          // bLength
    CS_INTERFACE,  // bDescriptorType - CS_I
    MIDI_OUT_JACK, // bDescriptorSubtype - MIDI_OUT_JACK
    0x01,          // bJackType - EMBEDDED
    0x04,          // bJackID
    0x01,          // bNrInputPins (I can't find an explanation for what this means)
    0x02,          // BaSourceID (ditto here but I think it is asking which midi in jack is associated?)
    0x01,          // BaSourcePin (ditto)
    0x00,          // iJack (unused)

    // MIDI_IN 3
    0x06,         // bLength
    CS_INTERFACE, // bDescriptorType
    MIDI_IN_JACK, // bDescriptorSubtype - MIDI_IN_JACK
    0x01,         // bJackType - EMBEDDED
    0x05,         // bJackID
    0x00,         // iJack (unused)

    // MIDI_OUT 3
    0x09,          // bLength
    CS_INTERFACE,  // bDescriptorType - CS_I
    MIDI_OUT_JACK, // bDescriptorSubtype - MIDI_OUT_JACK
    0x01,          // bJackType - EMBEDDED
    0x06,          // bJackID
    0x01,          // bNrInputPins (I can't find an explanation for what this means)
    0x05,          // BaSourceID (ditto here but I think it is asking which midi in jack is associated?)
    0x01,          // BaSourcePin (ditto)
    0x00,          // iJack (unused)

    /* MidiStreaming Endpoint Descriptors - USBMidi spec 6.2.1
     * These endpoints are shared across all jacks
     */
    // 28 bytes for bulk endpoints
    // USB standard bulk out -
    0x09,                            // bLength
    0x05,                            // bDescriptorType = ENDPOINT
    (uint8_t)(USB_EP_OUT | USB_EP2), // bEndpointAddress. D7 direction, low 4 addr
    0x02,                            // bmAttributes (bulk)
    0x40,
    0x00,                   // wMaxPacketSize
    0x00,                   // bInterval
    0x00,                   // bRefresh
    0x00,                   // bSynchAddress
                            // midi class specific bulk out
    (uint8_t)(4 + NCABLES), // bLength - 4+ncables
    0x25,                   // bDescriptorType - CS_ENDPOINT
    0x01,                   // bDescriptorSubType - MS_GENERAL
    0x03,                   // bNumEmbMidiJack - number of MIDI IN jacks
    0x01,                   // BaAssocJackID - ID of first associated jack
    0x03,
    0x05, // ID of last associated jack

    // USB standard bulk in - same fields as above, differences annotated
    0x09,                           // bLength
    0x05,                           // bDescriptor
    (uint8_t)(USB_EP_IN | USB_EP1), // different address
    0x02,                           // bmAttributes
    0x40,
    0x00,                   // wMaxPacketSize
    0x00,                   // bInterval
    0x00,                   // bRefresh
    0x00,                   // bSynchAddress
                            // midi specific bulk in
    (uint8_t)(4 + NCABLES), // bLength - 4+ncables
    CS_ENDPOINT,            // bDescriptorType - CS_ENDPOINT
    0x01,                   // bDescriptorSubtype
    0x03,                   // bNumEmbMidiJack - number of MIDI OUT jacks
    0x02,                   // BaAssocJackID - first associated jack
    0x04,
    0x06, // Last associated jack
};

/*************************************
 *    String Descriptor               *
 *************************************/
uint8_t g_midi_string0[] = {
    /* UNICODE 0x0409 English (United States) */
    4,             /*  0:bLength */
    USB_DT_STRING, /*  1:bDescriptorType */
    0x09, 0x04     /*  2:wLANGID[0] */
};

uint8_t g_midi_string1[] = {
    38,            /*  0:bLength */
    USB_DT_STRING, /*  1:bDescriptorType */
    'S',
    0x00, /*  2:bString */
    'y',
    0x00,
    'n',
    0x00,
    't',
    0x00,
    'h',
    0x00,
    's',
    0x00,
    't',
    0x00,
    'r',
    0x00,
    'o',
    0x00,
    'm',
    0x00,
    ' ',
    0x00,
    'A',
    0x00,
    'u',
    0x00,
    'd',
    0x00,
    'i',
    0x00,
    'b',
    0x00,
    'l',
    0x00,
    'e',
    0x00,
};

uint8_t g_midi_string2[] = {
    14,            /*  0:bLength */
    USB_DT_STRING, /*  1:bDescriptorType */
    'D',
    0x00, /*  2:bString */
    'e',
    0x00,
    'l',
    0x00,
    'u',
    0x00,
    'g',
    0x00,
    'e',
    0x00,
};

uint8_t g_midi_string3[] = {
    8,             /*  0:bLength */
    USB_DT_STRING, /*  1:bDescriptorType */
    'O',
    0x00, /*  2:bString */
    'U',
    0x00,
    'T',
    0x00,
};

uint8_t g_midi_string4[] = {
    6,             /*  0:bLength */
    USB_DT_STRING, /*  1:bDescriptorType */
    'I',
    0x00, /*  2:bString */
    'N',
    0x00,
};

uint8_t* g_midi_string_table[] = {g_midi_string0, g_midi_string1, g_midi_string2, g_midi_string3, g_midi_string4};

/***********************************************************************************************************************
Private global variables and functions
***********************************************************************************************************************/
