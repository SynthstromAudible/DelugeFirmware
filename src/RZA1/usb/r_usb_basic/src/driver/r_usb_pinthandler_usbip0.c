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
 * File Name    : r_usb_pinthandler_usbip0.c
 * Description  : USB Peripheral interrupt handler code
 ***********************************************************************************************************************/
/**********************************************************************************************************************
 * History : DD.MM.YYYY Version Description
 *         : 01.06.2016 1.00    First Release
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 ***********************************************************************************************************************/
#include "RZA1/mtu/mtu.h"
#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_extern.h"
#include "RZA1/usb/r_usb_basic/src/driver/inc/r_usb_typedef.h"
#include "definitions.h"

#if ((USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_PERI)
/***********************************************************************************************************************
 Exported global variables (to be accessed by other files)
***********************************************************************************************************************/
usb_int_t g_usb_pstd_usb_int;

/***********************************************************************************************************************
 Renesas Abstracted common Interrupt handler functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb_pstd_usb_handler
 Description     : USB interrupt routine. Analyze which USB interrupt occurred
                 : and send message to PCD task.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb_pstd_usb_handler(uint32_t sense)
{
    // uint16_t startTime = *TCNT[TIMER_SYSTEM_SUPERFAST];

    usb_pstd_interrupt_clock();

    /* Push Interrupt info */
    int alreadyAllDealtWith = usb_pstd_interrupt_handler(
        &g_usb_pstd_usb_int.buf[g_usb_pstd_usb_int.wp].type, &g_usb_pstd_usb_int.buf[g_usb_pstd_usb_int.wp].status);

    if (!alreadyAllDealtWith)
    { // return; // By Rohan

        /* Write countup */
        g_usb_pstd_usb_int.wp = ((g_usb_pstd_usb_int.wp + 1) % USB_INT_BUFSIZE);
    }

    /*
    uint16_t endTime = *TCNT[TIMER_SYSTEM_SUPERFAST];
    uint16_t duration = endTime - startTime;
    uint32_t timePassedNS = superfastTimerCountToNS(duration);
    uartPrint("interrupt duration, nSec: ");
    uartPrintNumber(timePassedNS);
    */
} /* End of function usb_pstd_usb_handler() */
#endif /* (USB_CFG_MODE & USB_CFG_PERI) == USB_CFG_REPI */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
