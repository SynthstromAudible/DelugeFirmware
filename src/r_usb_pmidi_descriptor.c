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
 */

/***********************************************************************************************************************
Includes   <System Includes> , "Project Includes"
***********************************************************************************************************************/
#include "r_usb_basic_if.h"

/***********************************************************************************************************************
Macro definitions
***********************************************************************************************************************/
#define USB_BCDNUM (0x0200u)  /* bcdUSB */
#define USB_RELEASE (0x0200u) /* Release Number */
#define USB_CONFIGNUM (1u)    /* Configuration number */
#define USB_DCPMAXP (64u)     /* DCP max packet size */

// Get VID from http://www.mcselec.com/index.php?page=shop.product_details&flypage=shop.flypage&product_id=92&category_id=20&option=com_phpshop&Itemid=1
#define USB_VENDORID (0x16D0)  /* Vendor ID */
#define USB_PRODUCTID (0x0CE2) /* Product ID */

#define USB_MIDI_CD_WTOTALLENGTH (68u)

/***********************************************************************************************************************
Exported global variables (to be accessed by other files)
***********************************************************************************************************************/
/* Standard Device Descriptor */
uint8_t g_midi_device[USB_DD_BLENGTH + (USB_DD_BLENGTH % 2)] = {
    USB_DD_BLENGTH,                                           /*  0:bLength */
    USB_DT_DEVICE,                                            /*  1:bDescriptorType */
    (uint8_t)(USB_BCDNUM&(uint8_t)0xff),                      /*  2:bcdUSB_lo */
    (uint8_t)((uint8_t)(USB_BCDNUM >> 8) & (uint8_t)0xff),    /*  3:bcdUSB_hi */
    0x00,                                                     /*  4:bDeviceClass */
    0x00,                                                     /*  5:bDeviceSubClass */
    0x00,                                                     /*  6:bDeviceProtocol */
    (uint8_t)USB_DCPMAXP,                                     /*  7:bMAXPacketSize(for DCP) */
    (uint8_t)(USB_VENDORID&(uint8_t)0xff),                    /*  8:idVendor_lo */
    (uint8_t)((uint8_t)(USB_VENDORID >> 8) & (uint8_t)0xff),  /*  9:idVendor_hi */
    (uint8_t)(USB_PRODUCTID&(uint8_t)0xff),                   /* 10:idProduct_lo */
    (uint8_t)((uint8_t)(USB_PRODUCTID >> 8) & (uint8_t)0xff), /* 11:idProduct_hi */
    (uint8_t)(USB_RELEASE&(uint8_t)0xff),                     /* 12:bcdDevice_lo */
    (uint8_t)((uint8_t)(USB_RELEASE >> 8) & (uint8_t)0xff),   /* 13:bcdDevice_hi */
    1,                                                        /* 14:iManufacturer */
    2,                                                        /* 15:iProduct */
    0,                                                        /* 16:iSerialNumber */
    USB_CONFIGNUM,                                            /* 17:bNumConfigurations */
};

/************************************************************
* Configuration Or Other_Speed_Configuration Descriptor     *
************************************************************/

uint8_t g_midi_configuration[USB_MIDI_CD_WTOTALLENGTH + (USB_MIDI_CD_WTOTALLENGTH % 2)] = {
    USB_CD_BLENGTH,                            /*  0:bLength */
    USB_DT_CONFIGURATION,                      /*  1:bDescriptorType */
    (uint8_t)(USB_MIDI_CD_WTOTALLENGTH % 256), /*  2:wTotalLength(L) */
    (uint8_t)(USB_MIDI_CD_WTOTALLENGTH / 256), /*  3:wTotalLength(H) */
    1,                                         /*  4:bNumInterfaces */
    1,                                         /*  5:bConfigurationValue */
    0,                                         /*  6:iConfiguration */
    (uint8_t)(USB_CF_RESERVED),                /*  7:bmAttributes */
    (uint8_t)(500 / 2),                        /*  8:bMaxPower (2mA unit) */

    /* Interface Descriptor */
    USB_ID_BLENGTH,   /*  0:bLength */
    USB_DT_INTERFACE, /*  1:bDescriptorType */
    0,                /*  2:bInterfaceNumber */
    0,                /*  3:bAlternateSetting */
    2,                /*  4:bNumEndpoints */
    USB_IFCLS_AUD,    /*  5:bInterfaceClass(AUD) */
    0x03,             /*  6:bInterfaceSubClass(MIDI) */
    0,                /*  7:bInterfaceProtocol */
    0,                /*  8:iInterface */

    0x07, 0x24, 0x01, 0x00, 0x01, 50, 0x00,                                          // CS Interface (midi)
    0x06, 0x24, 0x02, 0x01, 0x01, 0x03,                                              //   IN  Jack 1 (emb)
    0x09, 0x24, 0x03, 0x01, 0x02, 0x01, 0x02, 0x01, 0x04,                            //   OUT Jack 3 (emb)
    0x09, 0x05, (uint8_t)(USB_EP_OUT | USB_EP2), 0x02, 0x40, 0x00, 0x00, 0x00, 0x00, // Endpoint OUT
    0x05, 0x25, 0x01, 0x01, 0x01,                                                    //   CS EP IN  Jack
    0x09, 0x05, (uint8_t)(USB_EP_IN | USB_EP1), 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,  // Endpoint IN
    0x05, 0x25, 0x01, 0x01, 0x02                                                     //   CS EP OUT Jack
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
