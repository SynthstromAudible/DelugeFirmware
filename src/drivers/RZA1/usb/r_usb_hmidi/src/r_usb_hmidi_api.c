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
 * File Name    : r_usb_hhid_api.c
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

#include "r_usb_hmidi_if.h"
#include "r_usb_hmidi.h"
#include "r_usb_basic_if.h"
#include "r_usb_typedef.h"
#include "r_usb_extern.h"
#include "r_usb_reg_access.h"
#include "r_usb_hmidi_config.h"

/******************************************************************************
 Renesas Abstracted USB Driver functions
 ******************************************************************************/

/******************************************************************************
 Function Name   : R_USB_HhidDriverStart
 Description     : USB Host Initialize process
 Arguments       : usb_utr_t    *ptr         : IP info (mode, IP no., reg address).
 Return          : none
 ******************************************************************************/
void R_USB_HmidiDriverStart(usb_utr_t* ptr)
{
    usb_cstd_set_task_pri(USB_HMIDI_TSK, USB_PRI_3); /* Host HID task priority set */
} /* End of function R_USB_HhidDriverStart() */

uint8_t usbHostOutPipeInitializedForHub[2] = {0, 0};

/******************************************************************************
 Function Name   : R_USB_HhidSetPipeRegistration
 Description     : Host HID Pipe registration
 Arguments       : usb_utr_t    *ptr         : IP info (mode, IP no., reg address).
                 : uint16_t     devadr       : Device address
 Return          : none
 ******************************************************************************/
void R_USB_HmidiSetPipeRegistration(usb_utr_t* ptr, uint16_t devadr)
{
    uint16_t* pipetbl;

    pipetbl = R_USB_HmidiGetPipetbl(ptr, devadr);

    /* Device Address Set for Endpoint Table */
    pipetbl[3] |= (uint16_t)(devadr << USB_DEVADDRBIT);

    pipetbl[3 + USB_EPL] |= (uint16_t)(devadr << USB_DEVADDRBIT); /* OUT Pipe */

    int pipeToSetup;

    // If not on hub...
    if (devadr == 1)
    {

        // Note that if we go back on a hub in the future, everything's not set up
        usbHostOutPipeInitializedForHub[0] = 0;
        usbHostOutPipeInitializedForHub[1] = 0;

setupBothPipes:
        pipeToSetup = USB_USEPIPE; // Set up all pipes
    }

    // Or, if we are on a hub...
    else
    {
        int sendPipe    = pipetbl[0];
        int isInterrupt = (sendPipe == USB_CFG_HMIDI_INT_SEND);

        // If the send-pipe hasn't been set up yet, we want to do that now
        if (!usbHostOutPipeInitializedForHub[isInterrupt])
        {
            usbHostOutPipeInitializedForHub[isInterrupt] = 1;
            goto setupBothPipes;
        }

        // Otherwise, if the send-pipe has already been set up, we only have to set up the receive-pipe
        pipeToSetup = pipetbl[USB_EPL];
    }

    /* Set pipe configuration request(Interrupt IN) */
    usb_hstd_set_pipe_registration(ptr, pipetbl, pipeToSetup);
} /* End of function R_USB_HhidSetPipeRegistration() */

/******************************************************************************
 Function Name   : R_USB_HhidClassCheck
 Description     : Driver check.
 Argument        : usb_utr_t    *ptr
                 : uint16_t     **table
 Return value    : none
 ******************************************************************************/
void R_USB_HmidiClassCheck(usb_utr_t* ptr, uint16_t** table)
{
    uartPrintln("R_USB_HaudClassCheck");
    usb_mh_t p_blf;
    usb_er_t err;
    usb_clsinfo_t* cp;

    g_p_usb_hmidi_device_table[ptr->ip]    = (uint8_t*)((uint16_t*)table[0]); /* Device Descriptor Table */
    g_p_usb_hmidi_config_table[ptr->ip]    = (uint8_t*)((uint16_t*)table[1]); /* Configuration Descriptor Table */
    g_usb_hmidi_speed[ptr->ip]             = (uint16_t)(*table[6]);           /* Device speed */
    g_usb_hmidi_devaddr[ptr->ip]           = (uint16_t)(*table[7]);           /* Device Address */
    g_p_usb_hmidi_pipe_table[ptr->ip]      = (uint16_t*)(table[8]);           /* Pipe Table(DefEP) */
    g_p_usb_hmidi_interface_table[ptr->ip] = (uint8_t*)((uint16_t*)table[2]); /* Interface Descriptor Table */

    /* Enumeration Sequence String Descriptor #0 receive request */
    g_usb_hmidi_enum_seq[ptr->ip] = (uint16_t)USB_HHID_ENUM_STR_DT0_REQ;

    /* Descriptor check result */
    *table[3] = USB_OK;

    /* Get mem block from pool */
    err = USB_PGET_BLK(USB_HMIDI_MPL, &p_blf);
    if (USB_OK == err)
    {
        cp          = (usb_clsinfo_t*)p_blf;
        cp->msghead = (usb_mh_t)USB_NULL;
        cp->msginfo = USB_HHID_TCMD_OPEN; /* Set message information :USB transfer. */
        cp->ip      = ptr->ip;            /* IP number (0 or 1) */
        cp->ipp     = ptr->ipp;           /* IP address (USB0 or USB1) */

        /* Send message */
        err = USB_SND_MSG(USB_HMIDI_MBX, (usb_msg_t*)cp);
        if (USB_OK != err)
        {
            /* Error */
            err = USB_REL_BLK(USB_HMIDI_MPL, (usb_mh_t)p_blf);
            USB_PRINTF1("Host Sample snd_msg error %x\n", err);

            /* Transfer start request send NG */
        }
    }
    else
    {
        /* Error */
        USB_PRINTF0("### R_USB_HhidClassCheck function pget_blk error\n");

        /* Get mem block from pool */
    }
} /* End of function R_USB_HhidClassCheck() */

/******************************************************************************
 Function Name   : R_USB_HhidGetPipetbl
 Description     : Get pipe table address.
 Arguments       : usb_utr_t    *ptr         : IP info (mode, IP no., reg address).
                 : uint16_t     devadr       : Device address
 Return value    : Pipe table address.
 ******************************************************************************/
uint16_t* R_USB_HmidiGetPipetbl(usb_utr_t* ptr, uint16_t devadr)
{
    if (devadr == g_usb_hmidi_devaddr[ptr->ip]) /* Device Address */
    {
        return (uint16_t*)g_p_usb_hmidi_pipe_table[ptr->ip]; /* Pipe Table(DefEP) */
    }

    return (uint16_t*)USB_ERROR;
} /* End of function R_USB_HhidGetPipetbl() */

/******************************************************************************
 End Of File
 ******************************************************************************/
