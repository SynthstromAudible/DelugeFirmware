/***********************************************************************************************************************
 * DISCLAIMER
 * This software is supplied by Renesas Electronics Corporation and is only intended for use with Renesas products. No
 * other uses are authorized. This software is owned by Renesas Electronics Corporation and is protected under all
 * applicable laws, including copyright laws.
 * THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
 * THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED. TO THE MAXIMUM
 * EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES
 * SHALL BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR ANY REASON RELATED TO THIS
 * SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * Renesas reserves the right, without notice, to make changes to this software and to discontinue the availability of
 * this software. By using this software, you agree to the additional terms and conditions found by accessing the
 * following link:
 * http://www.renesas.com/disclaimer
 *
 * Copyright (C) 2014(2015) Renesas Electronics Corporation. All rights reserved.
 ***********************************************************************************************************************/
/***********************************************************************************************************************
 * File Name    : r_usb_hhid_driver.c
 * Description  : Host HID class driver code
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 04.01.2014 1.00 First Release
 *         : 30.01.2015 1.01    Support Multi device.
 ***********************************************************************************************************************/

/******************************************************************************
 Includes   <System Includes> , "Project Includes"
 ******************************************************************************/

#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_extern.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_typedef.h"
#include "RZA1/usb/r_usb_hmidi/r_usb_hmidi_if.h"
#include "RZA1/usb/r_usb_hmidi/src/inc/r_usb_hmidi.h"
#include "RZA1/usb/userdef/r_usb_hmidi_config.h"
#include "definitions.h"

#include "deluge/deluge.h"
#include "deluge/drivers/uart/uart.h"

/******************************************************************************
 Exported global variables
 ******************************************************************************/

/******************************************************************************
 Private global variables and functions
 ******************************************************************************/
/* variables */
static uint16_t usb_shmidi_class_request_setup[USB_NUM_USBIP][5]; /* Control Transfer Request field */
static usb_utr_t usb_shmidi_string_req[USB_NUM_USBIP];            /* Request for String discriptor */

/* functions */
static uint16_t usb_hmidi_cmd_submit(usb_utr_t* ptr, usb_cb_t complete);
static void usb_hhid_in_transfer_result(usb_utr_t* mess, uint16_t data1, uint16_t data2);
static usb_er_t usb_hhid_class_request_process(usb_utr_t* ptr);
static void usb_hhid_class_request_trans_result(usb_utr_t* mess, uint16_t data1, uint16_t data2);
static void usb_hmidi_check_result(usb_utr_t* ptr, uint16_t unused, uint16_t status);
static usb_er_t usb_hhid_data_trans(usb_utr_t* ptr, uint16_t pipe, uint32_t size, uint8_t* table, usb_cb_t complete);
static void usb_hmidi_enumeration_sequence(usb_utr_t* mess);

extern uint8_t currentDeviceNumWithSendPipe[];

/******************************************************************************
 Exported global variables (to be accessed by other files)
 ******************************************************************************/
/* Host HID TPL */
const uint16_t g_usb_hmidi_devicetpl[] = {
    USB_CFG_TPLCNT, /* Number of tpl table */
    0,              /* Reserved */
    USB_CFG_TPL     /* Vendor ID, Product ID */
};

/* Host HID Pipe Information Table (or "endpoint table"). */
uint16_t g_usb_hmidi_tmp_ep_tbl[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES][(USB_EPL * 2) + 1] = {
    {
        /* USB IP 0 */
        {
            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            /* Pipe end */
            USB_PDTBLEND,
        },
#if MAX_NUM_USB_MIDI_DEVICES >= 2
        {
            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            /* Pipe end */
            USB_PDTBLEND,
        },
#endif /* MAX_NUM_USB_MIDI_DEVICES >= 2 */
#if MAX_NUM_USB_MIDI_DEVICES >= 3
        {
            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            /* Pipe end */
            USB_PDTBLEND,
        },
#endif /* MAX_NUM_USB_MIDI_DEVICES >= 4 */
#if MAX_NUM_USB_MIDI_DEVICES >= 4
        {
            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            /* Pipe end */
            USB_PDTBLEND,
        },
#endif /* MAX_NUM_USB_MIDI_DEVICES >= 4 */
#if MAX_NUM_USB_MIDI_DEVICES >= 5
        {
            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            /* Pipe end */
            USB_PDTBLEND,
        },
#endif /* MAX_NUM_USB_MIDI_DEVICES >= 5 */
#if MAX_NUM_USB_MIDI_DEVICES >= 6
        {
            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            /* Pipe end */
            USB_PDTBLEND,
        },
#endif /* MAX_NUM_USB_MIDI_DEVICES >= 6 */
    },
#if USB_NUM_USBIP >= 2
    {/* USB IP 1 */
        {
            USB_CFG_HMIDI_BULK_SEND, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            USB_CFG_HMIDI_BULK_RECV1, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            /* Pipe end */
            USB_PDTBLEND,
        },
#if MAX_NUM_USB_MIDI_DEVICES >= 2
        {
            USB_CFG_HMIDI_BULK_IN2, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            /* Pipe end */
            USB_PDTBLEND,
        },
#endif /* MAX_NUM_USB_MIDI_DEVICES >= 2 */
#if MAX_NUM_USB_MIDI_DEVICES == 3
        {
            USB_CFG_HMIDI_BULK_IN3, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            USB_NULL, /* Pipe No. */
            /* TYPE  / DIR      / EPNUM */
            USB_NULL | USB_NULL | USB_NULL, /* PIPECFG */
            USB_NULL,                       /* rsv */
            USB_NULL,                       /* PIPEMAXP */
            USB_NULL,                       /* PIPEPERI */
            USB_NULL,                       /* rsv */

            /* Pipe end */
            USB_PDTBLEND,
        }
#endif /* MAX_NUM_USB_MIDI_DEVICES == 3 */
    }
#endif /* USB_NUM_USBIP >= 2 */
};

// These seem to all store temporary data for while the device is being set up.
uint8_t g_usb_hmidi_str_desc_data[USB_NUM_USBIP][USB_HMIDI_CLSDATASIZE];
uint16_t g_usb_hmidi_devaddr[USB_NUM_USBIP];           /* Device Address */
uint16_t g_usb_hmidi_enum_seq[USB_NUM_USBIP];          /* Enumeration Sequence */
uint16_t g_usb_hmidi_speed[USB_NUM_USBIP];             /* Device speed */
uint16_t* g_p_usb_hmidi_pipe_table[USB_NUM_USBIP];     /* Pipe Table(DefEP) */
uint8_t* g_p_usb_hmidi_config_table[USB_NUM_USBIP];    /* Configuration Descriptor Table */
uint8_t* g_p_usb_hmidi_device_table[USB_NUM_USBIP];    /* Device Descriptor Table */
uint8_t* g_p_usb_hmidi_interface_table[USB_NUM_USBIP]; /* Interface Descriptor Table */

/******************************************************************************
 Renesas Abstracted USB Driver functions
 ******************************************************************************/

/******************************************************************************
 Function Name   : usb_hhid_check_result
 Description     : String Descriptor receive complete callback.
 Argument        : usb_utr_t    *ptr         : USB internal structure. Selects USB channel.
                 : uint16_t     unused       : Not use
                 : uint16_t     status       : Not use
 Return          : none
 ******************************************************************************/
static void usb_hmidi_check_result(usb_utr_t* ptr, uint16_t unused, uint16_t status)
{
    usb_mh_t p_blf;
    usb_er_t err;
    usb_clsinfo_t* cp;

    /* Get mem pool blk */
    err = USB_PGET_BLK(USB_HMIDI_MPL, &p_blf);
    if (USB_OK == err)
    {
        cp          = (usb_clsinfo_t*)p_blf;
        cp->msghead = (usb_mh_t)USB_NULL;
        cp->msginfo = USB_HHID_TCMD_OPEN;
        cp->ip      = ptr->ip;
        cp->ipp     = ptr->ipp;

        /* Send message */
        err = USB_SND_MSG(USB_HMIDI_MBX, (usb_msg_t*)cp);
        if (USB_OK != err)
        {
            /* error */
            err = USB_REL_BLK(USB_HMIDI_MPL, (usb_mh_t)p_blf);
            USB_PRINTF0("### usb_open_hstd function snd_msg error\n");
        }
    }
    else
    {
        /* error */
        USB_PRINTF0("### usb_open_hstd function pget_blk error\n");
    }
} /* End of function usb_hhid_check_result() */

/* Condition compilation by the difference of the operating system */

#define USB_DEBUGPRINT_PP

void giveDetailsOfDeviceBeingSetUp(int ip, char const* name, uint16_t vendorId, uint16_t productId);

/******************************************************************************
 Function Name   : usb_hhid_enumeration_sequence
 Description     : Enumeration( Get String Descriptor ) and Pipe tabele set.
 Argument        : usb_utr_t    *mess
 Return value    : none
 ******************************************************************************/
static void usb_hmidi_enumeration_sequence(usb_utr_t* mess)
{
    uint16_t retval;
    uint8_t string;
    uint8_t* p_desc;
    uint8_t* p_iftable;
    uint16_t desc_len;
    uint8_t protocol;
#ifdef USB_DEBUGPRINT_PP
    uint8_t* table;
    uint8_t pdata[32], j;
#endif /* #ifdef USB_DEBUGPRINT_PP */

    /* Branch for Enumeration Sequence */
    switch (g_usb_hmidi_enum_seq[mess->ip])
    {
        /* Enumeration Sequence String Descriptor #0 receive request */
        case USB_HHID_ENUM_STR_DT0_REQ:

            /* Get String descriptor */
            usb_hmidi_get_string_desc(
                mess, g_usb_hmidi_devaddr[mess->ip], (uint16_t)0, (usb_cb_t)&usb_hmidi_check_result);

            /* String Descriptor #0 Receive wait */
            g_usb_hmidi_enum_seq[mess->ip]++;

            break;

        /* Enumeration Sequence String Descriptor #0 Receive complete */
        case USB_HHID_ENUM_STR_DT0_WAIT:

            /* String descriptor check */
            if ((usb_er_t)USB_CTRL_END == mess->status)
            {
                /* Set iProduct */
                string = g_p_usb_hmidi_device_table[mess->ip][15];

                /* Get String descriptor */
                usb_hmidi_get_string_desc(
                    mess, g_usb_hmidi_devaddr[mess->ip], (uint16_t)string, (usb_cb_t)&usb_hmidi_check_result);
            }

            /* StringDescriptor iProduct Receive */
            g_usb_hmidi_enum_seq[mess->ip]++;

            break;

            /* StringDescriptor iProduct Receive complete */
        case USB_HHID_ENUM_STR_IPRODUCT_WAIT:
            {
            }

            char const* deviceName = "Unnamed device";

            /* String descriptor check */
            if ((usb_er_t)USB_CTRL_END == mess->status)
            {
                /* String descriptor iProduct Address set */
                table = (uint8_t*)&g_usb_hmidi_str_desc_data[mess->ip];

                /* String descriptor bLength check */
                if (table[0] < (uint8_t)(32 * 2 + 2))
                {
                    /* Number of characters = bLength /2 -1  */
                    /* 1 character 16bit(UNICODE String) */
                    table[0] = (uint8_t)(table[0] / 2 - 1);
                }
                else
                {
                    /* Number of characters = 32 set */
                    table[0] = (uint8_t)32;
                }
                for (j = 0; j < table[0]; j++)
                {
                    pdata[j] = table[j * 2 + 2];
                }
                pdata[table[0]] = 0;

                deviceName = pdata;
            }
            else
            {
                USB_PRINTF0("*** Product name error\n");
            }

            // Send some info about this device to the device manager
            uint16_t vendorId  = *(uint16_t*)&g_p_usb_hmidi_device_table[USB_CFG_USE_USBIP][8];
            uint16_t productId = *(uint16_t*)&g_p_usb_hmidi_device_table[USB_CFG_USE_USBIP][10];
            giveDetailsOfDeviceBeingSetUp(USB_CFG_USE_USBIP, deviceName, vendorId, productId);

            p_desc = g_p_usb_hmidi_config_table[mess->ip];

            desc_len = ((uint16_t)*(p_desc + 3)) << 8;
            desc_len += (uint16_t)*(p_desc + 2);

            /* Searching InterfaceDescriptor */
            p_iftable = g_p_usb_hmidi_interface_table[mess->ip];

            desc_len = desc_len - (p_iftable - p_desc);

            /* pipe information table set */
            retval = usb_hmidi_pipe_info(mess, p_iftable, g_usb_hmidi_speed[mess->ip], desc_len);
            if (USB_ERROR == retval)
            {
                USB_PRINTF0("### Device information error 2 !\n");

                /* Enumeration Sequence Complete */
                g_usb_hmidi_enum_seq[mess->ip] = USB_HHID_ENUM_COMPLETE;

                /* Enumeration class check error */
                retval = USB_ERROR;
            }
            else
            {
                /* Enumeration Sequence Complete */
                g_usb_hmidi_enum_seq[mess->ip] = USB_HHID_ENUM_COMPLETE;

                /* Enumeration class check OK */
                retval = USB_OK;
            }

            /* return to MGR */
            usb_hstd_return_enu_mgr(mess, retval);

            break;

        default:
            break;
    }
} /* End of function usb_hhid_enumeration_sequence() */

extern usb_msg_t* p_usb_scheduler_add_use;

/******************************************************************************
 Function Name   : usb_hhid_task
 Description     : Host HIT Task
 Argument        : none
 Return value    : none
 ******************************************************************************/
void usb_hmidi_task(usb_vp_int_t stacd)
{
    usb_utr_t* p_mess = (usb_utr_t*)p_usb_scheduler_add_use;
    usb_er_t err      = 0l;

    if (p_mess->msginfo == USB_HHID_TCMD_OPEN)
    {
        usb_hmidi_enumeration_sequence(p_mess);
    }

    err = USB_REL_BLK(USB_HMIDI_MPL, (usb_mh_t)p_mess);
    if (USB_OK != err)
    {
        USB_PRINTF0("### USB HHID Task rel_blk error\n");
    }
} /* End of function usb_hhid_task() */

extern uint16_t g_usb_hstd_use_pipe[USB_NUM_USBIP];
/******************************************************************************
 Function Name   : usb_hhid_pipe_info
 Description     : pipe information table set
 Argument        : usb_utr_t    *ptr         : USB internal structure. Selects USB channel.
                 : uint8_t      *table       : Descriptor store address
                 : uint16_t     speed        : Conect Dpeed(Hi/Full/...)
                 : uint16_t     length       : Descriptor total lenght
                 : uint16_t     count        : Pipe tabel index
 Return value    : uint16_t                  : Error info
 ******************************************************************************/
uint16_t usb_hmidi_pipe_info(usb_utr_t* ptr, uint8_t* table, uint16_t speed, uint16_t length)
{
    /* Offset for Descriptor Top Address */
    uint16_t ofdsc;
    uint16_t retval;
    uint16_t* pipetbl;
    uint16_t detect_in_pipe  = USB_OFF;
    uint16_t detect_out_pipe = USB_OFF;

    /* Check configuration descriptor */
    if (USB_DT_INTERFACE != table[1])
    {
        USB_PRINTF0("### Descriptor is not Interface descriptor.\n");
        return USB_ERROR;
    }

    /* Descriptor Address set */
    ofdsc = table[0];

    /* Loop for Endpoint Descriptor search */
    while (ofdsc < length)
    {
        /* Branch descriptor type */
        switch (table[ofdsc + 1])
        {
            case USB_DT_DEVICE:
            case USB_DT_CONFIGURATION:
            case USB_DT_STRING:
            case USB_DT_INTERFACE:

                USB_PRINTF0("### Endpoint Descriptor error(HUB).\n");
                return USB_ERROR;

                break;

                /* Endpoint Descriptor */
            case USB_DT_ENDPOINT:

                pipetbl = R_USB_HmidiGetPipetbl(ptr, g_usb_hmidi_devaddr[ptr->ip]);

                int endpointType = table[ofdsc + 3] & USB_EP_TRNSMASK;

                // Only allow bulk or interrupt endpoints
                if (endpointType != USB_EP_BULK && endpointType != USB_EP_INT)
                    goto moveOnToNextDescriptor;

                int endpointDirection = table[ofdsc + 2] & USB_EP_DIRMASK;

                int pipeTableOffset;

                // Depending whether it's an in or out pipe, decide where to put its details
                // "Receive" pipe
                if (endpointDirection == USB_EP_IN)
                {
                    uartPrintln("found in-pipe");
                    detect_in_pipe  = USB_ON;
                    pipeTableOffset = USB_EPL;

                    int minPipe, maxPipe;
                    if (endpointType == USB_EP_BULK)
                    {
                        minPipe = USB_CFG_HMIDI_BULK_RECV_MIN;
                        maxPipe = USB_CFG_HMIDI_BULK_RECV_MAX;
                    }
                    else
                    {
                        minPipe = USB_CFG_HMIDI_INT_RECV_MIN;
                        maxPipe = USB_CFG_HMIDI_INT_RECV_MAX;
                    }

                    int pipe;

                    // Look for an unused pipe
                    for (pipe = minPipe; pipe <= maxPipe; pipe++)
                    {
                        if (!(g_usb_hstd_use_pipe[ptr->ip] & (1 << pipe)))
                            goto pickedReceivePipe;
                    }

                    // If still here, we didn't find a pipe
                    uartPrintln("no free pipe");
                    consoleTextIfAllBootedUp(l10n_get(l10n_STRING_FOR_USB_DEVICES_MAX));
                    goto moveOnToNextDescriptor;

pickedReceivePipe:
                    uartPrint("picked receive pipe: ");
                    uartPrintNumber(pipe);
                    pipetbl[pipeTableOffset] = pipe;
                }

                // "Send" pipe
                else
                {
                    uartPrintln("found out-pipe");
                    detect_out_pipe = USB_ON;
                    pipeTableOffset = 0;

                    pipetbl[pipeTableOffset] =
                        (endpointType == USB_EP_BULK) ? USB_CFG_HMIDI_BULK_SEND : USB_CFG_HMIDI_INT_SEND;
                }

                // We now have to set the buffer number for the pipe. I wasn't doing this for ages, resulting in the
                // insane bug where data sent out would be seen coming back in. I haven't double-checked, but I think
                // that even the unmodified USB host driver from Renesas was failing to set this.

                // Some pipe numbers mandate a particular buffer number - see the manual. Others I've picked
                // arbitrarily. So long as every pipe has a different buffer number than every other pipe, we won't get
                // that crazy bug as before.

                uint16_t bufferNumber;
                switch (pipetbl[pipeTableOffset])
                {
                    case USB_PIPE0:
                        bufferNumber = 0;
                        break;

                    case USB_PIPE1:
                        bufferNumber = 8;
                        break;

                    case USB_PIPE2:
                        bufferNumber = 9;
                        break;

                    case USB_PIPE3:
                        bufferNumber = 10;
                        break;

                    case USB_PIPE4:
                        bufferNumber = 11;
                        break;

                    case USB_PIPE5:
                        bufferNumber = 12;
                        break;

                    case USB_PIPE6:
                        bufferNumber = 4;
                        break;

                    case USB_PIPE7:
                        bufferNumber = 5;
                        break;

                    case USB_PIPE8:
                        bufferNumber = 6;
                        break;

                    case USB_PIPE9:
                        bufferNumber = 7;
                        break;
                }

                pipetbl[pipeTableOffset + 2] = (uint16_t)USB_BUF_SIZE(64u) | USB_BUF_NUMB(bufferNumber);

                /* Check pipe information */
                retval = usb_hstd_chk_pipe_info(speed, (uint16_t*)&pipetbl[pipeTableOffset],
                    &table[ofdsc]); // We could check the return for error if we wanted...

                if ((USB_ON == detect_in_pipe) && (USB_ON == detect_out_pipe))
                {
                    return USB_OK;
                }

                // No break

            default:
moveOnToNextDescriptor:
                if (!table[ofdsc])
                    return USB_ERROR;  // Prevent infinite loop if there's somehow a zero there. Got this while quickly
                                       // plugging and unplugging lots of devices / hub, to test.
                ofdsc += table[ofdsc]; /* Next descriptor point set */

                break;

            case USB_DT_DEVICE_QUALIFIER:
            case USB_DT_OTHER_SPEED_CONF:
            case USB_DT_INTERFACE_POWER:

                USB_PRINTF0("### Endpoint Descriptor error.\n");
                return USB_ERROR;

                break;
        }
    }

    return USB_ERROR;
} /* End of function usb_hhid_pipe_info() */

/******************************************************************************
 Function Name   : usb_hhid_get_string_desc
 Description     : Set GetDescriptor
 Arguments       : usb_utr_t    *ptr         : USB internal structure. Selects USB channel.
                 : uint16_t     addr         : device address
                 : uint16_t     string       : descriptor index
                 : usb_cb_t     complete     : callback function
 Return value    : uint16_t                  : error info
 ******************************************************************************/
uint16_t usb_hmidi_get_string_desc(usb_utr_t* ptr, uint16_t addr, uint16_t string, usb_cb_t complete)
{
    uint16_t i;

    if (0 == string)
    {
        usb_shmidi_class_request_setup[ptr->ip][2] = (uint16_t)0x0000;
        usb_shmidi_class_request_setup[ptr->ip][3] = (uint16_t)0x0004;
    }
    else
    {
        /* Set LanguageID */
        usb_shmidi_class_request_setup[ptr->ip][2] = (uint16_t)(g_usb_hmidi_str_desc_data[ptr->ip][2]);
        usb_shmidi_class_request_setup[ptr->ip][2] |=
            (uint16_t)((uint16_t)(g_usb_hmidi_str_desc_data[ptr->ip][3]) << 8);
        usb_shmidi_class_request_setup[ptr->ip][3] = (uint16_t)USB_HMIDI_CLSDATASIZE;
    }
    usb_shmidi_class_request_setup[ptr->ip][0] = (USB_GET_DESCRIPTOR | USB_DEV_TO_HOST | USB_STANDARD | USB_DEVICE);
    usb_shmidi_class_request_setup[ptr->ip][1] = (uint16_t)(USB_STRING_DESCRIPTOR + string);
    usb_shmidi_class_request_setup[ptr->ip][4] = addr;

    for (i = 0; i < usb_shmidi_class_request_setup[ptr->ip][3]; i++)
    {
        g_usb_hmidi_str_desc_data[ptr->ip][i] = (uint8_t)0xFF;
    }

    return usb_hmidi_cmd_submit(ptr, complete);
} /* End of function usb_hhid_get_string_desc() */

/******************************************************************************
 Function Name   : usb_hhid_cmd_submit
 Description     : command submit
 Arguments       : usb_utr_t    *ptr         : USB internal structure. Selects USB channel.
                 : usb_cb_t     complete     : callback info
 Return value    : uint16_t                  : USB_DONE
 ******************************************************************************/
static uint16_t usb_hmidi_cmd_submit(usb_utr_t* ptr, usb_cb_t complete)
{
    usb_er_t err;

    usb_shmidi_string_req[ptr->ip].p_tranadr = (void*)g_usb_hmidi_str_desc_data[ptr->ip];
    usb_shmidi_string_req[ptr->ip].complete  = complete;
    usb_shmidi_string_req[ptr->ip].tranlen   = (uint32_t)usb_shmidi_class_request_setup[ptr->ip][3];
    usb_shmidi_string_req[ptr->ip].keyword   = USB_PIPE0;
    usb_shmidi_string_req[ptr->ip].p_setup   = usb_shmidi_class_request_setup[ptr->ip];
    usb_shmidi_string_req[ptr->ip].segment   = USB_TRAN_END;
    usb_shmidi_string_req[ptr->ip].ip        = ptr->ip;
    usb_shmidi_string_req[ptr->ip].ipp       = ptr->ipp;

    err = usb_hstd_transfer_start(&usb_shmidi_string_req[ptr->ip]);
    if (USB_QOVR == err)
    {
        /* error */
        USB_PRINTF0("### usb_hhid_cmd_submit() : USB_E_QOVR \n");
    }

    return USB_OK;
} /* End of function usb_hhid_cmd_submit() */

/******************************************************************************
 Callback functions
 ******************************************************************************/

void hostedDeviceConfigured(int ip, int midiDeviceNum);

/******************************************************************************
 Function Name   : hid_configured
 Description     : Callback function for HID device configuered
 Arguments       : usb_utr_t    *ptr         : USB internal structure. Selects e.g. channel.
                 : uint16_t     devadr       : Device Adrress
                 : uint16_t     data2        : Not use
 Return value    : none
 ******************************************************************************/
void hmidi_configured(usb_utr_t* ptr, uint16_t devadr, uint16_t data2)
{
    R_USB_HmidiSetPipeRegistration(ptr, devadr); /* Host HID Pipe registration */

    uint16_t* pipetbl = R_USB_HmidiGetPipetbl(ptr, devadr);

    // Totally hackish way of getting the device number from our offset from the start of the "endpoint table", but it's
    // the only way the "driver" actually keeps track of this
    int midiDeviceNum = (pipetbl - (uint16_t*)g_usb_hmidi_tmp_ep_tbl) / ((USB_EPL * 2) + 1);

    uartPrint("configured MIDI device: ");
    uartPrintNumber(midiDeviceNum);

    int sendPipe    = g_usb_hmidi_tmp_ep_tbl[ptr->ip][midiDeviceNum][0];
    int isInterrupt = (sendPipe == USB_CFG_HMIDI_INT_SEND);

    if (currentDeviceNumWithSendPipe[isInterrupt] == midiDeviceNum)
        currentDeviceNumWithSendPipe[isInterrupt] =
            MAX_NUM_USB_MIDI_DEVICES; // Force a reset of pipe setup if it was already set to this deviceNum

    hostedDeviceConfigured(USB_CFG_USE_USBIP, midiDeviceNum);

} /* End of function hid_configured() */

void hostedDeviceDetached(int ip, int midiDeviceNum);

/******************************************************************************
 Function Name   : hid_detach
 Description     : Callback function for HID device detach
 Arguments       : usb_utr_t    *ptr         : USB internal structure. Selects e.g. channel.
                 : uint16_t     devadr       : Device Adrress
                 : uint16_t     data2        : Not use
 Return value    : none
 ******************************************************************************/
void hmidi_detach(usb_utr_t* ptr, uint16_t devadr, uint16_t data2)
{
    // Have to get the MIDI device num by searching through the info left in the endpoint table
    int d;
    for (d = 0; d < MAX_NUM_USB_MIDI_DEVICES; d++)
    {
        int devAddrHere = g_usb_hmidi_tmp_ep_tbl[USB_CFG_USE_USBIP][d][3] >> USB_DEVADDRBIT;
        if (devAddrHere == devadr)
            break;
    }

    // Clear endpoint table
    g_usb_hmidi_tmp_ep_tbl[USB_CFG_USE_USBIP][d][1] &= (uint16_t)(USB_BFREON | USB_CFG_SHTNAKON); /* PIPECFG */
    g_usb_hmidi_tmp_ep_tbl[USB_CFG_USE_USBIP][d][3] = (uint16_t)(USB_NULL);                       /* PIPEMAXP */
    g_usb_hmidi_tmp_ep_tbl[USB_CFG_USE_USBIP][d][4] = (uint16_t)(USB_NULL);                       /* PIPEPERI */

    hostedDeviceDetached(USB_CFG_USE_USBIP, d);
} /* End of function hid_detach() */

/******************************************************************************
 Function Name   : hid_resume_complete
 Description     : Resume complete callback.
 Arguments       : usb_utr_t    *ptr         : USB internal structure. Selects e.g. channel.
                 : uint16_t     devadr       : Device Adrress
                 : uint16_t     data2        : Not use
 Return value    : none
 ******************************************************************************/
void hmidi_resume_complete(usb_utr_t* ptr, uint16_t devadr, uint16_t data2)
{
} /* End of function hid_resume_complete() */

/******************************************************************************
 Initialization functions
 ******************************************************************************/

/******************************************************************************
 Function Name   : usb_registration
 Description     : sample registration.
 Argument        : none
 Return          : none
 ******************************************************************************/
void usb_registration(usb_utr_t* ptr)
{
    usb_hcdreg_t driver;
    uint8_t i;

    driver.ifclass    = (uint16_t)USB_IFCLS_AUD;                /* Interface class */
    driver.p_tpl      = (uint16_t*)&g_usb_hmidi_devicetpl;      /* Target peripheral list */
    driver.classinit  = (usb_cb_t)&usb_hstd_dummy_function;     /* Driver init */
    driver.classcheck = (usb_cb_check_t)&R_USB_HmidiClassCheck; /* Driver check */
    driver.devconfig  = (usb_cb_t)&hmidi_configured;            /* Device configuered */
    driver.devdetach  = (usb_cb_t)&hmidi_detach;                /* Device detach */
    driver.devsuspend = (usb_cb_t)&usb_hstd_dummy_function;     /* Device suspend */
    driver.devresume  = (usb_cb_t)&hmidi_resume_complete;       /* Device resume */

    for (i = 0; i < MAX_NUM_USB_MIDI_DEVICES; i++) /* Loop support for MIDI device count */
    {
        driver.p_pipetbl = (uint16_t*)&g_usb_hmidi_tmp_ep_tbl[ptr->ip][i]; /* Pipe def. table address. */
        usb_hstd_driver_registration(ptr, &driver);                        /* Host HID class driver registration. */
    }
    usb_cstd_set_task_pri(USB_HUB_TSK, USB_PRI_3);       /* Hub Task Priority set */
    usb_hhub_registration(ptr, (usb_hcdreg_t*)USB_NULL); /* Hub registration. */
} /* End of function usb_registration() */

/******************************************************************************
 End Of File
 ******************************************************************************/
