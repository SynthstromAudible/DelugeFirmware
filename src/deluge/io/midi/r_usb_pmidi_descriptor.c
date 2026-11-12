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
#include "deluge/drivers/usb/userdef/r_usb_paudio_config.h"
#include <stdbool.h>
#include <string.h>

/***********************************************************************************************************************
Macro definitions
***********************************************************************************************************************/
#define USB_BCDNUM (0x0200u) /* bcdUSB */
// Release Number. Bumped when USB audio was added, so hosts don't reuse anything they cached about the MIDI-only
// configuration, then each time the audio streams' descriptors changed: when the out stream went from adaptive to
// asynchronous with implicit feedback, and when it got its adaptive alternate setting back as well
#define USB_RELEASE (0x0302u)
// The multitrack configuration has its own, so a host doesn't take anything it cached about the stereo one for it, or
// the other way round, when the user switches between them
#define USB_RELEASE_MULTITRACK (0x0310u)
// With USB audio turned off we describe ourselves exactly as before USB audio existed
#define USB_RELEASE_MIDI_ONLY (0x0200u)
#define USB_CONFIGNUM (1u) /* Configuration number */
#define USB_DCPMAXP (64u)  /* DCP max packet size */

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

/* USB audio (UAC1), described in audio10.pdf and frmts10.pdf from usb.org
 * Audio control: interface descriptor, then the class-specific header (8 + 1 per streaming interface), then an input
 * and an output terminal for each direction
 */
#define USB_AUDIO_AC_CS_LENGTH (8 + 3 + (12 + 9) * 2)
#define USB_AUDIO_AC_LENGTH (9 + USB_AUDIO_AC_CS_LENGTH)
/* Each streaming interface: alternate setting 0 (no endpoint, so no bandwidth when not streaming), then for each
 * streaming alternate setting: interface descriptor, general descriptor, format type descriptor (with one sample
 * rate), standard and class-specific endpoint. The out stream has two of those, the in stream one
 */
#define USB_AUDIO_AS_ALT_LENGTH (9 + 7 + 11 + 9 + 7)
#define USB_AUDIO_AS_OUT_LENGTH (9 + USB_AUDIO_AS_ALT_LENGTH * 2)
#define USB_AUDIO_AS_IN_LENGTH (9 + USB_AUDIO_AS_ALT_LENGTH)

// Where the MIDI streaming interface starts in g_midi_configuration - everything from there on is the same in the
// MIDI-only configuration
#define MIDI_SECTION_OFFSET (9 + USB_AUDIO_AC_LENGTH + USB_AUDIO_AS_OUT_LENGTH + USB_AUDIO_AS_IN_LENGTH)
// 9 for config descriptor. Add any additional config lengths here
#define TOTAL_CONFIG_LENGTH (MIDI_SECTION_OFFSET + USB_MIDI_CD_WTOTALLENGTH)
#define MIDI_ONLY_CONFIG_LENGTH (9 + USB_MIDI_CD_WTOTALLENGTH)
// Good summary ref on overall USB structure https://www.beyondlogic.org/usbnutshell/usb5.shtml

// USB midi defines
#define CS_INTERFACE 0x24
#define CS_ENDPOINT 0x25
#define MIDI_IN_JACK 0x02
#define MIDI_OUT_JACK 0x03

// USB audio defines
#define AUDIO_SUBCLASS_CONTROL 0x01
#define AUDIO_SUBCLASS_STREAMING 0x02
#define AUDIO_AC_HEADER 0x01
#define AUDIO_AC_INPUT_TERMINAL 0x02
#define AUDIO_AC_OUTPUT_TERMINAL 0x03
#define AUDIO_AS_GENERAL 0x01
#define AUDIO_AS_FORMAT_TYPE 0x02
#define AUDIO_EP_GENERAL 0x01
#define AUDIO_TERMINAL_STREAMING 0x0101 // USB streaming
#define AUDIO_TERMINAL_LINE 0x0603      // Line connector
// Isochronous, asynchronous: our sample clock sets the rate. Both directions use it - we send the host 44 or 45 frames
// each USB frame to match how fast we produce them, and the host sends us as many as we send it
#define AUDIO_ISO_ASYNC 0x05
// Isochronous, adaptive: the host sends at its own clock's rate, and we follow it. For hosts that can't do implicit
// feedback - Windows' class drivers only support explicit feedback, which would need a third isochronous pipe we don't
// have. Alternate setting 2 of the out stream
#define AUDIO_ISO_ADAPTIVE 0x09
// USB 2.0 usage type (bits 5:4) for our data in endpoint: the host can work out our rate from how many frames it gets
// each USB frame, and use that for its out stream too. There's no room for an explicit feedback endpoint - only pipes 1
// and 2 can do isochronous transfers, and the two streams already have them
#define AUDIO_ISO_USAGE_IMPLICIT_FEEDBACK 0x20

// The streams' channels, as spatial locations (audio10.pdf 3.7.2.3). Stereo is left and right front. Multitrack's 8
// channels to the host are really four stereo pairs, but described as 7.1 - left, right, centre, LFE, left and right
// surround, left and right side - they're an ordinary 8-channel device to every host. Its 4 from the host, two stereo
// pairs, are quad - left and right front, then left and right surround. Windows, in particular, only shows the channels
// of a device whose channels all have locations. Channels arrive in the order of these bits
#define AUDIO_CHANNEL_CONFIG_STEREO 0x0003
#define AUDIO_CHANNEL_CONFIG_QUAD 0x0033
#define AUDIO_CHANNEL_CONFIG_7_1 0x063F

// Terminal IDs
#define AUDIO_TERMINAL_FROM_HOST 1
#define AUDIO_TERMINAL_OUTPUTS 2
#define AUDIO_TERMINAL_INPUTS 3
#define AUDIO_TERMINAL_TO_HOST 4

#define LO(x) ((uint8_t)((x) & 0xFF))
#define HI(x) ((uint8_t)(((x) >> 8) & 0xFF))

/* The USB audio function: the audio control interface, then the two audio streaming interfaces. The same layout for
 * both the stereo and multitrack configurations - only the formats and packet sizes differ, so they share endpoints,
 * pipes and all of the driver's code. audio10.pdf and frmts10.pdf from usb.org describe it all.
 *
 * fromHostChannels / fromHostChannelConfig / fromHostSubframe / fromHostBits: the out stream's format (host to Deluge)
 * toHostChannels / toHostChannelConfig / toHostSubframe / toHostBits: the in stream's format (Deluge to host)
 * asyncMaxPacket / adaptiveMaxPacket: the out stream's max packet sizes for its two streaming alternate settings
 * toHostMaxPacket: the in stream's max packet size
 *
 * The out stream's alternate setting 1 is asynchronous, taking its timing from our in stream through implicit feedback
 * (macOS, iOS, Linux). Alternate setting 2 is adaptive, for hosts that can't do implicit feedback - Windows' class
 * drivers only support explicit feedback, which would need a third isochronous pipe we don't have */
// clang-format off
#define AUDIO_FUNCTION_DESCRIPTORS(fromHostChannels, fromHostChannelConfig, fromHostSubframe, fromHostBits,             \
                                   toHostChannels, toHostChannelConfig, toHostSubframe, toHostBits, asyncMaxPacket,    \
                                   adaptiveMaxPacket, toHostMaxPacket)                                                 \
	/* Audio control interface - no endpoints, just describes how the streams connect up */                           \
	USB_ID_BLENGTH, USB_DT_INTERFACE, USB_PAUDIO_INTERFACE_CONTROL, 0 /* bAlternateSetting */,                        \
	    0 /* bNumEndpoints */, USB_IFCLS_AUD, AUDIO_SUBCLASS_CONTROL, 0, 0,                                            \
	/* Class-specific header - audio10.pdf 4.3.2. Its collection is the two audio streams and MIDI */                  \
	11, CS_INTERFACE, AUDIO_AC_HEADER, 0x00, 0x01 /* bcdADC 1.00 */, LO(USB_AUDIO_AC_CS_LENGTH),                       \
	    HI(USB_AUDIO_AC_CS_LENGTH), 3 /* bInCollection */, USB_PAUDIO_INTERFACE_STREAM_OUT,                            \
	    USB_PAUDIO_INTERFACE_STREAM_IN, USB_PMIDI_INTERFACE,                                                           \
	/* Host to Deluge - a stream in from USB, going to the outputs. audio10.pdf 4.3.2.1 and 4.3.2.2 */                 \
	12, CS_INTERFACE, AUDIO_AC_INPUT_TERMINAL, AUDIO_TERMINAL_FROM_HOST, LO(AUDIO_TERMINAL_STREAMING),                 \
	    HI(AUDIO_TERMINAL_STREAMING), 0 /* bAssocTerminal */, (fromHostChannels), LO(fromHostChannelConfig),           \
	    HI(fromHostChannelConfig), 0 /* iChannelNames */, 0 /* iTerminal */,                                           \
	9, CS_INTERFACE, AUDIO_AC_OUTPUT_TERMINAL, AUDIO_TERMINAL_OUTPUTS, LO(AUDIO_TERMINAL_LINE),                        \
	    HI(AUDIO_TERMINAL_LINE), 0 /* bAssocTerminal */, AUDIO_TERMINAL_FROM_HOST /* bSourceID */, 0 /* iTerminal */,  \
	/* Deluge to host */                                                                                               \
	12, CS_INTERFACE, AUDIO_AC_INPUT_TERMINAL, AUDIO_TERMINAL_INPUTS, LO(AUDIO_TERMINAL_LINE),                         \
	    HI(AUDIO_TERMINAL_LINE), 0 /* bAssocTerminal */, (toHostChannels), LO(toHostChannelConfig),                    \
	    HI(toHostChannelConfig), 0 /* iChannelNames */, 0 /* iTerminal */,                                             \
	9, CS_INTERFACE, AUDIO_AC_OUTPUT_TERMINAL, AUDIO_TERMINAL_TO_HOST, LO(AUDIO_TERMINAL_STREAMING),                   \
	    HI(AUDIO_TERMINAL_STREAMING), 0 /* bAssocTerminal */, AUDIO_TERMINAL_INPUTS /* bSourceID */, 0 /* iTerminal */,\
	                                                                                                                   \
	/* Audio streaming, host to Deluge. Alternate setting 0 has no endpoint, so takes no bus bandwidth */              \
	USB_ID_BLENGTH, USB_DT_INTERFACE, USB_PAUDIO_INTERFACE_STREAM_OUT, 0 /* bAlternateSetting */,                      \
	    0 /* bNumEndpoints */, USB_IFCLS_AUD, AUDIO_SUBCLASS_STREAMING, 0, 0,                                          \
	/* Alternate setting 1: asynchronous, following our clock through implicit feedback */                             \
	USB_ID_BLENGTH, USB_DT_INTERFACE, USB_PAUDIO_INTERFACE_STREAM_OUT, 1 /* bAlternateSetting */,                      \
	    1 /* bNumEndpoints */, USB_IFCLS_AUD, AUDIO_SUBCLASS_STREAMING, 0, 0,                                          \
	AUDIO_AS_DESCRIPTORS(AUDIO_TERMINAL_FROM_HOST, fromHostChannels, fromHostSubframe, fromHostBits),                  \
	/* Standard isochronous endpoint - audio10.pdf 4.6.1.1. bSynchAddress: implicit feedback from our in stream */     \
	9, USB_DT_ENDPOINT, (uint8_t)(USB_EP_OUT | USB_PAUDIO_EP_OUT), AUDIO_ISO_ASYNC, LO(asyncMaxPacket),                \
	    HI(asyncMaxPacket), 1 /* bInterval */, 0 /* bRefresh */, (uint8_t)(USB_EP_IN | USB_PAUDIO_EP_IN),              \
	AUDIO_CS_ENDPOINT_DESCRIPTOR,                                                                                      \
	/* Alternate setting 2: the same stream, adaptive, for hosts that can't follow our clock through implicit          \
	 * feedback. The Deluge then follows theirs, dropping or repeating the odd frame */                                \
	USB_ID_BLENGTH, USB_DT_INTERFACE, USB_PAUDIO_INTERFACE_STREAM_OUT, 2 /* bAlternateSetting */,                      \
	    1 /* bNumEndpoints */, USB_IFCLS_AUD, AUDIO_SUBCLASS_STREAMING, 0, 0,                                          \
	AUDIO_AS_DESCRIPTORS(AUDIO_TERMINAL_FROM_HOST, fromHostChannels, fromHostSubframe, fromHostBits),                  \
	9, USB_DT_ENDPOINT, (uint8_t)(USB_EP_OUT | USB_PAUDIO_EP_OUT), AUDIO_ISO_ADAPTIVE, LO(adaptiveMaxPacket),           \
	    HI(adaptiveMaxPacket), 1 /* bInterval */, 0 /* bRefresh */, 0 /* bSynchAddress - none */,                      \
	AUDIO_CS_ENDPOINT_DESCRIPTOR,                                                                                      \
	                                                                                                                   \
	/* Audio streaming, Deluge to host */                                                                              \
	USB_ID_BLENGTH, USB_DT_INTERFACE, USB_PAUDIO_INTERFACE_STREAM_IN, 0 /* bAlternateSetting */,                       \
	    0 /* bNumEndpoints */, USB_IFCLS_AUD, AUDIO_SUBCLASS_STREAMING, 0, 0,                                          \
	USB_ID_BLENGTH, USB_DT_INTERFACE, USB_PAUDIO_INTERFACE_STREAM_IN, 1 /* bAlternateSetting */,                       \
	    1 /* bNumEndpoints */, USB_IFCLS_AUD, AUDIO_SUBCLASS_STREAMING, 0, 0,                                          \
	AUDIO_AS_DESCRIPTORS(AUDIO_TERMINAL_TO_HOST, toHostChannels, toHostSubframe, toHostBits),                          \
	/* Asynchronous, and the source of implicit feedback for the out stream */                                         \
	9, USB_DT_ENDPOINT, (uint8_t)(USB_EP_IN | USB_PAUDIO_EP_IN), AUDIO_ISO_ASYNC | AUDIO_ISO_USAGE_IMPLICIT_FEEDBACK,   \
	    LO(toHostMaxPacket), HI(toHostMaxPacket), 1 /* bInterval */, 0 /* bRefresh */, 0 /* bSynchAddress */,          \
	AUDIO_CS_ENDPOINT_DESCRIPTOR

/* A streaming alternate setting's general descriptor (audio10.pdf 4.5.2) and Type I format descriptor (frmts10.pdf
 * 2.2.5), with our one sample rate */
#define AUDIO_AS_DESCRIPTORS(terminal, channels, subframe, bits)                                                       \
	7, CS_INTERFACE, AUDIO_AS_GENERAL, (terminal), 1 /* bDelay */, 0x01, 0x00 /* wFormatTag - PCM */,                 \
	11, CS_INTERFACE, AUDIO_AS_FORMAT_TYPE, 1 /* type I */, (channels), (subframe), (bits),                            \
	    1 /* bSamFreqType - one discrete rate */, LO(USB_PAUDIO_SAMPLE_RATE), HI(USB_PAUDIO_SAMPLE_RATE),              \
	    (uint8_t)(USB_PAUDIO_SAMPLE_RATE >> 16)

/* Class-specific isochronous endpoint - audio10.pdf 4.6.1.2. No sample rate control - we only do one */
#define AUDIO_CS_ENDPOINT_DESCRIPTOR 7, CS_ENDPOINT, AUDIO_EP_GENERAL, 0 /* bmAttributes */, 0, 0, 0
// clang-format on

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
    (uint8_t)(USB_BCDNUM&(uint8_t)0xff),                   /*  2:bcdUSB_lo */
    (uint8_t)((uint8_t)(USB_BCDNUM >> 8) & (uint8_t)0xff), /*  3:bcdUSB_hi */
                                                           // Device to be specified at interface level
    0x00,                                                  /*  4:bDeviceClass */
    0x00,                                                  /*  5:bDeviceSubClass */
    0x00,                                                  /*  6:bDeviceProtocol */

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

/* USB Configuration description - USB spec 9.6.3
 * 2nd level of USB declaration - defines power and number of interfaces
 * This config specifies that it is configuration 1, holding one USB audio function: an audio control interface, which
 * lists the two audio streaming interfaces and the MIDI streaming interface as its collection.
 * Endpoints are matched to pipes in the order they appear here - see g_usb_pstd_eptbl
 */
uint8_t g_midi_configuration[TOTAL_CONFIG_LENGTH + (TOTAL_CONFIG_LENGTH % 2)] = {
    USB_CD_BLENGTH,                       /*  0:bLength */
    USB_DT_CONFIGURATION,                 /*  1:bDescriptorType */
    (uint8_t)(TOTAL_CONFIG_LENGTH % 256), /*  2:wTotalLength(L) */
    (uint8_t)(TOTAL_CONFIG_LENGTH / 256), /*  3:wTotalLength(H) */
    USB_PAUDIO_NUM_INTERFACES,            /*  4:bNumInterfaces */
    1,                                    /*  5:bConfigurationValue */
    0,                                    /*  6:iConfiguration */
    (uint8_t)(USB_CF_RESERVED),           /*  7:bmAttributes */
    (uint8_t)(500 / 2),                   /*  8:bMaxPower (2mA unit) */

    AUDIO_FUNCTION_DESCRIPTORS(USB_PAUDIO_NUM_CHANNELS, AUDIO_CHANNEL_CONFIG_STEREO, USB_PAUDIO_SUBFRAME_BYTES,
                               USB_PAUDIO_BIT_RESOLUTION, USB_PAUDIO_NUM_CHANNELS, AUDIO_CHANNEL_CONFIG_STEREO,
                               USB_PAUDIO_SUBFRAME_BYTES, USB_PAUDIO_BIT_RESOLUTION, USB_PAUDIO_MAX_PACKET_BYTES,
                               USB_PAUDIO_ADAPTIVE_MAX_PACKET_BYTES, USB_PAUDIO_MAX_PACKET_BYTES),

    /* MIDI streaming interface Descriptor
     * 3rd level of USB declarations. This declares a USB midi interface with 2 endpoints (subclass of audio)
     */
    USB_ID_BLENGTH,      /*  0:bLength */
    USB_DT_INTERFACE,    /*  1:bDescriptorType */
    USB_PMIDI_INTERFACE, /*  2:bInterfaceNumber */
    0,                   /*  3:bAlternateSetting */
    2,                   /*  4:bNumEndpoints */
    USB_IFCLS_AUD,       /*  5:bInterfaceClass(AUD) */
    0x03,                /*  6:bInterfaceSubClass(MIDI) */
    0,                   /*  7:bInterfaceProtocol */
    0,                   /*  8:iInterface */

    /* Midi Streaming Interface descriptors
     * A level below interface, specific to USB midi
     * ref - https://www.usb.org/sites/default/files/midi10.pdf sect 6.1.2
     * to add ports/cables add more jacks here
     */
    // Header
    0x07,                        // header length
    CS_INTERFACE,                // bDescriptorType - CS interface
    0x01,                        // Subtype - Midi Streaming Header
    0x00, 0x01,                  // BCD revision (1.00)
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
    0x40, 0x00,                      // wMaxPacketSize
    0x00,                            // bInterval
    0x00,                            // bRefresh
    0x00,                            // bSynchAddress
                                     // midi class specific bulk out
    (uint8_t)(4 + NCABLES),          // bLength - 4+ncables
    0x25,                            // bDescriptorType - CS_ENDPOINT
    0x01,                            // bDescriptorSubType - MS_GENERAL
    0x03,                            // bNumEmbMidiJack - number of MIDI IN jacks
    0x01,                            // BaAssocJackID - ID of first associated jack
    0x03,
    0x05, // ID of last associated jack

    // USB standard bulk in - same fields as above, differences annotated
    0x09,                           // bLength
    0x05,                           // bDescriptor
    (uint8_t)(USB_EP_IN | USB_EP1), // different address
    0x02,                           // bmAttributes
    0x40, 0x00,                     // wMaxPacketSize
    0x00,                           // bInterval
    0x00,                           // bRefresh
    0x00,                           // bSynchAddress
                                    // midi specific bulk in
    (uint8_t)(4 + NCABLES),         // bLength - 4+ncables
    CS_ENDPOINT,                    // bDescriptorType - CS_ENDPOINT
    0x01,                           // bDescriptorSubtype
    0x03,                           // bNumEmbMidiJack - number of MIDI OUT jacks
    0x02,                           // BaAssocJackID - first associated jack
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

// usb_pstd_get_descriptor1() will look up any index below 7, so fill them all
uint8_t* g_midi_string_table[] = {g_midi_string0, g_midi_string1, g_midi_string2, g_midi_string3,
                                  g_midi_string4, g_midi_string2, g_midi_string2};

/* The configuration with USB audio turned off: just the MIDI streaming interface, as before USB audio existed. Built
 * from the MIDI section of g_midi_configuration, so there's only one copy of the MIDI descriptors to maintain */
static uint8_t g_midi_only_configuration[MIDI_ONLY_CONFIG_LENGTH + (MIDI_ONLY_CONFIG_LENGTH % 2)];

/* The multitrack configuration's audio function: 8 channels to the host and 4 from it, all 16-bit. See
 * r_usb_paudio_config.h for why */
static const uint8_t g_multitrack_audio_function[] = {AUDIO_FUNCTION_DESCRIPTORS(
    USB_PAUDIO_MT_CHANNELS_FROM_HOST, AUDIO_CHANNEL_CONFIG_QUAD, USB_PAUDIO_MT_SUBFRAME_BYTES,
    USB_PAUDIO_MT_BIT_RESOLUTION, USB_PAUDIO_MT_CHANNELS_TO_HOST, AUDIO_CHANNEL_CONFIG_7_1,
    USB_PAUDIO_MT_SUBFRAME_BYTES, USB_PAUDIO_MT_BIT_RESOLUTION, USB_PAUDIO_MT_MAX_PACKET_BYTES_FROM_HOST,
    USB_PAUDIO_MT_ADAPTIVE_MAX_PACKET_BYTES_FROM_HOST, USB_PAUDIO_MT_MAX_PACKET_BYTES_TO_HOST)};
_Static_assert(sizeof(g_multitrack_audio_function) == MIDI_SECTION_OFFSET - USB_CD_BLENGTH,
               "The multitrack audio function must be laid out like the stereo one");

/* The multitrack configuration: the stereo one with the multitrack audio function swapped in. Only built if it's used
 */
static uint8_t g_multitrack_configuration[TOTAL_CONFIG_LENGTH + (TOTAL_CONFIG_LENGTH % 2)];

uint8_t* usbPeripheralConfigurationDescriptor(uint8_t which) {
	uint16_t release = (which == USB_PAUDIO_CONFIG_MULTITRACK) ? USB_RELEASE_MULTITRACK
	                   : (which == USB_PAUDIO_CONFIG_STEREO)   ? USB_RELEASE
	                                                           : USB_RELEASE_MIDI_ONLY;
	g_midi_device[12] = LO(release); /* bcdDevice */
	g_midi_device[13] = HI(release);

	if (which == USB_PAUDIO_CONFIG_STEREO) {
		return g_midi_configuration;
	}

	if (which == USB_PAUDIO_CONFIG_MULTITRACK) {
		memcpy(g_multitrack_configuration, g_midi_configuration, sizeof(g_multitrack_configuration));
		memcpy(&g_multitrack_configuration[USB_CD_BLENGTH], g_multitrack_audio_function,
		       sizeof(g_multitrack_audio_function));
		return g_multitrack_configuration;
	}

	memcpy(g_midi_only_configuration, g_midi_configuration, USB_CD_BLENGTH);
	g_midi_only_configuration[2] = LO(MIDI_ONLY_CONFIG_LENGTH); /* wTotalLength */
	g_midi_only_configuration[3] = HI(MIDI_ONLY_CONFIG_LENGTH);
	g_midi_only_configuration[4] = 1; /* bNumInterfaces */
	memcpy(&g_midi_only_configuration[USB_CD_BLENGTH], &g_midi_configuration[MIDI_SECTION_OFFSET],
	       USB_MIDI_CD_WTOTALLENGTH);
	g_midi_only_configuration[USB_CD_BLENGTH + 2] = 0; /* The MIDI interface's bInterfaceNumber - it's the only one */
	return g_midi_only_configuration;
}

/***********************************************************************************************************************
Private global variables and functions
***********************************************************************************************************************/
