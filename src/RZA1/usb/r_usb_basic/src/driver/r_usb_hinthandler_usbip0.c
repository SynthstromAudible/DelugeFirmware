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
 * Copyright (C) 2016 Renesas Electronics Corporation. All rights reserved.
 ***********************************************************************************************************************/
/***********************************************************************************************************************
 * File Name    : r_usb_hinthandler_usbip0.c
 * Description  : USB IP0 Host and Peripheral interrupt handler code
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 01.06.2016 1.00    First Release
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 ***********************************************************************************************************************/
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_extern.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_typedef.h"

// Added by Rohan
#include "RZA1/mtu/mtu.h"
#include "definitions.h"

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
/***********************************************************************************************************************
 Exported global variables (to be accessed by other files)
 ***********************************************************************************************************************/
usb_utr_t g_usb_cstd_int_msg_t[USB_NUM_USBIP][USB_INTMSGMAX]; /* Interrupt message */
uint16_t g_usb_cstd_int_msg_t_cnt[USB_NUM_USBIP];             /* Interrupt message count */
usb_utr_t g_usb_cstd_int_msg_t_d0fifo;

/***********************************************************************************************************************
 Renesas Abstracted common Interrupt handler functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_hstd_usb_handler
 Description     : USB interrupt routine. Analyze which USB interrupt occurred
                 : and send message to PCD/HCD task.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_usb_handler(
    uint32_t sense) // This function actually gets called as part of a CPU interrupt, for real! Rohan
{
    usb_utr_t* ptr;
    usb_er_t err;

    /* Initial pointer */
    ptr      = &g_usb_cstd_int_msg_t[0][g_usb_cstd_int_msg_t_cnt[0]];
    ptr->ip  = USB_USBIP_0;
    ptr->ipp = usb_hstd_get_usb_ip_adr(ptr->ip);

    /* Host Function */
    /* Host Interrupt handler */

    // uint16_t startTime = *TCNT[TIMER_SYSTEM_SUPERFAST];

    int result = usb_hstd_interrupt_handler(ptr);

    /*
    uint16_t endTime = *TCNT[TIMER_SYSTEM_SUPERFAST];
    uint16_t duration = endTime - startTime;
    uint32_t timePassedNS = superfastTimerCountToNS(duration);
    uartPrint("host interrupt duration, nSec: ");
    uartPrintNumber(timePassedNS);
    */

    if (result)
        return; // By Rohan

    ptr->msghead = (usb_mh_t)USB_NULL;

    /* Send message */
    err = USB_ISND_MSG(USB_HCD_MBX, (usb_msg_t*)ptr);
    if (USB_OK != err)
    {
        USB_PRINTF1("### lib_UsbHandler DEF2 isnd_msg error (%ld)\n", err);
    }

    /* Renewal Message count  */
    g_usb_cstd_int_msg_t_cnt[0]++;
    if (USB_INTMSGMAX == g_usb_cstd_int_msg_t_cnt[0])
    {
        g_usb_cstd_int_msg_t_cnt[0] = 0;
    }
} /* End of function usb_hstd_usb_handler() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_dma_handler
 Description     : DMA interrupt routine. Send message to PCD/HCD task.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_dma_handler(void)
{
} /* End of function usb_hstd_dma_handler() */

/***********************************************************************************************************************
 Function Name   : usb_hstd_init_usb_message
 Description     : Initialization of message to be used at time of USB interrupt.
 Arguments       : usb_utr_t    *ptr         : Pointer to usb_utr_t structure.
 Return value    : none
 ***********************************************************************************************************************/
void usb_hstd_init_usb_message(usb_utr_t* ptr)
{
    /* TD.5.4 The interruption message of PCD is notified to HCD.  */
    uint16_t i;
    uint16_t msg_info;

    /* Host Function */
    msg_info = USB_MSG_HCD_INT;

    for (i = 0; i < USB_INTMSGMAX; i++)
    {
        g_usb_cstd_int_msg_t[ptr->ip][i].msginfo = msg_info;
    }

    g_usb_cstd_int_msg_t_cnt[ptr->ip] = 0;
} /* End of function usb_hstd_init_usb_message() */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
