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
 * File Name    : r_usb_hinthandler_usbip1.c
 * Description  : USB IP1 Host interrupt handler code
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

#if ((USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST)
#if USB_NUM_USBIP == 2
/***********************************************************************************************************************
 Exported global variables
 ***********************************************************************************************************************/
extern usb_utr_t g_usb_cstd_int_msg_t[][USB_INTMSGMAX]; /* Interrupt message */
extern uint16_t g_usb_cstd_int_msg_t_cnt[];             /* Interrupt message count */

/***********************************************************************************************************************
 Exported global variables (to be accessed by other files)
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Renesas Abstracted common Interrupt handler functions
 ***********************************************************************************************************************/

/***********************************************************************************************************************
 Function Name   : usb2_hstd_usb_handler
 Description     : USB2 interrupt routine. Analyze which USB interrupt occurred
                 : and send message to PCD/HCD task.
 Arguments       : none
 Return value    : none
 ***********************************************************************************************************************/
void usb2_hstd_usb_handler(void)
{
    usb_utr_t* ptr;
    usb_er_t err;

    /* Initial pointer */
    ptr      = &g_usb_cstd_int_msg_t[1][g_usb_cstd_int_msg_t_cnt[1]];
    ptr->ip  = USB_USBIP_1;
    ptr->ipp = usb_hstd_get_usb_ip_adr(ptr->ip);

    /* Host Function */
    /* Host Interrupt handler */
    usb_hstd_interrupt_handler(ptr);
    ptr->msghead = (usb_mh_t)USB_NULL;
    /* Send message */
    err = USB_ISND_MSG(USB_HCD_MBX, (usb_msg_t*)ptr);
    if (err != USB_OK)
    {
        /*USB_PRINTF1("### lib_UsbHandler DEF2 isnd_msg error (%ld)\n", err);*/
    }

    /* Renewal Message count  */
    g_usb_cstd_int_msg_t_cnt[1]++;
    if (USB_INTMSGMAX == g_usb_cstd_int_msg_t_cnt[1])
    {
        g_usb_cstd_int_msg_t_cnt[1] = 0;
    }
} /* End of function usb2_hstd_usb_handler() */

#endif /* #if USB_NUM_USBIP == 2 */
#endif /* (USB_CFG_MODE & USB_CFG_HOST) == USB_CFG_HOST */

/***********************************************************************************************************************
 End Of File
 ***********************************************************************************************************************/
